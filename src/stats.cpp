#include "ddsmon/stats.hpp"

#include <algorithm>
#include <cmath>
#include <map>

namespace ddsmon {

namespace {

double percentile(std::vector<double> v, double p)
{
  if (v.empty()) return 0.0;
  size_t idx = static_cast<size_t>(std::floor((v.size() - 1) * p));
  std::nth_element(v.begin(), v.begin() + static_cast<long>(idx), v.end());
  return v[idx];
}

}  // namespace

TopicStats::TopicStats(double expect_hz) : expect_hz_(expect_hz > 0 ? expect_hz : 1.0) {}

void TopicStats::add(const SampleRec& rec)
{
  samples_.push_back(rec);
  received_ += 1;
  if (first_recv_ns_ == 0) first_recv_ns_ = rec.recv_ns;

  // 只保留最近 60s，且不超过 20000 条，防止长时间运行内存膨胀
  const int64_t keep_ns = 60LL * 1000 * 1000 * 1000;
  while (!samples_.empty() && rec.recv_ns - samples_.front().recv_ns > keep_ns) {
    samples_.pop_front();
  }
  while (samples_.size() > 20000) samples_.pop_front();

  WriterState& ws = writers_[rec.writer];
  const double period_ms = 1000.0 / expect_hz_;

  if (ws.last_recv_ns != 0) {
    double interval_ms = static_cast<double>(rec.recv_ns - ws.last_recv_ns) / 1e6;
    if (interval_ms > period_ms * 1.5) ws.deadline_miss += 1;
  }
  ws.last_recv_ns = rec.recv_ns;

  if (rec.seq != 0) {
    if (ws.has_seq) {
      if (rec.seq > ws.last_seq) {
        uint32_t delta = rec.seq - ws.last_seq;
        ws.gaps += delta - 1;
        ws.expected += delta;
      } else {
        // 序号回绕/发布者重启：按新一段重新计数
        ws.expected += 1;
      }
    } else {
      ws.expected += 1;
      ws.has_seq = true;
    }
    ws.last_seq = rec.seq;
  }
}

TopicMetrics TopicStats::snapshot(int64_t now, int64_t window_ns) const
{
  TopicMetrics m;
  m.received = received_;
  m.writers = static_cast<int>(writers_.size());

  for (const auto& kv : writers_) {
    m.deadline_miss += kv.second.deadline_miss;
    m.lost_samples += kv.second.gaps;
    m.expected_samples += kv.second.expected;
  }
  if (m.expected_samples > 0) {
    m.loss_ratio = static_cast<double>(m.lost_samples) / static_cast<double>(m.expected_samples);
  }

  const int64_t from_ns = now - window_ns;
  std::vector<double> intervals_ms;
  std::vector<double> latencies_ms;
  std::vector<double> chains_ms;
  uint64_t in_window = 0;

  const double period_ms = 1000.0 / expect_hz_;
  // 同一话题可能有多个发布者（多个传感器/冗余节点），到达间隔必须按发布者分别计算，
  // 否则交错到达会被误判成抖动或丢帧。
  std::map<uint64_t, int64_t> last_by_writer;

  for (const auto& s : samples_) {
    if (s.recv_ns < from_ns) continue;
    ++in_window;

    if (s.src_ns > 0) {
      double lat = static_cast<double>(s.recv_ns - s.src_ns) / 1e6;
      latencies_ms.push_back(lat > 0 ? lat : 0.0);
    }
    if (s.origin_ns > 0) {
      double lat = static_cast<double>(s.recv_ns - s.origin_ns) / 1e6;
      chains_ms.push_back(lat > 0 ? lat : 0.0);
    }
    int64_t& prev = last_by_writer[s.writer];
    if (prev != 0) {
      double interval = static_cast<double>(s.recv_ns - prev) / 1e6;
      intervals_ms.push_back(interval);
    }
    prev = s.recv_ns;
  }

  m.samples_in_window = in_window;
  // 观测窗口不能超过「首帧到现在」的实际时长，否则刚启动/刚恢复的话题频率会被低估，
  // 从而在进程启动初期误报 RATE_LOW。
  int64_t span_ns = window_ns;
  if (first_recv_ns_ > 0 && now > first_recv_ns_) {
    int64_t observed = now - first_recv_ns_;
    if (observed < span_ns) span_ns = observed;
  }
  double window_sec = static_cast<double>(span_ns) / 1e9;
  if (window_sec < 0.2) window_sec = 0.2;
  m.rate_hz = static_cast<double>(in_window) / window_sec;

  if (!intervals_ms.empty()) {
    std::vector<double> jitter;
    jitter.reserve(intervals_ms.size());
    for (double v : intervals_ms) jitter.push_back(std::fabs(v - period_ms));
    m.jitter_ms = percentile(jitter, 0.99);
    m.interval_max_ms = *std::max_element(intervals_ms.begin(), intervals_ms.end());
    std::vector<double> sorted = intervals_ms;
    m.interval_p50_ms = percentile(sorted, 0.50);
    m.latency_p50_ms = percentile(latencies_ms, 0.50);
  }

  if (!latencies_ms.empty()) {
    m.latency_p50_ms = percentile(latencies_ms, 0.50);
    m.latency_p99_ms = percentile(latencies_ms, 0.99);
    m.latency_max_ms = *std::max_element(latencies_ms.begin(), latencies_ms.end());
  }
  if (!chains_ms.empty()) {
    m.chain_p50_ms = percentile(chains_ms, 0.50);
    m.chain_p99_ms = percentile(chains_ms, 0.99);
  }

  if (!samples_.empty()) {
    m.last_recv_ns = samples_.back().recv_ns;
    m.stale_ms = static_cast<double>(now - samples_.back().recv_ns) / 1e6;
    if (m.stale_ms < 0) m.stale_ms = 0;
  }
  return m;
}

void TopicStats::clear_history() { samples_.clear(); }

}  // namespace ddsmon
