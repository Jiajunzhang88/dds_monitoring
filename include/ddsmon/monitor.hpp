#pragma once

#include <cstdint>
#include <memory>
#include <string>

extern "C" {
#include "dds/dds.h"
}

#include "ddsmon/config.hpp"
#include "ddsmon/stats.hpp"
#include "ddsmon/types.hpp"

namespace ddsmon {

/// 一个被监控话题：持有一个诊断用 DataReader，把样本喂给 TopicStats。
///
/// 诊断 reader 故意使用 BEST_EFFORT + VOLATILE + KEEP_LAST，
/// 这样既能匹配 RELIABLE 发布者也能匹配 BEST_EFFORT 发布者，
/// 避免「QoS 不匹配导致完全收不到数据」把真实问题掩盖掉；
/// 发布/订阅侧真实的 QoS 由内置主题采集后单独做兼容性检查。
class TopicMonitor {
 public:
  TopicMonitor();
  ~TopicMonitor();

  bool init(dds_entity_t participant, const TopicCfg& cfg, std::string* err);
  void poll(int64_t now);

  const TopicCfg& cfg() const { return cfg_; }
  const TopicStats& stats() const { return *stats_; }
  TopicMetrics metrics(int64_t now) const;
  bool ok() const { return reader_ >= 0; }
  std::string type_name() const { return adapter_ ? adapter_->dds_type_name : cfg_.type; }

 private:
  TopicCfg cfg_;
  const PayloadAdapter* adapter_{nullptr};
  dds_entity_t topic_{DDS_ENTITY_NIL};
  dds_entity_t reader_{DDS_ENTITY_NIL};
  std::unique_ptr<TopicStats> stats_;
};

}  // namespace ddsmon
