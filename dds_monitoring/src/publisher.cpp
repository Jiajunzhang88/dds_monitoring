#include "ddsmon/publisher.hpp"

#include <cstring>

#include "ddsmon/util.hpp"
#include "ddsmon.h"

namespace ddsmon {

namespace {

char* dup_cstr(const std::string& s)
{
  char* p = static_cast<char*>(dds_alloc(s.size() + 1));
  if (!p) return nullptr;
  std::memcpy(p, s.c_str(), s.size() + 1);
  return p;
}

void free_cstr(char* p)
{
  if (p) dds_free(p);
}

template <typename Seq, typename Elem>
void init_seq(Seq* seq, size_t n)
{
  seq->_buffer = static_cast<Elem*>(dds_alloc(sizeof(Elem) * n));
  seq->_length = static_cast<uint32_t>(n);
  seq->_maximum = static_cast<uint32_t>(n);
  seq->_release = true;
  std::memset(seq->_buffer, 0, sizeof(Elem) * n);
}

template <typename Seq>
void free_seq(Seq* seq)
{
  if (seq->_buffer) dds_free(seq->_buffer);
  seq->_buffer = nullptr;
  seq->_length = 0;
  seq->_maximum = 0;
}

}  // namespace

DiagPublisher::DiagPublisher() = default;

DiagPublisher::~DiagPublisher() { shutdown(); }

bool DiagPublisher::init(const Config& cfg, std::string* err)
{
  const DomainCfg* diag = cfg.diag();
  if (diag == nullptr) {
    if (err) *err = "config has no diag domain '" + cfg.diag_domain + "'";
    return false;
  }

  participant_ =
      dds_create_participant(static_cast<dds_domainid_t>(diag->domain_id), nullptr, nullptr);
  if (participant_ < 0) {
    if (err) *err = "create diag participant failed: " + std::string(dds_strretcode(participant_));
    return false;
  }

  const PayloadAdapter* report_ad = find_adapter("DiagReport");
  const PayloadAdapter* event_ad = find_adapter("DiagEvent");
  topic_report_ = dds_create_topic(participant_, report_ad->desc, cfg.report_topic.c_str(), nullptr, nullptr);
  topic_event_ = dds_create_topic(participant_, event_ad->desc, cfg.event_topic.c_str(), nullptr, nullptr);
  if (topic_report_ < 0 || topic_event_ < 0) {
    if (err) *err = "create diag topics failed";
    return false;
  }

  dds_qos_t* qos = dds_create_qos();
  dds_qset_reliability(qos, DDS_RELIABILITY_RELIABLE, DDS_SECS(1));
  dds_qset_history(qos, DDS_HISTORY_KEEP_LAST, 8);
  dds_qset_durability(qos, DDS_DURABILITY_VOLATILE);
  writer_report_ = dds_create_writer(participant_, topic_report_, qos, nullptr);
  writer_event_ = dds_create_writer(participant_, topic_event_, qos, nullptr);
  dds_delete_qos(qos);
  if (writer_report_ < 0 || writer_event_ < 0) {
    if (err) *err = "create diag writers failed";
    return false;
  }
  return true;
}

bool DiagPublisher::publish_report(const Snapshot& snap)
{
  if (writer_report_ < 0) return false;

  ddsmon_diag_DiagReport msg;
  std::memset(&msg, 0, sizeof(msg));
  std::string report_id = "RPT_" + std::to_string(++report_seq_) + "_" + std::to_string(snap.at_ns);
  msg.report_id = dup_cstr(report_id);
  msg.stamp_ns = snap.at_ns;
  msg.health_score = snap.health;
  msg.issue_total = static_cast<uint32_t>(snap.issues.size());

  uint32_t crit = 0, err_n = 0, warn = 0;
  for (const auto& i : snap.issues) {
    int sev = static_cast<int>(i.severity);
    if (sev >= kSeverityCritical) ++crit;
    else if (sev == kSeverityError) ++err_n;
    else if (sev == kSeverityWarn) ++warn;
  }
  msg.critical_num = crit;
  msg.error_num = err_n;
  msg.warn_num = warn;

  size_t n_issue = snap.issues.size();
  init_seq<dds_sequence_ddsmon_diag_Issue, ddsmon_diag_Issue>(&msg.issues, n_issue);
  for (size_t i = 0; i < n_issue; ++i) {
    ddsmon_diag_Issue& out = msg.issues._buffer[i];
    out.code = dup_cstr(snap.issues[i].code);
    out.severity = static_cast<int32_t>(snap.issues[i].severity);
    out.domain = dup_cstr(snap.issues[i].domain);
    out.topic = dup_cstr(snap.issues[i].topic);
    out.detail = dup_cstr(snap.issues[i].detail);
    out.value = snap.issues[i].value;
    out.threshold = snap.issues[i].threshold;
  }

  size_t n_topic = snap.topics.size();
  init_seq<dds_sequence_ddsmon_diag_TopicMetric, ddsmon_diag_TopicMetric>(&msg.topics, n_topic);
  for (size_t i = 0; i < n_topic; ++i) {
    const TopicRow& t = snap.topics[i];
    ddsmon_diag_TopicMetric& out = msg.topics._buffer[i];
    out.domain = dup_cstr(t.domain);
    out.topic = dup_cstr(t.topic);
    out.publishers = static_cast<uint32_t>(t.publishers);
    out.subscribers = static_cast<uint32_t>(t.subscribers);
    out.rate_hz = t.metrics.rate_hz;
    out.expect_hz = t.expect_hz;
    out.jitter_ms = t.metrics.jitter_ms;
    out.latency_ms = t.metrics.latency_p99_ms;
    out.loss_ratio = t.metrics.loss_ratio;
    out.received = t.metrics.received;
    out.deadline_miss = static_cast<uint32_t>(t.metrics.deadline_miss);
  }

  dds_return_t rc = dds_write_ts(writer_report_, &msg, snap.at_ns);

  // 写成功后中间件已完成序列化，释放本地内存
  free_cstr(msg.report_id);
  for (size_t i = 0; i < n_issue; ++i) {
    free_cstr(msg.issues._buffer[i].code);
    free_cstr(msg.issues._buffer[i].domain);
    free_cstr(msg.issues._buffer[i].topic);
    free_cstr(msg.issues._buffer[i].detail);
  }
  free_seq<dds_sequence_ddsmon_diag_Issue>(&msg.issues);
  for (size_t i = 0; i < n_topic; ++i) {
    free_cstr(msg.topics._buffer[i].domain);
    free_cstr(msg.topics._buffer[i].topic);
  }
  free_seq<dds_sequence_ddsmon_diag_TopicMetric>(&msg.topics);

  return rc == DDS_RETCODE_OK;
}

bool DiagPublisher::publish_event(int severity, const std::string& code, const std::string& domain,
                                  const std::string& topic, const std::string& message)
{
  if (writer_event_ < 0) return false;
  ddsmon_diag_DiagEvent msg;
  std::memset(&msg, 0, sizeof(msg));
  int64_t now = now_ns();
  msg.stamp_ns = now;
  msg.code = dup_cstr(code);
  msg.severity = severity;
  msg.domain = dup_cstr(domain);
  msg.topic = dup_cstr(topic);
  msg.message = dup_cstr(message);

  dds_return_t rc = dds_write_ts(writer_event_, &msg, now);
  free_cstr(msg.code);
  free_cstr(msg.domain);
  free_cstr(msg.topic);
  free_cstr(msg.message);
  return rc == DDS_RETCODE_OK;
}

void DiagPublisher::shutdown()
{
  if (writer_report_ > 0) dds_delete(writer_report_);
  if (writer_event_ > 0) dds_delete(writer_event_);
  if (topic_report_ > 0) dds_delete(topic_report_);
  if (topic_event_ > 0) dds_delete(topic_event_);
  if (participant_ > 0) dds_delete(participant_);
  writer_report_ = writer_event_ = topic_report_ = topic_event_ = participant_ = DDS_ENTITY_NIL;
}

}  // namespace ddsmon
