#pragma once

#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace ddsmon {

/// 一条样本的采集记录
struct SampleRec {
  int64_t recv_ns{0};    ///< 本机收到时刻
  int64_t src_ns{0};     ///< DDS 源时间戳（发布端写入时刻）
  int64_t origin_ns{0};  ///< 跨域链路起点（如感知输出的时间戳），0 表示无
  uint32_t seq{0};       ///< 消息序号，0 表示该类型无序号
  uint64_t writer{0};    ///< publication handle，用于区分多发布者
};

/// 单个话题的诊断指标（滑动窗口计算结果）
struct TopicMetrics {
  uint64_t received{0};       ///< 累计收到样本
  double rate_hz{0.0};        ///< 窗口内实测频率
  double jitter_ms{0.0};      ///< 到达间隔抖动 p99（相对期望周期）
  double interval_p50_ms{0.0};
  double latency_p50_ms{0.0};
  double latency_p99_ms{0.0};
  double latency_max_ms{0.0};
  double chain_p50_ms{0.0};   ///< 跨域链路时延（origin → 本机收到）
  double chain_p99_ms{0.0};
  double loss_ratio{0.0};     ///< 序号空洞比例（累计）
  uint64_t lost_samples{0};
  uint64_t expected_samples{0};
  uint64_t deadline_miss{0};  ///< 超过 1.5 个期望周期才到的次数
  double stale_ms{0.0};       ///< 距最近一次样本的时长
  int writers{0};
  int64_t last_recv_ns{0};
  size_t samples_in_window{0};
  double interval_max_ms{0.0};
};

/// 单话题统计：滑动样本序列 + 每发布者的序号连续性跟踪
class TopicStats {
 public:
  explicit TopicStats(double expect_hz);
  void add(const SampleRec& rec);
  TopicMetrics snapshot(int64_t now_ns, int64_t window_ns = 5LL * 1000 * 1000 * 1000) const;

  /// 清空窗口内样本但保留累计计数（用于报告分段）
  void clear_history();

 private:
  struct WriterState {
    bool has_seq{false};
    uint32_t last_seq{0};
    uint64_t gaps{0};
    uint64_t expected{0};
    int64_t last_recv_ns{0};
    uint64_t deadline_miss{0};
  };

  double expect_hz_;
  std::deque<SampleRec> samples_;
  std::map<uint64_t, WriterState> writers_;
  uint64_t received_{0};
  uint64_t lost_{0};
  uint64_t expected_{0};
  int64_t first_recv_ns_{0};  // 首帧到达时刻：用于计算真实观测窗口，避免启动初期频率被低估
};

}  // namespace ddsmon
