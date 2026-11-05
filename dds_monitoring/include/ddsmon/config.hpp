#pragma once

#include <string>
#include <vector>

namespace ddsmon {

struct TopicCfg {
  std::string name;                 ///< DDS 话题名，如 rt/perception/object_list
  std::string type;                 ///< 短类型名，如 ObjectList
  double expect_hz{10.0};           ///< 期望频率
  bool consumer_required{true};     ///< 是否必须有订阅者（有下游消费者）
  bool critical{true};              ///< 出问题是否计为关键链路
};

struct DomainCfg {
  std::string name;                 ///< perception / planning / diag
  int domain_id{0};                 ///< DDS domain id
  std::vector<TopicCfg> topics;
};

struct RuleCfg {
  double rate_low_ratio{0.7};       ///< 实测 < 期望*ratio → 频率不足
  double rate_critical_ratio{0.3};  ///< 实测 < 期望*ratio → 严重不足
  double latency_warn_ms{40.0};
  double latency_error_ms{100.0};
  double jitter_warn_ratio{0.5};    ///< jitter p99 / 周期
  double loss_warn{0.01};
  double loss_error{0.05};
  double stale_warn_periods{2.0};   ///< 无样本超过 N 个周期 → 数据过期
  double no_publisher_sec{3.0};
  double no_consumer_sec{3.0};
  double chain_warn_ms{80.0};       ///< 跨域链路 p99
  double chain_error_ms{150.0};
  double heartbeat_lease_sec{3.0};
  int debounce_windows{2};          ///< 连续 N 个评估周期才落问题（抑抖）
  int eval_interval_ms{1000};
};

struct NodeCfg {
  std::string node_id;
  std::string domain;               ///< 归属域名称
  double lease_sec{3.0};
};

struct Config {
  std::string name{"dds_diagnosis"};
  std::string description;
  std::vector<DomainCfg> domains;
  std::vector<NodeCfg> nodes;
  RuleCfg rules;

  /// 监控诊断域 / 报告发布的固定入口
  std::string diag_domain{"diag"};
  std::string report_topic{"rt/diag/report"};
  std::string event_topic{"rt/diag/event"};
  std::string heartbeat_topic{"rt/diag/heartbeat"};

  static Config defaults();
  static bool load(const std::string& path, Config* out, std::string* err);

  const DomainCfg* domain_by_name(const std::string& name) const;
  const DomainCfg* diag() const;
};

}  // namespace ddsmon
