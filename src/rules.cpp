#include "ddsmon/rules.hpp"

#include <algorithm>
#include <map>
#include <sstream>

#include "ddsmon/engine.hpp"
#include "ddsmon/util.hpp"

namespace ddsmon {

std::string issue_key(const Issue& issue)
{
  return issue.code + "|" + issue.domain + "|" + issue.topic;
}

void RulesEngine::apply(Snapshot& snap, const Config& cfg, int64_t now, const Issue& issue,
                        bool condition)
{
  std::string key = issue_key(issue);
  if (!condition) {
    active_.erase(key);
    return;
  }
  State& st = active_[key];
  if (st.streak == 0) st.since_ns = now;
  st.streak += 1;
  st.issue = issue;

  int need = cfg.rules.debounce_windows > 0 ? cfg.rules.debounce_windows : 1;
  if (st.streak >= need) snap.issues.push_back(issue);
}

void RulesEngine::evaluate(Snapshot& snap, const Config& cfg, int64_t now)
{
  if (started_ns_ == 0) started_ns_ = now;
  double uptime_sec = static_cast<double>(now - started_ns_) / 1e9;
  const RuleCfg& r = cfg.rules;

  auto make = [](const std::string& code, Severity sev, const std::string& domain,
                 const std::string& topic, const std::string& detail, double value,
                 double threshold) {
    Issue i;
    i.code = code;
    i.severity = sev;
    i.domain = domain;
    i.topic = topic;
    i.detail = detail;
    i.value = value;
    i.threshold = threshold;
    return i;
  };

  // ---------------- 域级：域内没有任何参与者
  for (const auto& d : snap.domains) {
    bool cond = d.participants == 0 && uptime_sec > r.no_publisher_sec;
    apply(snap, cfg, now,
          make("DDS.DOMAIN.NO_PARTICIPANT", Severity::Error, d.name, "",
               "域 " + d.name + "(domain " + std::to_string(d.domain_id) + ") 内未发现任何 DDS 参与者",
               0, 1),
          cond);
  }

  // ---------------- 话题级
  for (const auto& t : snap.topics) {
    const double period_ms = 1000.0 / (t.expect_hz > 0 ? t.expect_hz : 1.0);
    bool started = uptime_sec > r.no_publisher_sec;

    // 1) 没有发布者
    apply(snap, cfg, now,
          make("DDS.TOPIC.NO_PUBLISHER", t.critical ? Severity::Error : Severity::Warn, t.domain,
               t.topic, "话题无发布者（期望 " + fmt_double(t.expect_hz, 1) + "Hz 数据流）",
               t.publishers, 1),
          started && t.publishers == 0);

    // 2) 没有订阅者
    if (t.consumer_required) {
      apply(snap, cfg, now,
            make("DDS.TOPIC.NO_SUBSCRIBER", t.critical ? Severity::Error : Severity::Warn, t.domain,
                 t.topic, "话题无订阅者，下游消费链路缺失", t.subscribers, 1),
            started && t.publishers > 0 && t.subscribers == 0);
    }

    // 3) 频率不足（含「有发布者但完全不出数」，rate=0 也要报）
    if (t.publishers > 0) {
      double low = t.expect_hz * r.rate_low_ratio;
      double crit = t.expect_hz * r.rate_critical_ratio;
      Severity sev = t.metrics.rate_hz < crit ? Severity::Error : Severity::Warn;
      apply(snap, cfg, now,
            make("DDS.TOPIC.RATE_LOW", sev, t.domain, t.topic,
                 "实测频率 " + fmt_double(t.metrics.rate_hz, 1) + "Hz 低于期望 " +
                     fmt_double(t.expect_hz, 1) + "Hz",
                 t.metrics.rate_hz, low),
            started && t.metrics.rate_hz < low);
    }

    // 4) 抖动
    apply(snap, cfg, now,
          make("DDS.TOPIC.JITTER_HIGH", Severity::Warn, t.domain, t.topic,
               "到达间隔抖动 p99 " + fmt_ms(t.metrics.jitter_ms) + "，超过周期 " +
                   fmt_ms(period_ms) + " 的 " + fmt_double(r.jitter_warn_ratio, 2) + " 倍",
               t.metrics.jitter_ms, period_ms * r.jitter_warn_ratio),
          t.metrics.jitter_ms > period_ms * r.jitter_warn_ratio && t.metrics.samples_in_window > 5);

    // 5) 端到端时延
    if (t.metrics.latency_p99_ms > 0) {
      Severity sev = t.metrics.latency_p99_ms > r.latency_error_ms ? Severity::Error
                                                                   : Severity::Warn;
      apply(snap, cfg, now,
            make("DDS.TOPIC.LATENCY_HIGH", sev, t.domain, t.topic,
                 "端到端时延 p99 " + fmt_ms(t.metrics.latency_p99_ms) + "（阈值 " +
                     fmt_ms(r.latency_warn_ms) + "）",
                 t.metrics.latency_p99_ms, r.latency_warn_ms),
            t.metrics.latency_p99_ms > r.latency_warn_ms);
    }

    // 6) 丢包（序号空洞）
    if (t.metrics.expected_samples > 100) {
      Severity sev = t.metrics.loss_ratio > r.loss_error ? Severity::Error : Severity::Warn;
      apply(snap, cfg, now,
            make("DDS.TOPIC.SAMPLE_LOSS", sev, t.domain, t.topic,
                 "样本丢失 " + std::to_string(t.metrics.lost_samples) + "/" +
                     std::to_string(t.metrics.expected_samples) + "（" +
                     fmt_double(t.metrics.loss_ratio * 100.0, 2) + "%）",
                 t.metrics.loss_ratio * 100.0, r.loss_warn * 100.0),
            t.metrics.loss_ratio > r.loss_warn);
    }

    // 7) 数据过期（发布者还在，但没有新样本；从未收到样本同样算问题）
    if (t.publishers > 0) {
      double limit_ms = period_ms * r.stale_warn_periods;
      bool no_data = t.metrics.last_recv_ns == 0;
      Severity sev = (no_data || t.metrics.stale_ms > limit_ms * 2) ? Severity::Error
                                                                   : Severity::Warn;
      std::string detail = no_data
                               ? "发布者已上线但从未收到该话题样本"
                               : "距离最近一帧已 " + fmt_ms(t.metrics.stale_ms) + "（超 " +
                                     fmt_double(r.stale_warn_periods, 1) + " 个周期）";
      apply(snap, cfg, now,
            make("DDS.TOPIC.STALE", sev, t.domain, t.topic, detail, t.metrics.stale_ms, limit_ms),
            started && (no_data || t.metrics.stale_ms > limit_ms));
    }

    // 8) 类型冲突
    if (t.types_seen.size() > 1) {
      std::string joined;
      for (size_t i = 0; i < t.types_seen.size(); ++i) {
        if (i) joined += " / ";
        joined += t.types_seen[i];
      }
      apply(snap, cfg, now,
            make("DDS.TOPIC.TYPE_MISMATCH", Severity::Critical, t.domain, t.topic,
                 "同一话题上出现多个类型：" + joined + "，端点无法互通", t.types_seen.size(), 1),
            true);
    } else if (t.types_seen.size() == 1) {
      const PayloadAdapter* expect = find_adapter(t.type);
      if (expect && t.types_seen[0] != expect->dds_type_name &&
          !t.types_seen[0].empty()) {
        apply(snap, cfg, now,
              make("DDS.TOPIC.TYPE_MISMATCH", Severity::Critical, t.domain, t.topic,
                   "话题实际类型 " + t.types_seen[0] + " 与配置期望 " + expect->dds_type_name +
                       " 不一致",
                   1, 1),
              true);
      }
    }

    // 9) 跨域链路时延
    for (const auto& c : snap.chains) {
      if (c.domain != t.domain || c.topic != t.topic) continue;
      Severity sev = c.p99_ms > r.chain_error_ms ? Severity::Error : Severity::Warn;
      apply(snap, cfg, now,
            make("DDS.CHAIN.LATENCY_HIGH", sev, t.domain, t.topic,
                 c.name + " 链路时延 p99 " + fmt_ms(c.p99_ms) + "（阈值 " +
                     fmt_ms(r.chain_warn_ms) + "）",
                 c.p99_ms, r.chain_warn_ms),
            c.p99_ms > r.chain_warn_ms);
    }
  }

  // ---------------- QoS 兼容性（按 topic+policy 聚合）
  {
    struct Agg {
      int count{0};
      std::string detail;
      std::string suggestion;
    };
    std::map<std::string, Agg> agg;
    for (const auto& q : snap.qos_checks) {
      std::string key = q.domain + "|" + q.topic + "|" + q.policy;
      auto& a = agg[key];
      a.count += 1;
      if (a.detail.empty()) {
        a.detail = q.detail;
        a.suggestion = q.suggestion;
      }
    }
    for (const auto& kv : agg) {
      size_t p1 = kv.first.find('|');
      size_t p2 = kv.first.find('|', p1 + 1);
      std::string domain = kv.first.substr(0, p1);
      std::string topic = kv.first.substr(p1 + 1, p2 - p1 - 1);
      std::string policy = kv.first.substr(p2 + 1);
      Severity sev = policy == "LIVELINESS" || policy == "OWNERSHIP" ? Severity::Warn
                                                                     : Severity::Error;
      Issue issue = make("DDS.QOS." + policy + "_INCOMPATIBLE", sev, domain, topic,
                         kv.second.detail + "（" + std::to_string(kv.second.count) +
                             " 对端点不兼容；建议：" + kv.second.suggestion + "）",
                         kv.second.count, 1);
      apply(snap, cfg, now, issue, true);
    }
  }

  // ---------------- 节点心跳
  for (const auto& n : snap.nodes) {
    auto cfg_it = std::find_if(cfg.nodes.begin(), cfg.nodes.end(),
                               [&](const NodeCfg& c) { return c.node_id == n.node_id; });
    double lease = cfg_it != cfg.nodes.end() ? cfg_it->lease_sec : r.heartbeat_lease_sec;

    if (!n.alive) {
      std::string detail = n.last_seen_ms == 0
                               ? "节点从未上报心跳"
                               : "节点心跳中断超过 " + fmt_double(lease, 1) + "s";
      apply(snap, cfg, now,
            make("DDS.NODE.HEARTBEAT_LOST", Severity::Error, n.domain, "rt/diag/heartbeat",
                 "节点 " + n.node_id + " " + detail, 0, lease),
            uptime_sec > lease);
    }
    if (n.alive && n.state == 2) {
      apply(snap, cfg, now,
            make("DDS.NODE.DEGRADED", Severity::Warn, n.domain, "rt/diag/heartbeat",
                 "节点 " + n.node_id + " 自报降级状态 (DEGRADED)", n.state, 1),
            true);
    }
    if (n.alive && n.state >= 3) {
      apply(snap, cfg, now,
            make("DDS.NODE.FAULT", Severity::Error, n.domain, "rt/diag/heartbeat",
                 "节点 " + n.node_id + " 自报故障状态 (FAULT)", n.state, 2),
            true);
    }
  }

  std::sort(snap.issues.begin(), snap.issues.end(), [](const Issue& a, const Issue& b) {
    if (static_cast<int>(a.severity) != static_cast<int>(b.severity))
      return static_cast<int>(a.severity) > static_cast<int>(b.severity);
    if (a.domain != b.domain) return a.domain < b.domain;
    return a.topic < b.topic;
  });
}

}  // namespace ddsmon
