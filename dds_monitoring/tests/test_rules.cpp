#include <algorithm>
#include <cassert>
#include <iostream>

#include "ddsmon/engine.hpp"
#include "ddsmon/rules.hpp"

using namespace ddsmon;

namespace {

bool has_issue(const Snapshot& s, const std::string& code)
{
  for (const auto& i : s.issues) {
    if (i.code == code) return true;
  }
  return false;
}

TopicRow healthy_topic(int64_t now)
{
  TopicRow t;
  t.domain = "perception";
  t.domain_id = 10;
  t.topic = "rt/perception/object_list";
  t.type = "ObjectList";
  t.publishers = 1;
  t.subscribers = 1;
  t.expect_hz = 30.0;
  t.consumer_required = true;
  t.metrics.rate_hz = 30.0;
  t.metrics.samples_in_window = 240;
  t.metrics.latency_p99_ms = 5.0;
  t.metrics.last_recv_ns = now - 2000000LL;
  t.metrics.stale_ms = 2.0;
  return t;
}

}  // namespace

int main()
{
  Config cfg = Config::defaults();
  cfg.rules.debounce_windows = 1;

  RulesEngine rules;
  int64_t t0 = 1000000000LL;

  // --- 健康场景：不应产生任何问题
  {
    Snapshot snap;
    snap.at_ns = t0;
    DomainInfo d;
    d.name = "perception";
    d.domain_id = 10;
    d.participants = 2;
    d.writers = 1;
    d.readers = 1;
    snap.domains.push_back(d);
    snap.topics.push_back(healthy_topic(t0));
    rules.evaluate(snap, cfg, t0);
    assert(snap.issues.empty());
  }

  // --- 无发布者 + 无订阅者 + 类型冲突（超过 3s 宽限期后）
  {
    Snapshot snap;
    int64_t t1 = t0 + 5000000000LL;
    snap.at_ns = t1;
    DomainInfo d;
    d.name = "perception";
    d.domain_id = 10;
    d.participants = 2;
    d.writers = 1;
    d.readers = 1;
    snap.domains.push_back(d);

    TopicRow row_a = healthy_topic(t1);
    row_a.topic = "rt/perception/camera_front";
    row_a.type = "CameraFrame";
    row_a.publishers = 0;
    row_a.subscribers = 1;
    row_a.metrics.rate_hz = 0;
    row_a.metrics.samples_in_window = 0;
    snap.topics.push_back(row_a);

    TopicRow row_b = healthy_topic(t1);
    row_b.publishers = 1;
    row_b.subscribers = 0;
    row_b.types_seen = {"ddsmon::perception::ObjectList", "ddsmon::planning::Trajectory"};
    snap.topics.push_back(row_b);

    rules.evaluate(snap, cfg, t1);
    assert(has_issue(snap, "DDS.TOPIC.NO_PUBLISHER"));
    assert(has_issue(snap, "DDS.TOPIC.NO_SUBSCRIBER"));
    assert(has_issue(snap, "DDS.TOPIC.TYPE_MISMATCH"));
  }

  // --- 频率不足 + 时延过高 + 数据过期
  {
    Snapshot snap;
    int64_t t2 = t0 + 6000000000LL;
    snap.at_ns = t2;
    DomainInfo d;
    d.name = "perception";
    d.domain_id = 10;
    d.participants = 2;
    d.writers = 1;
    d.readers = 1;
    snap.domains.push_back(d);

    TopicRow t = healthy_topic(t2);
    t.metrics.rate_hz = 5.0;                       // 30Hz 期望 → 严重不足
    t.metrics.latency_p99_ms = 150.0;              // 超过 error 阈值 100ms
    t.metrics.stale_ms = 200.0;                    // 超过 2 个周期
    t.metrics.last_recv_ns = t2 - 200000000LL;
    snap.topics.push_back(t);

    rules.evaluate(snap, cfg, t2);
    assert(has_issue(snap, "DDS.TOPIC.RATE_LOW"));
    assert(has_issue(snap, "DDS.TOPIC.LATENCY_HIGH"));
    assert(has_issue(snap, "DDS.TOPIC.STALE"));
  }

  // --- 发布者在线但完全不出数（rate=0、从未收到样本）也必须报出来
  {
    Snapshot snap;
    int64_t t_nodata = t0 + 6500000000LL;
    snap.at_ns = t_nodata;
    DomainInfo d;
    d.name = "planning";
    d.domain_id = 20;
    d.participants = 2;
    d.writers = 1;
    d.readers = 1;
    snap.domains.push_back(d);

    TopicRow t = healthy_topic(t_nodata);
    t.domain = "planning";
    t.domain_id = 20;
    t.topic = "rt/planning/trajectory";
    t.type = "Trajectory";
    t.expect_hz = 10.0;
    t.metrics.rate_hz = 0.0;
    t.metrics.samples_in_window = 0;
    t.metrics.last_recv_ns = 0;
    t.metrics.stale_ms = 0;
    snap.topics.push_back(t);

    rules.evaluate(snap, cfg, t_nodata);
    assert(has_issue(snap, "DDS.TOPIC.RATE_LOW"));
    assert(has_issue(snap, "DDS.TOPIC.STALE"));
  }

  // --- QoS 冲突（聚合后应只有一条 issue）
  {
    Snapshot snap;
    int64_t t3 = t0 + 7000000000LL;
    snap.at_ns = t3;
    DomainInfo d;
    d.name = "planning";
    d.domain_id = 20;
    d.participants = 2;
    d.writers = 1;
    d.readers = 1;
    snap.domains.push_back(d);
    TopicRow t = healthy_topic(t3);
    t.domain = "planning";
    t.domain_id = 20;
    t.topic = "rt/planning/control_cmd";
    t.type = "ControlCommand";
    snap.topics.push_back(t);

    QosCheckRow q;
    q.domain = "planning";
    q.topic = "rt/planning/control_cmd";
    q.policy = "RELIABILITY";
    q.detail = "订阅者请求 RELIABLE，发布者仅提供 BEST_EFFORT";
    q.suggestion = "发布者改为 RELIABLE";
    snap.qos_checks.push_back(q);
    snap.qos_checks.push_back(q);

    RulesEngine r2;
    r2.evaluate(snap, cfg, t3);
    int count = 0;
    for (const auto& i : snap.issues) {
      if (i.code == "DDS.QOS.RELIABILITY_INCOMPATIBLE") ++count;
    }
    assert(count == 1);
    assert(snap.issues[0].severity == Severity::Error);
  }

  // --- 节点心跳丢失
  {
    Snapshot snap;
    int64_t t4 = t0 + 8000000000LL;
    snap.at_ns = t4;
    DomainInfo d;
    d.name = "diag";
    d.domain_id = 30;
    d.participants = 1;
    snap.domains.push_back(d);

    NodeRow n;
    n.node_id = "perception_node";
    n.domain = "perception";
    n.alive = false;
    n.last_seen_ms = (t4 / 1000000LL) - 10000;
    snap.nodes.push_back(n);

    RulesEngine r3;
    r3.evaluate(snap, cfg, t4);
    // 心跳丢失需要超过 lease(3s) 才判定，第二次评估在 3s 之后
    int64_t t5 = t4 + 4000000000LL;
    snap.at_ns = t5;
    snap.issues.clear();
    r3.evaluate(snap, cfg, t5);
    assert(has_issue(snap, "DDS.NODE.HEARTBEAT_LOST"));
  }

  std::cout << "test_rules ok\n";
  return 0;
}
