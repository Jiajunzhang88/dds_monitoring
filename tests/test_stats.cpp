#include <cassert>
#include <cmath>
#include <iostream>

#include "ddsmon/stats.hpp"

using namespace ddsmon;

int main()
{
  const double hz = 10.0;
  const int64_t period_ns = 100000000LL;  // 100ms

  TopicStats stats(hz);
  int64_t t0 = 1000000000LL;
  for (int i = 0; i < 50; ++i) {
    SampleRec r;
    r.recv_ns = t0 + static_cast<int64_t>(i) * period_ns;
    r.src_ns = r.recv_ns - 20000000LL;  // 20ms 时延
    r.seq = static_cast<uint32_t>(i + 1);
    r.writer = 11;
    stats.add(r);
  }

  int64_t now = t0 + 50 * period_ns;
  TopicMetrics m = stats.snapshot(now, 50 * period_ns);
  assert(m.received == 50);
  assert(std::fabs(m.rate_hz - 10.0) < 0.5);        // 50 帧 / 5s
  assert(m.lost_samples == 0);
  assert(m.loss_ratio == 0.0);
  assert(std::fabs(m.latency_p50_ms - 20.0) < 1.0);
  assert(m.jitter_ms < 1.0);

  // 制造序号空洞：跳到 60，seq 51..59 丢失
  SampleRec r;
  r.recv_ns = now + period_ns;
  r.src_ns = r.recv_ns - 20000000LL;
  r.seq = 60;
  r.writer = 11;
  stats.add(r);
  TopicMetrics m2 = stats.snapshot(r.recv_ns, 60 * period_ns);
  assert(m2.lost_samples == 9);
  assert(m2.expected_samples > 50);
  assert(m2.loss_ratio > 0.1);

  // 数据过期
  TopicMetrics m3 = stats.snapshot(r.recv_ns + 500000000LL, 60 * period_ns);
  assert(m3.stale_ms > 400.0);

  // 多发布者：两个 writer 各自独立统计序号
  TopicStats multi(hz);
  for (int w = 0; w < 2; ++w) {
    for (int i = 0; i < 10; ++i) {
      SampleRec s;
      s.recv_ns = t0 + i * period_ns + w * 5000000LL;
      s.src_ns = s.recv_ns;
      s.seq = static_cast<uint32_t>(i + 1);
      s.writer = static_cast<uint64_t>(100 + w);
      multi.add(s);
    }
  }
  TopicMetrics mm = multi.snapshot(t0 + 10 * period_ns, 10 * period_ns);
  assert(mm.writers == 2);
  assert(mm.lost_samples == 0);

  std::cout << "test_stats ok\n";
  return 0;
}
