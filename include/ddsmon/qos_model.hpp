#pragma once

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "dds/dds.h"
}

namespace ddsmon {

/// DDS QoS 的常用策略快照（只保留 RxO 相关的部分）
struct QosInfo {
  int reliability{-1};   ///< 0 BEST_EFFORT, 1 RELIABLE, -1 未知
  int durability{-1};    ///< 0 VOLATILE, 1 TRANSIENT_LOCAL, 2 TRANSIENT, 3 PERSISTENT
  int history{-1};       ///< 0 KEEP_LAST, 1 KEEP_ALL
  int depth{-1};
  int liveliness{-1};    ///< 0 AUTOMATIC, 1 MANUAL_BY_PARTICIPANT, 2 MANUAL_BY_TOPIC
  int64_t lease_ns{0};   ///< 0 表示无限
  int64_t deadline_ns{0};///< 0 表示无限
  int ownership{-1};     ///< 0 SHARED, 1 EXCLUSIVE

  std::string reliability_str() const;
  std::string durability_str() const;
  std::string history_str() const;
  std::string deadline_str() const;
  std::string liveliness_str() const;
};

/// 从 DDS QoS 对象读取快照；qos 为 nullptr 时返回默认值
QosInfo read_qos(const dds_qos_t* qos);

struct QosViolation {
  std::string policy;   ///< RELIABILITY / DURABILITY / DEADLINE / LIVELINESS / OWNERSHIP
  std::string detail;   ///< 人类可读说明
  std::string suggestion;
};

/// 检查 writer 提供（offered）的 QoS 是否满足 reader 请求（requested）的 QoS
std::vector<QosViolation> check_qos_compat(const QosInfo& writer, const QosInfo& reader);

}  // namespace ddsmon
