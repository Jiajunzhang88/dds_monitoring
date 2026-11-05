#include <cassert>
#include <iostream>

#include "ddsmon/qos_model.hpp"

using namespace ddsmon;

int main()
{
  QosInfo reliable;
  reliable.reliability = DDS_RELIABILITY_RELIABLE;
  reliable.durability = DDS_DURABILITY_VOLATILE;
  reliable.history = DDS_HISTORY_KEEP_LAST;
  reliable.depth = 8;

  QosInfo best_effort = reliable;
  best_effort.reliability = DDS_RELIABILITY_BEST_EFFORT;

  // RELIABLE reader + BEST_EFFORT writer → 不兼容
  assert(check_qos_compat(best_effort, reliable).size() == 1);
  // 反方向可以匹配
  assert(check_qos_compat(reliable, best_effort).empty());
  // 双方 RELIABLE
  assert(check_qos_compat(reliable, reliable).empty());

  // DURABILITY：reader 要求 TRANSIENT_LOCAL，writer 只有 VOLATILE
  QosInfo durable_reader = reliable;
  durable_reader.durability = DDS_DURABILITY_TRANSIENT_LOCAL;
  auto v = check_qos_compat(reliable, durable_reader);
  assert(v.size() == 1 && v[0].policy == "DURABILITY");

  // DEADLINE：reader 要求 20ms，writer 未声明
  QosInfo deadline_reader = reliable;
  deadline_reader.deadline_ns = 20000000LL;
  v = check_qos_compat(reliable, deadline_reader);
  assert(v.size() == 1 && v[0].policy == "DEADLINE");

  // DEADLINE：writer 50ms > reader 20ms 也不兼容
  QosInfo deadline_writer = reliable;
  deadline_writer.deadline_ns = 50000000LL;
  v = check_qos_compat(deadline_writer, deadline_reader);
  assert(v.size() == 1 && v[0].policy == "DEADLINE");

  // DEADLINE 满足
  QosInfo ok_writer = reliable;
  ok_writer.deadline_ns = 10000000LL;
  assert(check_qos_compat(ok_writer, deadline_reader).empty());

  // OWNERSHIP：EXCLUSIVE reader + SHARED writer
  QosInfo exclusive_reader = reliable;
  exclusive_reader.ownership = DDS_OWNERSHIP_EXCLUSIVE;
  v = check_qos_compat(reliable, exclusive_reader);
  assert(v.size() == 1 && v[0].policy == "OWNERSHIP");

  // LIVELINESS：reader MANUAL_BY_TOPIC，writer AUTOMATIC
  QosInfo liveliness_reader = reliable;
  liveliness_reader.liveliness = DDS_LIVELINESS_MANUAL_BY_TOPIC;
  v = check_qos_compat(reliable, liveliness_reader);
  assert(v.size() == 1 && v[0].policy == "LIVELINESS");

  std::cout << "test_qos ok\n";
  return 0;
}
