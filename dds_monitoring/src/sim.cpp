/**
 * ddsmon-sim —— 三个域的模拟节点，用于联调与故障注入。
 *
 * 感知域:  camera_front(30Hz) / object_list(30Hz) / lidar_top(10Hz)
 * 规控域:  obstacle_list(30Hz，桥接感知输出) / trajectory(10Hz) /
 *          control_cmd(50Hz) / vehicle_state(100Hz)
 * 监控域:  三节点 1Hz 心跳；monitor 节点同时订阅诊断报告形成闭环
 */

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "ddsmon/config.hpp"
#include "ddsmon/types.hpp"
#include "ddsmon/util.hpp"
#include "ddsmon.h"

namespace {

struct Faults {
  bool loss{false};   // 每 5 帧丢 1 帧（序号仍自增）
  bool stale{false};  // 感知输出时间戳回退 300ms
  bool stall{false};  // trajectory 每 10s 停发 3s
  bool qos{false};    // control_cmd 用 BEST_EFFORT 发布，与 RELIABLE 订阅者不兼容
  bool type{false};   // control_cmd 上再挂一个不同类型名的发布者
  bool kill{false};   // lidar_top 完全停发
};

struct Args {
  std::string nodes{"all"};
  std::string config{"config/ddsmon.json"};
  std::string faults{"none"};
  double duration_sec{0};
  double rate_scale{1.0};
  double stale_ms{300};
};

template <typename Seq, typename Elem>
void seq_alloc(Seq* seq, size_t n)
{
  seq->_buffer = static_cast<Elem*>(dds_alloc(sizeof(Elem) * n));
  std::memset(seq->_buffer, 0, sizeof(Elem) * n);
  seq->_length = static_cast<uint32_t>(n);
  seq->_maximum = static_cast<uint32_t>(n);
  seq->_release = true;
}

dds_entity_t make_writer(dds_entity_t pp, const ddsmon::PayloadAdapter* ad, const std::string& topic,
                         bool reliable, dds_entity_t* topic_out = nullptr)
{
  dds_entity_t tp = dds_create_topic(pp, ad->desc, topic.c_str(), nullptr, nullptr);
  if (tp < 0) {
    std::cerr << "create topic " << topic << " failed: " << dds_strretcode(tp) << "\n";
    return tp;
  }
  dds_qos_t* q = dds_create_qos();
  dds_qset_reliability(q, reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                       DDS_SECS(1));
  dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 16);
  dds_entity_t w = dds_create_writer(pp, tp, q, nullptr);
  dds_delete_qos(q);
  if (topic_out) *topic_out = tp;
  return w;
}

dds_entity_t make_reader(dds_entity_t pp, const ddsmon::PayloadAdapter* ad, const std::string& topic,
                         bool reliable)
{
  dds_entity_t tp = dds_create_topic(pp, ad->desc, topic.c_str(), nullptr, nullptr);
  if (tp < 0) return tp;
  dds_qos_t* q = dds_create_qos();
  dds_qset_reliability(q, reliable ? DDS_RELIABILITY_RELIABLE : DDS_RELIABILITY_BEST_EFFORT,
                       DDS_SECS(1));
  dds_qset_history(q, DDS_HISTORY_KEEP_LAST, 32);
  dds_entity_t r = dds_create_reader(pp, tp, q, nullptr);
  dds_delete_qos(q);
  return r;
}

class Simulator {
 public:
  bool init(const ddsmon::Config& cfg, const std::string& nodes, const Faults& faults,
            double rate_scale, double stale_ms)
  {
    cfg_ = cfg;
    faults_ = faults;
    rate_scale_ = rate_scale > 0 ? rate_scale : 1.0;
    stale_ms_ = stale_ms > 0 ? stale_ms : 300.0;
    run_perception_ = nodes == "all" || nodes == "perception";
    run_planning_ = nodes == "all" || nodes == "planning";
    run_monitor_ = nodes == "all" || nodes == "monitor";

    if (!run_perception_ && !run_planning_ && !run_monitor_) {
      std::cerr << "未知节点: " << nodes << "（可选 perception|planning|monitor|all）\n";
      return false;
    }

    auto dom = [&](const std::string& name) -> int {
      const ddsmon::DomainCfg* d = cfg_.domain_by_name(name);
      return d ? d->domain_id : -1;
    };
    int dom_perception = dom("perception");
    int dom_planning = dom("planning");
    int dom_diag = dom("diag");

    if (run_perception_ && dom_perception >= 0) {
      if (!init_perception(dom_perception)) return false;
    }
    if (run_planning_ && dom_planning >= 0) {
      if (!init_planning(dom_planning, dom_perception)) return false;
    }
    if (dom_diag >= 0 && (run_perception_ || run_planning_ || run_monitor_)) {
      if (!init_diag(dom_diag)) return false;
    }
    return true;
  }

  void run(double duration_sec)
  {
    int64_t start = ddsmon::now_ns();
    int64_t hard_end = duration_sec > 0 ? start + static_cast<int64_t>(duration_sec * 1e9) : 0;

    while (true) {
      int64_t now = ddsmon::now_ns();
      if (hard_end > 0 && now >= hard_end) break;

      if (run_perception_) publish_perception(now);
      if (run_planning_) publish_planning(now);
      if (run_perception_ || run_planning_ || run_monitor_) publish_heartbeats(now);
      poll_bridge(now);
      poll_reports(now);

      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

 private:
  // ------------------------------------------------------------ 感知域
  bool init_perception(int domain_id)
  {
    pp_perception_ = dds_create_participant(static_cast<dds_domainid_t>(domain_id), nullptr, nullptr);
    if (pp_perception_ < 0) {
      std::cerr << "perception participant failed: " << dds_strretcode(pp_perception_) << "\n";
      return false;
    }
    const auto* cam = ddsmon::find_adapter("CameraFrame");
    const auto* obj = ddsmon::find_adapter("ObjectList");
    const auto* lidar = ddsmon::find_adapter("LidarCloud");

    w_camera_ = make_writer(pp_perception_, cam, "rt/perception/camera_front", true);
    w_object_ = make_writer(pp_perception_, obj, "rt/perception/object_list", true);
    if (!faults_.kill) {
      w_lidar_ = make_writer(pp_perception_, lidar, "rt/perception/lidar_top", false);
    }

    seq_alloc<dds_sequence_ddsmon_perception_Object, ddsmon_perception_Object>(&objects_, 8);
    std::memset(&obj_msg_, 0, sizeof(obj_msg_));
    obj_msg_.objects = objects_;
    obj_msg_.camera_id = 0;
    obj_msg_.object_num = 8;

    // 感知域内的下游消费者（融合节点 / 激光雷达处理），避免把真实系统误报成「无订阅者」
    rd_camera_ = make_reader(pp_perception_, cam, "rt/perception/camera_front", false);
    rd_lidar_ = make_reader(pp_perception_, lidar, "rt/perception/lidar_top", false);
    return w_camera_ >= 0 && w_object_ >= 0;
  }

  void publish_perception(int64_t now)
  {
    if (faults_.kill && w_lidar_ > 0) {
      dds_delete(w_lidar_);
      w_lidar_ = DDS_ENTITY_NIL;
    }

    if (due("camera", rate("perception", "rt/perception/camera_front", 30.0), now)) {
      uint32_t seq = ++seq_["camera"];
      bool drop = faults_.loss && (seq % 5 == 0);
      if (!drop) {
        ddsmon_perception_CameraFrame m;
        std::memset(&m, 0, sizeof(m));
        m.seq = seq;
        m.stamp_ns = now;
        m.camera_id = 0;
        m.width = 1920;
        m.height = 1080;
        m.data_bytes = 5 * 1024 * 1024;
        m.encoding = const_cast<char*>("h264");
        dds_write_ts(w_camera_, &m, now);
      }
    }

    if (due("object_list", rate("perception", "rt/perception/object_list", 30.0), now)) {
      uint32_t seq = ++seq_["object_list"];
      int64_t stamp = faults_.stale ? now - static_cast<int64_t>(stale_ms_ * 1e6) : now;
      obj_msg_.seq = seq;
      obj_msg_.stamp_ns = stamp;
      for (size_t i = 0; i < 8; ++i) {
        ddsmon_perception_Object& o = objects_._buffer[i];
        o.id = static_cast<uint32_t>(i + 1);
        o.category = static_cast<int32_t>(1 + (i % 3));
        o.x = 12.0 + 3.5 * static_cast<double>(i);
        o.y = -3.0 + 0.8 * static_cast<double>(i);
        o.yaw = 0.02 * static_cast<double>(i);
        o.length = 4.4;
        o.width = 1.8;
        o.confidence = 0.92 - 0.02 * static_cast<double>(i);
        o.velocity = 10.0 + static_cast<double>(i);
      }
      dds_write_ts(w_object_, &obj_msg_, stamp);
    }

    if (w_lidar_ > 0) {
      if (due("lidar", rate("perception", "rt/perception/lidar_top", 10.0), now)) {
        uint32_t seq = ++seq_["lidar"];
        ddsmon_perception_LidarCloud m;
        std::memset(&m, 0, sizeof(m));
        m.seq = seq;
        m.stamp_ns = now;
        m.ring = 4;
        m.point_num = 120000;
        m.min_range = 0.3;
        m.max_range = 120.0;
        dds_write_ts(w_lidar_, &m, now);
      }
    }
  }

  // ------------------------------------------------------------ 规控域
  bool init_planning(int domain_id, int perception_domain)
  {
    pp_planning_ = dds_create_participant(static_cast<dds_domainid_t>(domain_id), nullptr, nullptr);
    if (pp_planning_ < 0) {
      std::cerr << "planning participant failed: " << dds_strretcode(pp_planning_) << "\n";
      return false;
    }

    const auto* obs = ddsmon::find_adapter("ObstacleList");
    const auto* traj = ddsmon::find_adapter("Trajectory");
    const auto* cmd = ddsmon::find_adapter("ControlCommand");
    const auto* state = ddsmon::find_adapter("VehicleState");

    w_obstacle_ = make_writer(pp_planning_, obs, "rt/perception/obstacle_list", true);
    w_trajectory_ = make_writer(pp_planning_, traj, "rt/planning/trajectory", true);
    w_control_ = make_writer(pp_planning_, cmd, "rt/planning/control_cmd", !faults_.qos);
    w_vehicle_ = make_writer(pp_planning_, state, "rt/planning/vehicle_state", false);

    // 真实控制器：RELIABLE 订阅 control_cmd。qos 故障时与 BEST_EFFORT 发布者无法匹配，
    // 诊断工具需要把这对 QoS 冲突报出来。
    rd_control_ = make_reader(pp_planning_, cmd, "rt/planning/control_cmd", true);

    // 规控域内其他下游消费者：规划器读障碍物、控制器读轨迹、HMI 读车辆状态
    rd_obstacle_ = make_reader(pp_planning_, obs, "rt/perception/obstacle_list", true);
    rd_trajectory_ = make_reader(pp_planning_, traj, "rt/planning/trajectory", true);
    rd_vehicle_ = make_reader(pp_planning_, state, "rt/planning/vehicle_state", false);

    seq_alloc<dds_sequence_ddsmon_planning_Obstacle, ddsmon_planning_Obstacle>(&obstacles_, 8);
    seq_alloc<dds_sequence_ddsmon_planning_TrajectoryPoint, ddsmon_planning_TrajectoryPoint>(
        &traj_points_, 40);

    std::memset(&obstacle_msg_, 0, sizeof(obstacle_msg_));
    obstacle_msg_.obstacles = obstacles_;
    obstacle_msg_.object_num = 8;

    std::memset(&traj_msg_, 0, sizeof(traj_msg_));
    traj_msg_.points = traj_points_;
    traj_msg_.point_num = 40;

    // 桥接：订阅感知域 object_list，把「感知输出时间戳」透传到规控域
    if (perception_domain >= 0) {
      pp_bridge_ = dds_create_participant(static_cast<dds_domainid_t>(perception_domain), nullptr, nullptr);
      const auto* obj = ddsmon::find_adapter("ObjectList");
      rd_object_ = make_reader(pp_bridge_, obj, "rt/perception/object_list", false);
    }

    // 类型冲突故障：另起 participant，用不同类型在同一话题上发布
    if (faults_.type) {
      pp_bad_ = dds_create_participant(static_cast<dds_domainid_t>(domain_id), nullptr, nullptr);
      w_bad_type_ = make_writer(pp_bad_, traj, "rt/planning/control_cmd", true);
    }

    return w_obstacle_ >= 0 && w_trajectory_ >= 0 && w_control_ >= 0;
  }

  void poll_bridge(int64_t now)
  {
    if (rd_object_ <= 0) return;
    constexpr int kBatch = 8;
    ddsmon_perception_ObjectList* buf[kBatch] = {nullptr};
    dds_sample_info_t si[kBatch];
    dds_return_t n = dds_take(rd_object_, reinterpret_cast<void**>(buf), si, kBatch, kBatch);
    if (n <= 0) return;
    for (dds_return_t i = 0; i < n; ++i) {
      if (!si[i].valid_data) continue;
      last_object_stamp_ = buf[i]->stamp_ns;
      last_object_count_ = buf[i]->object_num;
      last_object_recv_ns_ = now;
    }
    dds_return_loan(rd_object_, reinterpret_cast<void**>(buf), n);
  }

  void publish_planning(int64_t now)
  {
    if (due("obstacle_list", rate("planning", "rt/perception/obstacle_list", 30.0), now)) {
      uint32_t seq = ++seq_["obstacle_list"];
      int64_t source_stamp =
          (last_object_stamp_ > 0 && now - last_object_recv_ns_ < 1000000000LL) ? last_object_stamp_
                                                                                : 0;
      obstacle_msg_.seq = seq;
      obstacle_msg_.stamp_ns = now;
      obstacle_msg_.source_stamp_ns = source_stamp;
      size_t count = last_object_count_ > 0 ? last_object_count_ : 8;
      if (count > obstacles_._maximum) count = obstacles_._maximum;
      obstacle_msg_.object_num = static_cast<uint32_t>(count);
      for (size_t i = 0; i < count; ++i) {
        ddsmon_planning_Obstacle& o = obstacles_._buffer[i];
        o.id = static_cast<uint32_t>(i + 1);
        o.x = 10.0 + 3.0 * static_cast<double>(i);
        o.y = -2.0 + 0.7 * static_cast<double>(i);
        o.speed = 8.0 + static_cast<double>(i);
        o.confidence = 0.9;
      }
      dds_write_ts(w_obstacle_, &obstacle_msg_, now);
    }

    if (due("trajectory", rate("planning", "rt/planning/trajectory", 10.0), now)) {
      uint32_t seq = ++seq_["trajectory"];
      // stall：轨迹话题「有发布者但不出数」，用于验证 STALE / RATE_LOW 判定
      if (!faults_.stall) {
        traj_msg_.seq = seq;
        traj_msg_.stamp_ns = now;
        traj_msg_.plan_id = 1000 + seq;
        for (size_t i = 0; i < 40; ++i) {
          ddsmon_planning_TrajectoryPoint& p = traj_points_._buffer[i];
          double t = static_cast<double>(i) * 0.1;
          p.x = 30.0 * t;
          p.y = 2.5 * std::sin(t * 0.4);
          p.theta = 0.4 * std::cos(t * 0.4);
          p.velocity = 15.0;
          p.acceleration = 0.0;
          p.t = t;
        }
        dds_write_ts(w_trajectory_, &traj_msg_, now);
      }
    }

    if (due("control_cmd", rate("planning", "rt/planning/control_cmd", 50.0), now)) {
      uint32_t seq = ++seq_["control_cmd"];
      ddsmon_planning_ControlCommand m;
      std::memset(&m, 0, sizeof(m));
      m.seq = seq;
      m.stamp_ns = now;
      double phase = static_cast<double>(now % (10LL * 1000 * 1000 * 1000)) / 1e9;
      m.steering = 0.15 * std::sin(phase * 0.6);
      m.throttle = 0.35;
      m.brake = 0.0;
      m.gear = 3;
      m.target_speed = 15.0;
      dds_write_ts(w_control_, &m, now);

      if (w_bad_type_ > 0) {
        ddsmon_planning_Trajectory bad;
        std::memset(&bad, 0, sizeof(bad));
        bad.seq = seq;
        bad.stamp_ns = now;
        bad.plan_id = 999;
        dds_write_ts(w_bad_type_, &bad, now);
      }
    }

    if (due("vehicle_state", rate("planning", "rt/planning/vehicle_state", 100.0), now)) {
      uint32_t seq = ++seq_["vehicle_state"];
      ddsmon_planning_VehicleState m;
      std::memset(&m, 0, sizeof(m));
      m.seq = seq;
      m.stamp_ns = now;
      m.speed = 14.8 + 0.3 * std::sin(static_cast<double>(seq) * 0.02);
      m.yaw_rate = 0.02;
      m.steering = 0.1;
      m.accel = 0.05;
      m.gear = 3;
      dds_write_ts(w_vehicle_, &m, now);
    }
  }

  // ------------------------------------------------------------ 监控诊断域
  bool init_diag(int domain_id)
  {
    pp_diag_ = dds_create_participant(static_cast<dds_domainid_t>(domain_id), nullptr, nullptr);
    if (pp_diag_ < 0) return false;
    const auto* hb = ddsmon::find_adapter("Heartbeat");

    // 每个节点一个独立 writer：真实系统里三个节点是三个进程，各自一个 publication
    auto add_node = [&](const std::string& node_id, const std::string& domain) {
      HbWriter h;
      h.node_id = node_id;
      h.domain_id = domain_of(domain);
      h.writer = make_writer(pp_diag_, hb, cfg_.heartbeat_topic, true);
      hb_writers_.push_back(h);
    };
    if (run_perception_) add_node("perception_node", "perception");
    if (run_planning_) add_node("planning_node", "planning");
    if (run_monitor_) add_node("diag_node", "diag");

    if (run_monitor_) {
      const auto* rep = ddsmon::find_adapter("DiagReport");
      const auto* evt = ddsmon::find_adapter("DiagEvent");
      rd_report_ = make_reader(pp_diag_, rep, cfg_.report_topic, false);
      rd_event_ = make_reader(pp_diag_, evt, cfg_.event_topic, false);
    }
    return hb_writers_.empty() || hb_writers_.front().writer >= 0;
  }

  void publish_heartbeats(int64_t now)
  {
    for (auto& h : hb_writers_) {
      if (h.writer < 0) continue;
      if (!due("hb_" + h.node_id, 1.0, now)) continue;

      ddsmon_diag_Heartbeat m;
      std::memset(&m, 0, sizeof(m));
      m.node_id = const_cast<char*>(h.node_id.c_str());
      m.domain_id = h.domain_id;
      m.seq = ++seq_["hb_" + h.node_id];
      m.stamp_ns = now;
      m.state = 1;
      m.cpu_percent = 25.0 + static_cast<double>(m.seq % 20);
      m.mem_mb = 120.0 + static_cast<double>(m.seq % 20);
      m.loop_us = 10000;
      dds_write_ts(h.writer, &m, now);
    }
  }

  void poll_reports(int64_t now)
  {
    if (rd_report_ > 0) {
      ddsmon_diag_DiagReport* buf[4] = {nullptr};
      dds_sample_info_t si[4];
      dds_return_t n = dds_take(rd_report_, reinterpret_cast<void**>(buf), si, 4, 4);
      if (n > 0) {
        for (dds_return_t i = 0; i < n; ++i) {
          if (!si[i].valid_data) continue;
          std::cout << "[monitor_node] 收到诊断报告 "
                    << (buf[i]->report_id ? buf[i]->report_id : "")
                    << "  health=" << buf[i]->health_score
                    << "  issues=" << buf[i]->issue_total << "\n"
                    << std::flush;
        }
        dds_return_loan(rd_report_, reinterpret_cast<void**>(buf), n);
      }
    }

    if (rd_event_ > 0) {
      ddsmon_diag_DiagEvent* buf[4] = {nullptr};
      dds_sample_info_t si[4];
      dds_return_t n = dds_take(rd_event_, reinterpret_cast<void**>(buf), si, 4, 4);
      if (n > 0) {
        for (dds_return_t i = 0; i < n; ++i) {
          if (!si[i].valid_data) continue;
          std::cout << "[monitor_node] 收到诊断事件 " << (buf[i]->code ? buf[i]->code : "")
                    << " sev=" << buf[i]->severity << "  "
                    << (buf[i]->message ? buf[i]->message : "") << " (" 
                    << (buf[i]->domain ? buf[i]->domain : "") << "/"
                    << (buf[i]->topic ? buf[i]->topic : "") << ")\n"
                    << std::flush;
        }
        dds_return_loan(rd_event_, reinterpret_cast<void**>(buf), n);
      }
    }
  }

  // ------------------------------------------------------------ 工具函数
  int domain_of(const std::string& name) const
  {
    const ddsmon::DomainCfg* d = cfg_.domain_by_name(name);
    return d ? d->domain_id : -1;
  }

  double rate(const std::string& domain, const std::string& topic, double fallback) const
  {
    const ddsmon::DomainCfg* d = cfg_.domain_by_name(domain);
    if (d) {
      for (const auto& t : d->topics) {
        if (t.name == topic) return t.expect_hz * rate_scale_;
      }
    }
    return fallback * rate_scale_;
  }

  bool due(const std::string& key, double hz, int64_t now)
  {
    if (hz <= 0) return false;
    int64_t period = static_cast<int64_t>(1e9 / hz);
    int64_t& next = next_due_[key];
    if (next == 0) next = now;
    if (now < next) return false;
    next += period;
    if (next < now) next = now + period;
    return true;
  }

  ddsmon::Config cfg_;
  Faults faults_;
  double rate_scale_{1.0};
  double stale_ms_{300};

  bool run_perception_{false}, run_planning_{false}, run_monitor_{false};

  dds_entity_t pp_perception_{DDS_ENTITY_NIL}, pp_planning_{DDS_ENTITY_NIL},
      pp_diag_{DDS_ENTITY_NIL}, pp_bridge_{DDS_ENTITY_NIL}, pp_bad_{DDS_ENTITY_NIL};
  dds_entity_t w_camera_{DDS_ENTITY_NIL}, w_object_{DDS_ENTITY_NIL}, w_lidar_{DDS_ENTITY_NIL};
  dds_entity_t w_obstacle_{DDS_ENTITY_NIL}, w_trajectory_{DDS_ENTITY_NIL},
      w_control_{DDS_ENTITY_NIL}, w_vehicle_{DDS_ENTITY_NIL}, w_bad_type_{DDS_ENTITY_NIL};
  dds_entity_t rd_camera_{DDS_ENTITY_NIL}, rd_lidar_{DDS_ENTITY_NIL}, rd_obstacle_{DDS_ENTITY_NIL},
      rd_trajectory_{DDS_ENTITY_NIL}, rd_vehicle_{DDS_ENTITY_NIL};
  dds_entity_t rd_object_{DDS_ENTITY_NIL}, rd_control_{DDS_ENTITY_NIL}, rd_report_{DDS_ENTITY_NIL},
      rd_event_{DDS_ENTITY_NIL};

  struct HbWriter {
    std::string node_id;
    int domain_id;
    dds_entity_t writer{DDS_ENTITY_NIL};
  };
  std::vector<HbWriter> hb_writers_;

  dds_sequence_ddsmon_perception_Object objects_{0, 0, nullptr, false};
  ddsmon_perception_ObjectList obj_msg_{};
  dds_sequence_ddsmon_planning_Obstacle obstacles_{0, 0, nullptr, false};
  ddsmon_planning_ObstacleList obstacle_msg_{};
  dds_sequence_ddsmon_planning_TrajectoryPoint traj_points_{0, 0, nullptr, false};
  ddsmon_planning_Trajectory traj_msg_{};

  int64_t last_object_stamp_{0};
  int64_t last_object_recv_ns_{0};
  uint32_t last_object_count_{0};

  std::map<std::string, uint32_t> seq_;
  std::map<std::string, int64_t> next_due_;
};

bool parse_faults(const std::string& text, Faults* f)
{
  if (text == "all") {
    f->loss = f->stale = f->stall = f->qos = f->type = f->kill = true;
    return true;
  }
  for (const auto& part : ddsmon::split(text, ',')) {
    std::string p = ddsmon::to_lower(ddsmon::trim(part));
    if (p.empty() || p == "none") continue;
    if (p == "loss") f->loss = true;
    else if (p == "stale") f->stale = true;
    else if (p == "stall") f->stall = true;
    else if (p == "qos") f->qos = true;
    else if (p == "type") f->type = true;
    else if (p == "kill") f->kill = true;
    else {
      std::cerr << "未知故障类型: " << p << "\n";
      return false;
    }
  }
  return true;
}

}  // namespace

int main(int argc, char** argv)
{
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto next = [&]() -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "缺少参数值: " << a << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (a == "--node" || a == "-n") args.nodes = next();
    else if (a == "-c" || a == "--config") args.config = next();
    else if (a == "--fault" || a == "-f") args.faults = next();
    else if (a == "--duration" || a == "-d") args.duration_sec = std::stod(next());
    else if (a == "--rate-scale") args.rate_scale = std::stod(next());
    else if (a == "--stale-ms") args.stale_ms = std::stod(next());
    else if (a == "-h" || a == "--help") {
      std::cout <<
          "ddsmon-sim —— 三域模拟节点（含故障注入）\n"
          "  --node perception|planning|monitor|all   启动哪些节点，默认 all\n"
          "  -c, --config <path>                       配置文件（域 id / 频率）\n"
          "  -f, --fault none|loss|stale|stall|qos|type|kill|all[,..]\n"
          "  -d, --duration <sec>                      运行时长，默认一直运行\n"
          "      --rate-scale <k>                      频率倍率\n"
          "      --stale-ms <ms>                       stale 故障回退时间戳\n";
      return 0;
    } else {
      std::cerr << "未知参数: " << a << "\n";
      return 2;
    }
  }

  ddsmon::Config cfg;
  std::string err;
  if (!ddsmon::Config::load(args.config, &cfg, &err)) {
    std::cerr << "配置加载失败: " << err << "\n";
    return 2;
  }

  Faults faults;
  if (!parse_faults(args.faults, &faults)) return 2;

  Simulator sim;
  if (!sim.init(cfg, args.nodes, faults, args.rate_scale, args.stale_ms)) return 2;

  std::cout << "ddsmon-sim 启动: nodes=" << args.nodes << " faults=" << args.faults
            << " duration=" << args.duration_sec << "s\n"
            << std::flush;
  sim.run(args.duration_sec);
  std::cout << "ddsmon-sim 退出\n";
  return 0;
}
