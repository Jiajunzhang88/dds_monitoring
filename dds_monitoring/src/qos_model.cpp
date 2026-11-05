#include "ddsmon/qos_model.hpp"

#include <cmath>
#include <cstdint>

#include "ddsmon/util.hpp"

namespace ddsmon {

// Cyclone DDS 用 INT64_MAX 表示「无限时长」
static constexpr dds_duration_t kDurationInfinite = INT64_MAX;

std::string QosInfo::reliability_str() const
{
  switch (reliability) {
    case DDS_RELIABILITY_RELIABLE: return "RELIABLE";
    case DDS_RELIABILITY_BEST_EFFORT: return "BEST_EFFORT";
    default: return "default";
  }
}

std::string QosInfo::durability_str() const
{
  switch (durability) {
    case DDS_DURABILITY_VOLATILE: return "VOLATILE";
    case DDS_DURABILITY_TRANSIENT_LOCAL: return "TRANSIENT_LOCAL";
    case DDS_DURABILITY_TRANSIENT: return "TRANSIENT";
    case DDS_DURABILITY_PERSISTENT: return "PERSISTENT";
    default: return "default";
  }
}

std::string QosInfo::history_str() const
{
  if (history == DDS_HISTORY_KEEP_ALL) return "KEEP_ALL";
  if (history == DDS_HISTORY_KEEP_LAST) return "KEEP_LAST(" + std::to_string(depth) + ")";
  return "default";
}

std::string QosInfo::deadline_str() const
{
  if (deadline_ns <= 0) return "infinite";
  return fmt_ns_short(deadline_ns);
}

std::string QosInfo::liveliness_str() const
{
  switch (liveliness) {
    case DDS_LIVELINESS_AUTOMATIC: return "AUTOMATIC";
    case DDS_LIVELINESS_MANUAL_BY_PARTICIPANT: return "MANUAL_BY_PARTICIPANT";
    case DDS_LIVELINESS_MANUAL_BY_TOPIC: return "MANUAL_BY_TOPIC";
    default: return "default";
  }
}

QosInfo read_qos(const dds_qos_t* qos)
{
  QosInfo info;
  if (qos == nullptr) return info;

  dds_reliability_kind_t rk;
  dds_duration_t tmp = 0;
  if (dds_qget_reliability(qos, &rk, &tmp)) info.reliability = static_cast<int>(rk);

  dds_durability_kind_t dk;
  if (dds_qget_durability(qos, &dk)) info.durability = static_cast<int>(dk);

  dds_history_kind_t hk;
  int32_t depth = 0;
  if (dds_qget_history(qos, &hk, &depth)) {
    info.history = static_cast<int>(hk);
    info.depth = depth;
  }

  dds_duration_t deadline = 0;
  if (dds_qget_deadline(qos, &deadline)) {
    info.deadline_ns = (deadline == kDurationInfinite) ? 0 : deadline;
  }

  dds_liveliness_kind_t lk;
  dds_duration_t lease = 0;
  if (dds_qget_liveliness(qos, &lk, &lease)) {
    info.liveliness = static_cast<int>(lk);
    info.lease_ns = (lease == kDurationInfinite) ? 0 : lease;
  }

  dds_ownership_kind_t ok;
  if (dds_qget_ownership(qos, &ok)) info.ownership = static_cast<int>(ok);

  return info;
}

std::vector<QosViolation> check_qos_compat(const QosInfo& writer, const QosInfo& reader)
{
  std::vector<QosViolation> out;

  // QoS 未显式设置时按 DDS 规范默认值处理：RELIABLE / VOLATILE / SHARED /
  // AUTOMATIC；否则会把「默认即满足」的组合误判为冲突。
  const int w_rel = writer.reliability < 0 ? DDS_RELIABILITY_RELIABLE : writer.reliability;
  const int r_rel = reader.reliability < 0 ? DDS_RELIABILITY_RELIABLE : reader.reliability;
  const int w_dur = writer.durability < 0 ? DDS_DURABILITY_VOLATILE : writer.durability;
  const int r_dur = reader.durability < 0 ? DDS_DURABILITY_VOLATILE : reader.durability;
  const int w_own = writer.ownership < 0 ? DDS_OWNERSHIP_SHARED : writer.ownership;
  const int r_own = reader.ownership < 0 ? DDS_OWNERSHIP_SHARED : reader.ownership;
  const int w_liv = writer.liveliness < 0 ? DDS_LIVELINESS_AUTOMATIC : writer.liveliness;
  const int r_liv = reader.liveliness < 0 ? DDS_LIVELINESS_AUTOMATIC : reader.liveliness;

  // RELIABILITY：RELIABLE 的订阅者无法与 BEST_EFFORT 的发布者匹配
  if (r_rel == DDS_RELIABILITY_RELIABLE && w_rel == DDS_RELIABILITY_BEST_EFFORT) {
    out.push_back({"RELIABILITY",
                   "订阅者请求 RELIABLE，发布者仅提供 BEST_EFFORT，两者无法匹配",
                   "发布者改为 RELIABLE，或订阅者改为 BEST_EFFORT"});
  }

  // DURABILITY：订阅者的持久性要求不能高于发布者
  if (w_dur < r_dur) {
    out.push_back({"DURABILITY",
                   "订阅者请求 " + reader.durability_str() + "，发布者仅提供 " +
                       writer.durability_str() + "，历史数据不可用",
                   "发布者提高 DURABILITY（如 TRANSIENT_LOCAL），或订阅者降为 VOLATILE"});
  }

  // DEADLINE：发布者的截止期必须小于等于订阅者要求的截止期
  if (reader.deadline_ns > 0) {
    if (writer.deadline_ns <= 0) {
      out.push_back({"DEADLINE",
                     "订阅者要求 " + reader.deadline_str() + " 的截止期，发布者未声明 deadline",
                     "发布者设置 deadline <= " + reader.deadline_str()});
    } else if (writer.deadline_ns > reader.deadline_ns) {
      out.push_back({"DEADLINE",
                     "发布者 deadline " + writer.deadline_str() + " 大于订阅者要求的 " +
                         reader.deadline_str(),
                     "发布者收紧 deadline，或放宽订阅者 deadline"});
    }
  }

  // LIVELINESS：发布者的 liveliness 级别与租期需满足订阅者
  if (w_liv < r_liv) {
    out.push_back({"LIVELINESS",
                   "订阅者要求 " + reader.liveliness_str() + "，发布者仅提供 " +
                       writer.liveliness_str(),
                   "发布者提升 liveliness 级别，或订阅者降低要求"});
  }
  if (writer.lease_ns > 0 && reader.lease_ns > 0 && writer.lease_ns > reader.lease_ns) {
    out.push_back({"LIVELINESS",
                   "发布者租期 " + fmt_ns_short(writer.lease_ns) + " 超过订阅者可容忍的 " +
                       fmt_ns_short(reader.lease_ns),
                   "缩短发布者 lease_duration，或放宽订阅者要求"});
  }

  // OWNERSHIP：EXCLUSIVE 订阅者不能接受 SHARED 发布者
  if (r_own == DDS_OWNERSHIP_EXCLUSIVE && w_own == DDS_OWNERSHIP_SHARED) {
    out.push_back({"OWNERSHIP",
                   "订阅者请求 EXCLUSIVE，发布者为 SHARED，无法匹配",
                   "发布者改为 EXCLUSIVE，或订阅者改为 SHARED"});
  }

  return out;
}

}  // namespace ddsmon
