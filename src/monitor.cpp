#include "ddsmon/monitor.hpp"

#include "ddsmon/util.hpp"

namespace ddsmon {

TopicMonitor::TopicMonitor() = default;

TopicMonitor::~TopicMonitor()
{
  if (reader_ > 0) dds_delete(reader_);
  if (topic_ > 0) dds_delete(topic_);
}

bool TopicMonitor::init(dds_entity_t participant, const TopicCfg& cfg, std::string* err)
{
  cfg_ = cfg;
  stats_ = std::make_unique<TopicStats>(cfg.expect_hz);

  adapter_ = find_adapter(cfg.type);
  if (adapter_ == nullptr) {
    if (err) *err = "no payload adapter for type '" + cfg.type + "'";
    return false;
  }

  topic_ = dds_create_topic(participant, adapter_->desc, cfg.name.c_str(), nullptr, nullptr);
  if (topic_ < 0) {
    if (err) {
      *err = "create topic '" + cfg.name + "' failed: " + dds_strretcode(topic_);
    }
    return false;
  }

  // 诊断 reader：BEST_EFFORT 可以同时匹配 RELIABLE 与 BEST_EFFORT 发布者
  dds_qos_t* qos = dds_create_qos();
  dds_qset_reliability(qos, DDS_RELIABILITY_BEST_EFFORT, DDS_SECS(1));
  dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 128);
  dds_qset_durability(qos, DDS_DURABILITY_VOLATILE);
  reader_ = dds_create_reader(participant, topic_, qos, nullptr);
  dds_delete_qos(qos);

  if (reader_ < 0) {
    if (err) *err = "create reader for '" + cfg.name + "' failed: " + dds_strretcode(reader_);
    return false;
  }
  return true;
}

void TopicMonitor::poll(int64_t now)
{
  if (reader_ <= 0) return;
  constexpr int kBatch = 64;
  for (int round = 0; round < 16; ++round) {
    void* buf[kBatch] = {nullptr};
    dds_sample_info_t si[kBatch];
    dds_return_t n = dds_take(reader_, buf, si, kBatch, kBatch);
    if (n <= 0) break;

    for (dds_return_t i = 0; i < n; ++i) {
      if (!si[i].valid_data) continue;
      SampleRec rec;
      rec.recv_ns = now;
      rec.seq = adapter_->seq_of ? adapter_->seq_of(buf[i]) : 0;
      rec.src_ns = adapter_->stamp_of ? adapter_->stamp_of(buf[i]) : 0;
      rec.origin_ns = adapter_->origin_of ? adapter_->origin_of(buf[i]) : 0;
      rec.writer = si[i].publication_handle;
      stats_->add(rec);
    }
    dds_return_loan(reader_, buf, n);

    if (n < kBatch) break;
  }
}

TopicMetrics TopicMonitor::metrics(int64_t now) const { return stats_->snapshot(now); }

}  // namespace ddsmon
