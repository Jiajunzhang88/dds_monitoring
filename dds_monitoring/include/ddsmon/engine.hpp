#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "ddsmon/config.hpp"
#include "ddsmon/monitor.hpp"
#include "ddsmon/qos_model.hpp"
#include "ddsmon/rules.hpp"
#include "ddsmon/stats.hpp"
#include "ddsmon/topology.hpp"
#include "ddsmon/types.hpp"

namespace ddsmon {

struct DomainInfo {
  std::string name;
  int domain_id{0};
  int participants{0};
  int writers{0};
  int readers{0};
  int declared_topics{0};
  int active_topics{0};
  int health{100};
};

struct TopicRow {
  std::string domain;
  int domain_id{0};
  std::string topic;
  std::string type;
  int publishers{0};
  int subscribers{0};
  double expect_hz{0};
  bool consumer_required{true};
  bool critical{true};
  bool reader_ok{true};
  std::vector<std::string> types_seen;
  TopicMetrics metrics;
};

struct QosCheckRow {
  std::string domain;
  std::string topic;
  std::string policy;
  std::string writer;
  std::string reader;
  std::string detail;
  std::string suggestion;
};

struct NodeRow {
  std::string node_id;
  std::string domain;
  int state{0};
  bool alive{false};
  double cpu{0};
  double mem{0};
  int64_t last_seen_ms{0};
  uint64_t seq{0};
};

struct ChainRow {
  std::string name;
  std::string domain;
  std::string topic;
  double p50_ms{0};
  double p99_ms{0};
  size_t samples{0};
};

/// 某一时刻的完整诊断视图，报告/看板都基于它渲染
struct Snapshot {
  int64_t at_ns{0};
  int health{100};
  std::vector<DomainInfo> domains;
  std::vector<TopicRow> topics;
  std::vector<QosCheckRow> qos_checks;
  std::vector<NodeRow> nodes;
  std::vector<ChainRow> chains;
  std::vector<Issue> issues;
};

/// 诊断引擎：管理三个域的 participant、内置主题 reader、话题 reader，
/// 周期性采集 → 计算指标 → 规则判定 → 生成 Snapshot。
class Engine {
 public:
  Engine();
  ~Engine();

  bool init(const Config& cfg, std::string* err);
  /// 推进一次采集；内部按 eval_interval 生成新 Snapshot
  void tick();
  /// 强制立即刷新 Snapshot（用于报告收尾）
  void refresh();

  const Snapshot& snapshot() const { return snapshot_; }
  const Config& config() const { return cfg_; }
  bool healthy() const { return inited_; }

 private:
  struct DomainRuntime;
  void poll_domains(int64_t now, bool poll_topology);
  void build_snapshot(int64_t now);

  Config cfg_;
  bool inited_{false};
  std::vector<std::unique_ptr<DomainRuntime>> domains_;
  std::map<std::string, NodeRow> nodes_;
  Snapshot snapshot_;
  RulesEngine rules_;
  int64_t last_eval_ns_{0};
  int64_t last_topo_ns_{0};
};

}  // namespace ddsmon
