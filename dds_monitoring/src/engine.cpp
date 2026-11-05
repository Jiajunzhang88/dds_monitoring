#include "ddsmon/engine.hpp"

#include <functional>
#include <map>
#include <memory>

#include "ddsmon/util.hpp"
#include "ddsmon.h"

namespace ddsmon {

namespace {

int health_deduction(Severity s)
{
  switch (s) {
    case Severity::Critical: return 40;
    case Severity::Error: return 15;
    case Severity::Warn: return 5;
    case Severity::Info: return 1;
  }
  return 0;
}

}  // namespace

struct Engine::DomainRuntime {
  DomainCfg cfg;
  dds_entity_t participant{DDS_ENTITY_NIL};
  dds_entity_t rd_participant{DDS_ENTITY_NIL};
  dds_entity_t rd_publication{DDS_ENTITY_NIL};
  dds_entity_t rd_subscription{DDS_ENTITY_NIL};
  dds_entity_t hb_topic{DDS_ENTITY_NIL};
  dds_entity_t hb_reader{DDS_ENTITY_NIL};
  std::string own_guid;
  Topology topo;
  std::vector<std::unique_ptr<TopicMonitor>> monitors;

  bool init(const DomainCfg& c, std::string* err)
  {
    cfg = c;
    participant = dds_create_participant(static_cast<dds_domainid_t>(cfg.domain_id), nullptr, nullptr);
    if (participant < 0) {
      *err = "create participant for domain " + cfg.name + " failed: " + dds_strretcode(participant);
      return false;
    }

    rd_participant = dds_create_reader(participant, DDS_BUILTIN_TOPIC_DCPSPARTICIPANT, nullptr, nullptr);
    rd_publication = dds_create_reader(participant, DDS_BUILTIN_TOPIC_DCPSPUBLICATION, nullptr, nullptr);
    rd_subscription = dds_create_reader(participant, DDS_BUILTIN_TOPIC_DCPSSUBSCRIPTION, nullptr, nullptr);
    if (rd_participant < 0 || rd_publication < 0 || rd_subscription < 0) {
      *err = "create builtin readers for domain " + cfg.name + " failed";
      return false;
    }

    dds_guid_t guid;
    if (dds_get_guid(participant, &guid) == DDS_RETCODE_OK) {
      own_guid = guid_str(guid.v);
    }

    for (const auto& tc : cfg.topics) {
      auto mon = std::make_unique<TopicMonitor>();
      if (!mon->init(participant, tc, err)) return false;
      monitors.push_back(std::move(mon));

      if (tc.type == "Heartbeat" && hb_reader == DDS_ENTITY_NIL) {
        const PayloadAdapter* ad = find_adapter("Heartbeat");
        hb_topic = dds_create_topic(participant, ad->desc, tc.name.c_str(), nullptr, nullptr);
        if (hb_topic < 0) {
          *err = "create heartbeat topic failed: " + std::string(dds_strretcode(hb_topic));
          return false;
        }
        dds_qos_t* qos = dds_create_qos();
        dds_qset_reliability(qos, DDS_RELIABILITY_BEST_EFFORT, DDS_SECS(1));
        dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 32);
        hb_reader = dds_create_reader(participant, hb_topic, qos, nullptr);
        dds_delete_qos(qos);
        if (hb_reader < 0) {
          *err = "create heartbeat reader failed: " + std::string(dds_strretcode(hb_reader));
          return false;
        }
      }
    }
    return true;
  }

  ~DomainRuntime()
  {
    monitors.clear();
    if (hb_reader > 0) dds_delete(hb_reader);
    if (hb_topic > 0) dds_delete(hb_topic);
    if (rd_participant > 0) dds_delete(rd_participant);
    if (rd_publication > 0) dds_delete(rd_publication);
    if (rd_subscription > 0) dds_delete(rd_subscription);
    if (participant > 0) dds_delete(participant);
  }

  int own_readers() const
  {
    int n = static_cast<int>(monitors.size());
    if (hb_reader != DDS_ENTITY_NIL) n += 1;
    n += 3;  // 三个内置主题 reader（participant / publication / subscription）同样算本域读者
    return n;
  }
};

Engine::Engine() = default;
Engine::~Engine() { domains_.clear(); }

bool Engine::init(const Config& cfg, std::string* err)
{
  cfg_ = cfg;
  std::string local_err;
  for (const auto& dc : cfg_.domains) {
    auto rt = std::make_unique<DomainRuntime>();
    if (!rt->init(dc, &local_err)) {
      if (err) *err = local_err;
      return false;
    }
    domains_.push_back(std::move(rt));
  }
  inited_ = true;
  int64_t now = now_ns();
  build_snapshot(now);
  last_eval_ns_ = now;
  last_topo_ns_ = now;
  return true;
}

void Engine::tick()
{
  if (!inited_) return;
  int64_t now = now_ns();
  bool poll_topology = (now - last_topo_ns_) >= 200LL * 1000 * 1000;
  poll_domains(now, poll_topology);
  if (poll_topology) last_topo_ns_ = now;

  int64_t interval = static_cast<int64_t>(cfg_.rules.eval_interval_ms) * 1000 * 1000;
  if (now - last_eval_ns_ >= interval) {
    build_snapshot(now);
    last_eval_ns_ = now;
  }
}

void Engine::refresh()
{
  if (!inited_) return;
  int64_t now = now_ns();
  poll_domains(now, true);
  build_snapshot(now);
  last_eval_ns_ = now;
  last_topo_ns_ = now;
}

namespace {

void drain_builtin(dds_entity_t reader, int64_t now,
                   const std::function<void(dds_sample_info_t&, const void*)>& on_participant,
                   const std::function<void(dds_sample_info_t&, const void*)>& on_writer,
                   const std::function<void(dds_sample_info_t&, const void*)>& on_reader)
{
  constexpr int kBatch = 64;
  for (int round = 0; round < 8; ++round) {
    void* buf[kBatch] = {nullptr};
    dds_sample_info_t si[kBatch];
    dds_return_t n = dds_take(reader, buf, si, kBatch, kBatch);
    if (n <= 0) break;
    for (dds_return_t i = 0; i < n; ++i) {
      dds_sample_info_t info = si[i];
      if (on_participant) on_participant(info, buf[i]);
      if (on_writer) on_writer(info, buf[i]);
      if (on_reader) on_reader(info, buf[i]);
    }
    dds_return_loan(reader, buf, n);
    if (n < kBatch) break;
  }
}

}  // namespace

void Engine::poll_domains(int64_t now, bool poll_topology)
{
  for (auto& rt : domains_) {
    for (auto& m : rt->monitors) m->poll(now);

    // 心跳：更新节点级信息
    if (rt->hb_reader > 0) {
      constexpr int kBatch = 32;
      for (int round = 0; round < 8; ++round) {
        ddsmon_diag_Heartbeat* buf[kBatch] = {nullptr};
        dds_sample_info_t si[kBatch];
        dds_return_t n = dds_take(rt->hb_reader, reinterpret_cast<void**>(buf), si, kBatch, kBatch);
        if (n <= 0) break;
        for (dds_return_t i = 0; i < n; ++i) {
          if (!si[i].valid_data || buf[i]->node_id == nullptr) continue;
          NodeRow row;
          row.node_id = buf[i]->node_id;
          row.domain = cfg_.diag_domain;
          for (const auto& dc : cfg_.domains) {
            if (dc.domain_id == buf[i]->domain_id) row.domain = dc.name;
          }
          row.state = static_cast<int>(buf[i]->state);
          row.alive = true;
          row.cpu = buf[i]->cpu_percent;
          row.mem = buf[i]->mem_mb;
          row.last_seen_ms = now / 1000000LL;
          row.seq = buf[i]->seq;
          nodes_[row.node_id] = row;
        }
        dds_return_loan(rt->hb_reader, reinterpret_cast<void**>(buf), n);
        if (n < kBatch) break;
      }
    }

    if (!poll_topology) continue;

    auto on_part = [&](dds_sample_info_t& si, const void* p) {
      rt->topo.on_participant(*static_cast<const dds_builtintopic_participant_t*>(p), si, now);
    };
    drain_builtin(rt->rd_participant, now, on_part, {}, {});

    auto on_writer = [&](dds_sample_info_t& si, const void* p) {
      rt->topo.on_endpoint(*static_cast<const dds_builtintopic_endpoint_t*>(p), si, now, true);
    };
    drain_builtin(rt->rd_publication, now, {}, on_writer, {});

    auto on_reader = [&](dds_sample_info_t& si, const void* p) {
      rt->topo.on_endpoint(*static_cast<const dds_builtintopic_endpoint_t*>(p), si, now, false);
    };
    drain_builtin(rt->rd_subscription, now, {}, {}, on_reader);
  }
}

void Engine::build_snapshot(int64_t now)
{
  Snapshot snap;
  snap.at_ns = now;

  for (auto& rt : domains_) {
    DomainInfo di;
    di.name = rt->cfg.name;
    di.domain_id = rt->cfg.domain_id;
    di.participants = rt->topo.participants_alive();
    if (!rt->own_guid.empty() && di.participants > 0) di.participants -= 1;  // 扣掉诊断工具自身
    di.writers = rt->topo.writers_alive();
    di.readers = rt->topo.readers_alive();
    if (di.readers > 0) di.readers -= rt->own_readers();
    if (di.readers < 0) di.readers = 0;

    for (const auto& m : rt->monitors) {
      const TopicCfg& tc = m->cfg();
      TopicRow row;
      row.domain = rt->cfg.name;
      row.domain_id = rt->cfg.domain_id;
      row.topic = tc.name;
      row.type = tc.type;
      row.expect_hz = tc.expect_hz;
      row.consumer_required = tc.consumer_required;
      row.critical = tc.critical;
      row.reader_ok = m->ok();
      // 统计发布者/订阅者时排除诊断工具自身的 reader：否则每个话题都会多算一个订阅者，
      // 而像 Heartbeat 这种另有专用 reader 的话题还会多算多个。
      int pubs = 0;
      int subs = 0;
      for (const auto& w : rt->topo.writers()) {
        if (w.alive && w.topic == tc.name && w.participant_guid != rt->own_guid) ++pubs;
      }
      for (const auto& r : rt->topo.readers()) {
        if (r.alive && r.topic == tc.name && r.participant_guid != rt->own_guid) ++subs;
      }
      row.publishers = pubs;
      row.subscribers = subs;
      row.metrics = m->metrics(now);

      for (const auto& t : rt->topo.types_on(tc.name)) row.types_seen.push_back(t);
      snap.topics.push_back(row);

      if (row.metrics.samples_in_window > 0 || row.publishers > 0) di.active_topics += 1;

      if (row.metrics.chain_p50_ms > 0 || row.metrics.chain_p99_ms > 0) {
        ChainRow chain;
        chain.name = rt->cfg.name == "planning" ? "感知 → 规控" : rt->cfg.name;
        chain.domain = rt->cfg.name;
        chain.topic = tc.name;
        chain.p50_ms = row.metrics.chain_p50_ms;
        chain.p99_ms = row.metrics.chain_p99_ms;
        chain.samples = row.metrics.samples_in_window;
        snap.chains.push_back(chain);
      }
    }
    di.declared_topics = static_cast<int>(rt->cfg.topics.size());
    snap.domains.push_back(di);

    // ---- QoS 兼容性：同话题下每个 writer × reader
    for (const auto& w : rt->topo.writers()) {
      if (!w.alive) continue;
      if (!rt->own_guid.empty() && w.participant_guid == rt->own_guid) continue;
      for (const auto& r : rt->topo.readers()) {
        if (!r.alive || r.topic != w.topic) continue;
        if (!rt->own_guid.empty() && r.participant_guid == rt->own_guid) continue;
        for (const auto& v : check_qos_compat(w.qos, r.qos)) {
          QosCheckRow row;
          row.domain = rt->cfg.name;
          row.topic = w.topic;
          row.policy = v.policy;
          row.writer = w.guid;
          row.reader = r.guid;
          row.detail = v.detail;
          row.suggestion = v.suggestion;
          snap.qos_checks.push_back(row);
        }
      }
    }
  }

  // ---- 节点存活
  for (const auto& nc : cfg_.nodes) {
    auto it = nodes_.find(nc.node_id);
    NodeRow row;
    if (it != nodes_.end()) {
      row = it->second;
      int64_t silent_ms = now / 1000000LL - row.last_seen_ms;
      double lease_ms = nc.lease_sec * 1000.0;
      row.alive = silent_ms <= lease_ms;
    } else {
      row.node_id = nc.node_id;
      row.domain = nc.domain;
      row.state = -1;
      row.alive = false;
      row.last_seen_ms = 0;
    }
    snap.nodes.push_back(row);
  }
  // 配置外但实际出现的节点也纳入报告
  for (const auto& kv : nodes_) {
    bool listed = false;
    for (const auto& nc : cfg_.nodes) {
      if (nc.node_id == kv.first) { listed = true; break; }
    }
    if (!listed) snap.nodes.push_back(kv.second);
  }

  if (snap.nodes.size() > 0) {
    // nothing
  }

  rules_.evaluate(snap, cfg_, now);

  // ---- 健康分
  int overall = 100;
  for (const auto& iss : snap.issues) {
    overall -= health_deduction(iss.severity);
  }
  snap.health = overall < 0 ? 0 : overall;

  for (auto& d : snap.domains) {
    int score = 100;
    for (const auto& iss : snap.issues) {
      if (iss.domain == d.name) score -= health_deduction(iss.severity);
    }
    d.health = score < 0 ? 0 : score;
  }

  snapshot_ = std::move(snap);
}

}  // namespace ddsmon
