#include "ddsmon/metrics.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include "ddsmon/engine.hpp"

namespace ddsmon {

namespace {

std::string escape_label(const std::string& s)
{
  std::string out;
  out.reserve(s.size() + 8);
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
      out.push_back(c);
    } else if (c == '\n') {
      out += "\\n";
    } else {
      out.push_back(c);
    }
  }
  return out;
}

std::string num(double v)
{
  if (!std::isfinite(v)) v = 0.0;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  return buf;
}

std::string labels(const std::vector<std::pair<std::string, std::string>>& kv)
{
  if (kv.empty()) return "";
  std::string out = "{";
  for (size_t i = 0; i < kv.size(); ++i) {
    if (i) out += ",";
    out += kv[i].first + "=\"" + escape_label(kv[i].second) + "\"";
  }
  out += "}";
  return out;
}

void header(std::ostringstream& os, const std::string& name, const std::string& help,
            const std::string& type)
{
  os << "# HELP " << name << " " << help << "\n";
  os << "# TYPE " << name << " " << type << "\n";
}

void sample(std::ostringstream& os, const std::string& name,
            const std::vector<std::pair<std::string, std::string>>& kv, double value)
{
  os << name << labels(kv) << " " << num(value) << "\n";
}

std::string severity_tag(Severity s)
{
  switch (s) {
    case Severity::Critical: return "CRITICAL";
    case Severity::Error: return "ERROR";
    case Severity::Warn: return "WARN";
    default: return "INFO";
  }
}

}  // namespace

std::string render_prometheus(const Snapshot& snap, const Config& cfg)
{
  std::ostringstream os;

  header(os, "ddsmon_up", "诊断进程存活（恒为 1，进程消失即抓取失败）", "gauge");
  sample(os, "ddsmon_up", {}, 1.0);

  std::vector<std::pair<std::string, std::string>> cfg_labels = {{"config", cfg.name}};
  header(os, "ddsmon_config_info", "当前诊断配置标识", "gauge");
  sample(os, "ddsmon_config_info", cfg_labels, 1.0);

  header(os, "ddsmon_health_score", "综合健康分（0-100）", "gauge");
  sample(os, "ddsmon_health_score", {}, snap.health);

  header(os, "ddsmon_snapshot_timestamp_seconds", "最近一次诊断评估的时间戳", "gauge");
  sample(os, "ddsmon_snapshot_timestamp_seconds", {}, static_cast<double>(snap.at_ns) / 1e9);

  int n_crit = 0, n_err = 0, n_warn = 0, n_info = 0;
  for (const auto& i : snap.issues) {
    switch (i.severity) {
      case Severity::Critical: ++n_crit; break;
      case Severity::Error: ++n_err; break;
      case Severity::Warn: ++n_warn; break;
      default: ++n_info; break;
    }
  }
  header(os, "ddsmon_issue_active", "当前活跃问题（1=成立），按代码/域/话题标注", "gauge");
  for (const auto& i : snap.issues) {
    sample(os, "ddsmon_issue_active",
           {{"code", i.code},
            {"severity", severity_tag(i.severity)},
            {"domain", i.domain},
            {"topic", i.topic},
            {"detail", i.detail}},
           1.0);
  }
  header(os, "ddsmon_issues_by_severity", "当前活跃问题数量（按级别）", "gauge");
  sample(os, "ddsmon_issues_by_severity", {{"severity", "CRITICAL"}}, n_crit);
  sample(os, "ddsmon_issues_by_severity", {{"severity", "ERROR"}}, n_err);
  sample(os, "ddsmon_issues_by_severity", {{"severity", "WARN"}}, n_warn);
  sample(os, "ddsmon_issues_by_severity", {{"severity", "INFO"}}, n_info);

  header(os, "ddsmon_topics_monitored", "被监控话题数量", "gauge");
  sample(os, "ddsmon_topics_monitored", {}, static_cast<double>(snap.topics.size()));

  header(os, "ddsmon_domain_participants", "域内 participant 数量", "gauge");
  for (const auto& d : snap.domains) {
    sample(os, "ddsmon_domain_participants",
           {{"domain", d.name}, {"domain_id", std::to_string(d.domain_id)}}, d.participants);
  }
  header(os, "ddsmon_domain_publishers", "域内 writer 数量", "gauge");
  for (const auto& d : snap.domains) {
    sample(os, "ddsmon_domain_publishers",
           {{"domain", d.name}, {"domain_id", std::to_string(d.domain_id)}}, d.writers);
  }
  header(os, "ddsmon_domain_subscribers", "域内 reader 数量", "gauge");
  for (const auto& d : snap.domains) {
    sample(os, "ddsmon_domain_subscribers",
           {{"domain", d.name}, {"domain_id", std::to_string(d.domain_id)}}, d.readers);
  }
  header(os, "ddsmon_domain_health", "域健康分（0-100）", "gauge");
  for (const auto& d : snap.domains) {
    sample(os, "ddsmon_domain_health",
           {{"domain", d.name}, {"domain_id", std::to_string(d.domain_id)}}, d.health);
  }

  header(os, "ddsmon_topic_rate_hz", "实测话题频率", "gauge");
  header(os, "ddsmon_topic_rate_expected_hz", "期望话题频率", "gauge");
  header(os, "ddsmon_topic_jitter_p99_ms", "到达间隔抖动 p99", "gauge");
  header(os, "ddsmon_topic_latency_p50_ms", "端到端时延 p50", "gauge");
  header(os, "ddsmon_topic_latency_p99_ms", "端到端时延 p99", "gauge");
  header(os, "ddsmon_topic_loss_ratio", "样本丢失率（0-1）", "gauge");
  header(os, "ddsmon_topic_stale_ms", "距最近一帧的时间", "gauge");
  header(os, "ddsmon_topic_received_total", "窗口内收到的样本数", "gauge");
  header(os, "ddsmon_topic_deadline_miss_total", "deadline miss 次数", "gauge");
  header(os, "ddsmon_topic_publishers", "话题发布者数量", "gauge");
  header(os, "ddsmon_topic_subscribers", "话题订阅者数量", "gauge");
  header(os, "ddsmon_topic_status", "话题状态（0=OK 1=INFO 2=WARN 3=ERROR 4=CRITICAL）", "gauge");
  for (const auto& t : snap.topics) {
    auto base = std::vector<std::pair<std::string, std::string>>{
        {"domain", t.domain}, {"topic", t.topic}, {"type", t.type}};
    sample(os, "ddsmon_topic_rate_hz", base, t.metrics.rate_hz);
    sample(os, "ddsmon_topic_rate_expected_hz", base, t.expect_hz);
    sample(os, "ddsmon_topic_jitter_p99_ms", base, t.metrics.jitter_ms);
    sample(os, "ddsmon_topic_latency_p50_ms", base, t.metrics.latency_p50_ms);
    sample(os, "ddsmon_topic_latency_p99_ms", base, t.metrics.latency_p99_ms);
    sample(os, "ddsmon_topic_loss_ratio", base, t.metrics.loss_ratio);
    sample(os, "ddsmon_topic_stale_ms", base, t.metrics.stale_ms);
    sample(os, "ddsmon_topic_received_total", base, static_cast<double>(t.metrics.received));
    sample(os, "ddsmon_topic_deadline_miss_total", base,
           static_cast<double>(t.metrics.deadline_miss));
    sample(os, "ddsmon_topic_publishers", base, t.publishers);
    sample(os, "ddsmon_topic_subscribers", base, t.subscribers);
    int status = 0;
    for (const auto& i : snap.issues) {
      if (i.domain == t.domain && i.topic == t.topic) {
        status = std::max(status, static_cast<int>(i.severity));
      }
    }
    sample(os, "ddsmon_topic_status", base, status);
  }

  header(os, "ddsmon_qos_incompatible_pairs", "不兼容的 writer/reader 端点数", "gauge");
  sample(os, "ddsmon_qos_incompatible_pairs", {}, static_cast<double>(snap.qos_checks.size()));
  header(os, "ddsmon_qos_incompatible", "QoS 不兼容（1=存在），按话题与策略标注", "gauge");
  for (const auto& q : snap.qos_checks) {
    sample(os, "ddsmon_qos_incompatible",
           {{"domain", q.domain}, {"topic", q.topic}, {"policy", q.policy}}, 1.0);
  }

  header(os, "ddsmon_chain_latency_p50_ms", "跨域链路时延 p50", "gauge");
  header(os, "ddsmon_chain_latency_p99_ms", "跨域链路时延 p99", "gauge");
  header(os, "ddsmon_chain_samples", "链路统计样本数", "gauge");
  for (const auto& c : snap.chains) {
    auto base = std::vector<std::pair<std::string, std::string>>{
        {"chain", c.name}, {"topic", c.topic}, {"domain", c.domain}};
    sample(os, "ddsmon_chain_latency_p50_ms", base, c.p50_ms);
    sample(os, "ddsmon_chain_latency_p99_ms", base, c.p99_ms);
    sample(os, "ddsmon_chain_samples", base, static_cast<double>(c.samples));
  }

  header(os, "ddsmon_node_alive", "节点心跳存活（1=在 lease 内）", "gauge");
  header(os, "ddsmon_node_cpu_percent", "节点 CPU 占用", "gauge");
  header(os, "ddsmon_node_mem_mb", "节点内存占用（MB）", "gauge");
  header(os, "ddsmon_node_heartbeat_age_seconds", "距最近心跳的秒数", "gauge");
  double now_ms = static_cast<double>(snap.at_ns) / 1e6;
  for (const auto& n : snap.nodes) {
    auto base = std::vector<std::pair<std::string, std::string>>{
        {"node", n.node_id}, {"domain", n.domain}};
    sample(os, "ddsmon_node_alive", base, n.alive ? 1.0 : 0.0);
    sample(os, "ddsmon_node_cpu_percent", base, n.cpu);
    sample(os, "ddsmon_node_mem_mb", base, n.mem);
    double age = n.last_seen_ms > 0 ? (now_ms - static_cast<double>(n.last_seen_ms)) / 1e3 : 0.0;
    sample(os, "ddsmon_node_heartbeat_age_seconds", base, age < 0 ? 0.0 : age);
  }

  return os.str();
}

struct MetricsServer::Impl {
  int fd{-1};
  int port{0};
  std::atomic<bool> running{false};
  std::atomic<bool> started{false};
  std::thread th;
  std::mutex mu;
  std::string body;
};

MetricsServer::MetricsServer() : impl_(new Impl) {}

MetricsServer::~MetricsServer() { stop(); }

int MetricsServer::port() const { return impl_->port; }
bool MetricsServer::running() const { return impl_->running.load(); }

bool MetricsServer::start(const std::string& addr, int port, std::string* err)
{
  if (impl_->started.load()) {
    if (err) *err = "metrics server 已启动";
    return false;
  }
  int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    if (err) *err = std::string("socket 创建失败: ") + std::strerror(errno);
    return false;
  }
  int one = 1;
  ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  sockaddr_in sa{};
  sa.sin_family = AF_INET;
  sa.sin_port = htons(static_cast<uint16_t>(port));
  if (::inet_pton(AF_INET, addr.c_str(), &sa.sin_addr) != 1) {
    ::close(fd);
    if (err) *err = "非法监听地址: " + addr;
    return false;
  }
  if (::bind(fd, reinterpret_cast<sockaddr*>(&sa), sizeof(sa)) != 0) {
    std::string msg = std::string("bind ") + addr + ":" + std::to_string(port) + " 失败: " +
                      std::strerror(errno);
    ::close(fd);
    if (err) *err = msg;
    return false;
  }
  if (::listen(fd, 16) != 0) {
    if (err) *err = std::string("listen 失败: ") + std::strerror(errno);
    ::close(fd);
    return false;
  }
  impl_->fd = fd;
  impl_->port = port;
  impl_->running.store(true);
  impl_->started.store(true);

  MetricsServer::Impl* p = impl_.get();
  impl_->th = std::thread([p]() {
    while (p->running.load()) {
      sockaddr_in ca{};
      socklen_t cl = sizeof(ca);
      int cfd = ::accept(p->fd, reinterpret_cast<sockaddr*>(&ca), &cl);
      if (cfd < 0) {
        if (!p->running.load()) break;
        continue;
      }
      char buf[2048] = {0};
      ssize_t n = ::recv(cfd, buf, sizeof(buf) - 1, 0);
      std::string req = n > 0 ? std::string(buf, static_cast<size_t>(n)) : std::string();
      size_t sp1 = req.find(' ');
      std::string method = sp1 == std::string::npos ? "" : req.substr(0, sp1);
      size_t sp2 = sp1 == std::string::npos ? std::string::npos : req.find(' ', sp1 + 1);
      std::string path = (sp1 == std::string::npos || sp2 == std::string::npos)
                             ? ""
                             : req.substr(sp1 + 1, sp2 - sp1 - 1);

      std::string body;
      std::string status = "404 Not Found";
      const std::string ctype = "text/plain; version=0.0.4; charset=utf-8";
      if (method == "GET" && path == "/metrics") {
        std::lock_guard<std::mutex> lk(p->mu);
        body = p->body;
        status = "200 OK";
      } else if (method == "GET" && path == "/healthz") {
        body = "ok\n";
        status = "200 OK";
      } else {
        body = "ddsmon metrics server: 可用路径 /metrics , /healthz\n";
      }
      std::string resp = "HTTP/1.1 " + status + "\r\nContent-Type: " + ctype +
                         "\r\nContent-Length: " + std::to_string(body.size()) +
                         "\r\nConnection: close\r\n\r\n" + body;
      size_t sent = 0;
      while (sent < resp.size()) {
        ssize_t w = ::send(cfd, resp.data() + sent, resp.size() - sent, MSG_NOSIGNAL);
        if (w <= 0) break;
        sent += static_cast<size_t>(w);
      }
      ::close(cfd);
    }
  });
  return true;
}

void MetricsServer::update(std::string body)
{
  std::lock_guard<std::mutex> lk(impl_->mu);
  impl_->body = std::move(body);
}

void MetricsServer::stop()
{
  if (!impl_->started.exchange(false)) return;
  impl_->running.store(false);
  if (impl_->fd >= 0) {
    ::shutdown(impl_->fd, SHUT_RDWR);
    ::close(impl_->fd);
    impl_->fd = -1;
  }
  if (impl_->th.joinable()) impl_->th.join();
}

}  // namespace ddsmon
