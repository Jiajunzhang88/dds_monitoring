#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "dds/dds.h"
}

#include "ddsmon/config.hpp"
#include "ddsmon/engine.hpp"
#include "ddsmon/types.hpp"

namespace ddsmon {

/// 监控诊断域的发布端：把诊断结论回写到 DDS（闭环）。
///
/// - rt/diag/report : 周期性完整报告（DiagReport）
/// - rt/diag/event  : 问题出现/恢复事件（DiagEvent）
class DiagPublisher {
 public:
  DiagPublisher();
  ~DiagPublisher();

  bool init(const Config& cfg, std::string* err);
  bool publish_report(const Snapshot& snap);
  bool publish_event(int severity, const std::string& code, const std::string& domain,
                     const std::string& topic, const std::string& message);
  void shutdown();

  bool ready() const { return writer_report_ >= 0; }

 private:
  dds_entity_t participant_{DDS_ENTITY_NIL};
  dds_entity_t topic_report_{DDS_ENTITY_NIL};
  dds_entity_t topic_event_{DDS_ENTITY_NIL};
  dds_entity_t writer_report_{DDS_ENTITY_NIL};
  dds_entity_t writer_event_{DDS_ENTITY_NIL};
  uint64_t report_seq_{0};
};

}  // namespace ddsmon
