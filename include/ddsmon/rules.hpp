#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "ddsmon/config.hpp"
#include "ddsmon/types.hpp"

namespace ddsmon {

struct Snapshot;  // engine.hpp 中定义

/// 规则引擎：把 Snapshot 中的指标翻译成 Issue。
///
/// 每条规则按 (code, domain, topic) 维护持续时间计数，连续 N 个评估窗口成立才落问题
/// （debounce），避免瞬时抖动刷屏。问题恢复后从活跃集合移除，
/// 由发布器负责发出「已恢复」事件。
class RulesEngine {
 public:
  void evaluate(Snapshot& snap, const Config& cfg, int64_t now_ns);

  struct State {
    int streak{0};
    int64_t since_ns{0};
    Issue issue;
  };
  const std::map<std::string, State>& active() const { return active_; }

 private:
  void apply(Snapshot& snap, const Config& cfg, int64_t now, const Issue& issue, bool condition);
  std::map<std::string, State> active_;
  int64_t started_ns_{0};
};

std::string issue_key(const Issue& issue);

}  // namespace ddsmon
