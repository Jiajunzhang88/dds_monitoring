#include "ddsmon/report.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>

#include "ddsmon/json.hpp"
#include "ddsmon/types.hpp"
#include "ddsmon/util.hpp"

namespace ddsmon {

namespace {

int worst_severity_on_topic(const Snapshot& snap, const std::string& domain,
                            const std::string& topic)
{
  int worst = 0;
  for (const auto& i : snap.issues) {
    if (i.domain == domain && i.topic == topic) {
      worst = std::max(worst, static_cast<int>(i.severity));
    }
  }
  return worst;
}

std::string status_text(int severity, bool color)
{
  if (severity == 0) return color ? "\033[1;32mOK\033[0m" : "OK";
  std::string name = severity_name(severity);
  return color ? severity_color(severity) + name + "\033[0m" : name;
}

std::string count_by_severity(const Snapshot& snap, int severity)
{
  int n = 0;
  for (const auto& i : snap.issues) {
    if (static_cast<int>(i.severity) == severity) ++n;
  }
  return std::to_string(n);
}

}  // namespace

std::string render_health_line(const Snapshot& snap)
{
  std::ostringstream os;
  os << "健康分 " << pad_left(std::to_string(snap.health), 3) << "/100"
     << "   问题 " << snap.issues.size() << " (CRITICAL " << count_by_severity(snap, kSeverityCritical)
     << " / ERROR " << count_by_severity(snap, kSeverityError) << " / WARN "
     << count_by_severity(snap, kSeverityWarn) << ")";
  return os.str();
}

std::string render_text(const Snapshot& snap, const Config& cfg, bool color)
{
  std::ostringstream os;
  os << "DDS 通信诊断快照  " << wallclock_str(snap.at_ns) << "    " << render_health_line(snap)
     << "\n\n";

  os << "【域概览】\n";
  os << pad_right("域", 14) << pad_left("domain", 8) << pad_left("参与者", 8) << pad_left("发布者", 8)
     << pad_left("订阅者", 8) << pad_left("话题", 8) << pad_left("健康分", 8) << "\n";
  for (const auto& d : snap.domains) {
    os << pad_right(d.name, 14) << pad_left(std::to_string(d.domain_id), 8)
       << pad_left(std::to_string(d.participants), 8) << pad_left(std::to_string(d.writers), 8)
       << pad_left(std::to_string(d.readers), 8)
       << pad_left(std::to_string(d.declared_topics), 8) << pad_left(std::to_string(d.health), 8)
       << "\n";
  }

  os << "\n【话题指标】\n";
  os << pad_right("域", 12) << pad_right("话题", 30) << pad_left("Pub", 5) << pad_left("Sub", 5)
     << pad_left("频率", 10) << pad_left("期望", 8) << pad_left("抖动p99", 10)
     << pad_left("时延p99", 10) << pad_left("丢失", 8) << pad_left("状态", 9) << "\n";
  for (const auto& t : snap.topics) {
    std::string rate = (t.metrics.received > 0 || t.publishers > 0)
                           ? fmt_double(t.metrics.rate_hz, 1) + "Hz"
                           : "-";
    std::string jitter = t.metrics.jitter_ms > 0 ? fmt_double(t.metrics.jitter_ms, 1) + "ms" : "-";
    std::string lat =
        t.metrics.latency_p99_ms > 0 ? fmt_double(t.metrics.latency_p99_ms, 1) + "ms" : "-";
    std::string loss = t.metrics.expected_samples > 0
                           ? fmt_double(t.metrics.loss_ratio * 100.0, 2) + "%"
                           : "-";
    int sev = worst_severity_on_topic(snap, t.domain, t.topic);
    os << pad_right(t.domain, 12) << pad_right(ellipsize(t.topic, 29), 30)
       << pad_left(std::to_string(t.publishers), 5) << pad_left(std::to_string(t.subscribers), 5)
       << pad_left(rate, 10) << pad_left(fmt_double(t.expect_hz, 0), 8) << pad_left(jitter, 10)
       << pad_left(lat, 10) << pad_left(loss, 8) << pad_left(status_text(sev, color), 9) << "\n";
  }

  if (!snap.chains.empty()) {
    os << "\n【跨域链路】\n";
    for (const auto& c : snap.chains) {
      os << "  " << pad_right(c.name, 14) << pad_right(c.topic, 30)
         << " p50 " << pad_left(fmt_ms(c.p50_ms), 10) << "   p99 " << pad_left(fmt_ms(c.p99_ms), 10)
         << "\n";
    }
  }

  if (!snap.nodes.empty()) {
    os << "\n【节点心跳】\n";
    for (const auto& n : snap.nodes) {
      os << "  " << pad_right(n.node_id, 18) << pad_right(n.domain, 12)
         << pad_right(n.alive ? "ALIVE" : "LOST", 8) << " state=" << n.state
         << "  cpu=" << fmt_double(n.cpu, 1) << "%  mem=" << fmt_double(n.mem, 1) << "MB"
         << "  最近心跳=" << (n.last_seen_ms > 0 ? wallclock_str(n.last_seen_ms * 1000000LL) : "-")
         << "\n";
    }
  }

  os << "\n【活跃问题】\n";
  if (snap.issues.empty()) {
    os << "  无。所有被监控链路指标正常。\n";
  } else {
    for (const auto& i : snap.issues) {
      os << "  " << pad_left(status_text(static_cast<int>(i.severity), color), 9) << " "
         << pad_right(i.code, 34) << pad_right(i.domain + "/" + i.topic, 40) << " " << i.detail
         << "\n";
    }
  }
  return os.str();
}

std::string render_markdown(const Snapshot& snap, const Config& cfg, double duration_sec)
{
  std::ostringstream os;
  os << "# DDS 通信诊断报告\n\n";
  os << "| 项目 | 内容 |\n|---|---|\n";
  os << "| 采集时间 | " << wallclock_str(snap.at_ns) << " |\n";
  os << "| 采集时长 | " << fmt_double(duration_sec, 1) << " s |\n";
  os << "| 配置 | " << cfg.name << " |\n";
  os << "| 健康分 | **" << snap.health << " / 100** |\n";
  os << "| 问题统计 | CRITICAL " << count_by_severity(snap, kSeverityCritical) << " / ERROR "
     << count_by_severity(snap, kSeverityError) << " / WARN "
     << count_by_severity(snap, kSeverityWarn) << " / INFO "
     << count_by_severity(snap, kSeverityInfo) << " |\n\n";

  os << "## 1. 域概览\n\n";
  os << "| 域 | domain_id | 参与者 | 发布者 | 订阅者 | 话题数 | 健康分 |\n";
  os << "|---|---|---|---|---|---|---|\n";
  for (const auto& d : snap.domains) {
    os << "| " << d.name << " | " << d.domain_id << " | " << d.participants << " | " << d.writers
       << " | " << d.readers << " | " << d.declared_topics << " | " << d.health << " |\n";
  }
  os << "\n";

  os << "## 2. 话题指标\n\n";
  os << "| 域 | 话题 | 类型 | Pub | Sub | 实测频率 | 期望 | 抖动 p99 | 时延 p99 | 丢失率 | deadline miss | 状态 |\n";
  os << "|---|---|---|---|---|---|---|---|---|---|---|---|\n";
  for (const auto& t : snap.topics) {
    int sev = worst_severity_on_topic(snap, t.domain, t.topic);
    std::string jitter = t.metrics.jitter_ms > 0 ? fmt_double(t.metrics.jitter_ms, 2) + " ms" : "-";
    std::string lat = t.metrics.latency_p99_ms > 0
                          ? fmt_double(t.metrics.latency_p99_ms, 2) + " ms"
                          : "-";
    std::string loss = t.metrics.received > 0
                           ? fmt_double(t.metrics.loss_ratio * 100.0, 2) + " %"
                           : "-";
    os << "| " << t.domain << " | `" << t.topic << "` | " << t.type << " | " << t.publishers
       << " | " << t.subscribers << " | " << fmt_double(t.metrics.rate_hz, 1) << " Hz | "
       << fmt_double(t.expect_hz, 1) << " Hz | " << jitter << " | " << lat << " | " << loss
       << " | " << t.metrics.deadline_miss
       << " | " << (sev == 0 ? "OK" : severity_name(sev)) << " |\n";
  }
  os << "\n";

  os << "## 3. QoS 兼容性\n\n";
  if (snap.qos_checks.empty()) {
    os << "全部发布者/订阅者 QoS 组合兼容（RELIABILITY / DURABILITY / DEADLINE / LIVELINESS / OWNERSHIP 均满足 RxO 约束）。\n\n";
  } else {
    os << "| 域 | 话题 | 策略 | 不兼容说明 | 建议 |\n|---|---|---|---|---|\n";
    for (const auto& q : snap.qos_checks) {
      os << "| " << q.domain << " | `" << q.topic << "` | " << q.policy << " | " << q.detail
         << " | " << q.suggestion << " |\n";
    }
    os << "\n";
  }

  if (!snap.chains.empty()) {
    os << "## 4. 跨域链路时延\n\n";
    os << "| 链路 | 话题 | p50 | p99 | 样本数 |\n|---|---|---|---|---|\n";
    for (const auto& c : snap.chains) {
      os << "| " << c.name << " | `" << c.topic << "` | " << fmt_double(c.p50_ms, 2) << " ms | "
         << fmt_double(c.p99_ms, 2) << " ms | " << c.samples << " |\n";
    }
    os << "\n";
  }

  os << "## 5. 节点心跳\n\n";
  if (snap.nodes.empty()) {
    os << "未配置节点心跳。\n\n";
  } else {
    os << "| 节点 | 归属域 | 状态 | 最近心跳 | CPU | 内存 | 序号 |\n|---|---|---|---|---|---|---|\n";
    for (const auto& n : snap.nodes) {
      std::string st = n.alive ? (n.state >= 3 ? "FAULT" : (n.state == 2 ? "DEGRADED" : "RUNNING"))
                               : "LOST";
      os << "| " << n.node_id << " | " << n.domain << " | " << st << " | "
         << (n.last_seen_ms > 0 ? wallclock_str(n.last_seen_ms * 1000000LL) : "-") << " | "
         << fmt_double(n.cpu, 1) << "% | " << fmt_double(n.mem, 1) << " MB | " << n.seq << " |\n";
    }
    os << "\n";
  }

  os << "## 6. 问题清单\n\n";
  if (snap.issues.empty()) {
    os << "未发现问题。\n\n";
  } else {
    os << "| 级别 | 代码 | 域 | 话题 | 说明 |\n|---|---|---|---|---|\n";
    for (const auto& i : snap.issues) {
      os << "| " << severity_name(static_cast<int>(i.severity)) << " | `" << i.code << "` | "
         << i.domain << " | " << (i.topic.empty() ? "-" : "`" + i.topic + "`") << " | " << i.detail
         << " |\n";
    }
    os << "\n";
  }

  os << "## 7. 结论与建议\n\n";
  if (snap.issues.empty()) {
    os << "- 感知域、规控域、监控诊断域的发布/订阅拓扑完整，QoS 组合兼容，数据流频率与时延均在阈值内。\n";
    os << "- 建议保持当前配置，并将本报告纳入版本基线，便于后续对比。\n";
  } else {
    int crit = 0, err = 0, warn = 0;
    for (const auto& i : snap.issues) {
      if (static_cast<int>(i.severity) == kSeverityCritical) ++crit;
      if (static_cast<int>(i.severity) == kSeverityError) ++err;
      if (static_cast<int>(i.severity) == kSeverityWarn) ++warn;
    }
    if (crit > 0) {
      os << "- 存在 " << crit
         << " 个 CRITICAL 问题（多为类型/拓扑冲突），相关端点之间无法建立通信，需优先修复。\n";
    }
    if (err > 0) {
      os << "- 存在 " << err
         << " 个 ERROR 问题：数据流中断、QoS 不兼容或严重时延/丢包，会直接导致功能降级。\n";
    }
    if (warn > 0) {
      os << "- 存在 " << warn << " 个 WARN 问题：指标劣化趋势（抖动、轻微丢包、链路时延偏高），建议排查。\n";
    }
    os << "- 修复顺序建议：类型冲突 → QoS 兼容性 → 发布者存活 → 频率/时延/丢包。\n";
  }
  return os.str();
}

std::string render_json(const Snapshot& snap, const Config& cfg, double duration_sec)
{
  Json root = Json::object();
  root.set("schema", Json("ddsmon.report/1"));
  root.set("config", Json(cfg.name));
  root.set("generated_at", Json(wallclock_iso(snap.at_ns)));
  root.set("timestamp_ns", Json(static_cast<long long>(snap.at_ns)));
  root.set("duration_sec", Json(duration_sec));
  root.set("health_score", Json(snap.health));

  Json summary = Json::object();
  summary.set("critical", Json(std::stoi(count_by_severity(snap, kSeverityCritical))));
  summary.set("error", Json(std::stoi(count_by_severity(snap, kSeverityError))));
  summary.set("warn", Json(std::stoi(count_by_severity(snap, kSeverityWarn))));
  summary.set("info", Json(std::stoi(count_by_severity(snap, kSeverityInfo))));
  summary.set("total", Json(static_cast<int>(snap.issues.size())));
  root.set("summary", summary);

  Json domains = Json::array();
  for (const auto& d : snap.domains) {
    Json j = Json::object();
    j.set("name", Json(d.name));
    j.set("domain_id", Json(d.domain_id));
    j.set("participants", Json(d.participants));
    j.set("writers", Json(d.writers));
    j.set("readers", Json(d.readers));
    j.set("topics", Json(d.declared_topics));
    j.set("health", Json(d.health));
    domains.push(j);
  }
  root.set("domains", domains);

  Json topics = Json::array();
  for (const auto& t : snap.topics) {
    Json j = Json::object();
    j.set("domain", Json(t.domain));
    j.set("domain_id", Json(t.domain_id));
    j.set("topic", Json(t.topic));
    j.set("type", Json(t.type));
    j.set("publishers", Json(t.publishers));
    j.set("subscribers", Json(t.subscribers));
    j.set("expect_hz", Json(t.expect_hz));
    j.set("rate_hz", Json(t.metrics.rate_hz));
    j.set("jitter_ms", Json(t.metrics.jitter_ms));
    j.set("latency_p50_ms", Json(t.metrics.latency_p50_ms));
    j.set("latency_p99_ms", Json(t.metrics.latency_p99_ms));
    j.set("loss_ratio", Json(t.metrics.loss_ratio));
    j.set("received", Json(static_cast<long long>(t.metrics.received)));
    j.set("deadline_miss", Json(static_cast<int>(t.metrics.deadline_miss)));
    j.set("stale_ms", Json(t.metrics.stale_ms));
    int sev = worst_severity_on_topic(snap, t.domain, t.topic);
    j.set("status", Json(sev == 0 ? "OK" : severity_name(sev)));
    j.set("status_level", Json(sev));
    topics.push(j);
  }
  root.set("topics", topics);

  Json qos = Json::array();
  for (const auto& q : snap.qos_checks) {
    Json j = Json::object();
    j.set("domain", Json(q.domain));
    j.set("topic", Json(q.topic));
    j.set("policy", Json(q.policy));
    j.set("writer", Json(q.writer));
    j.set("reader", Json(q.reader));
    j.set("detail", Json(q.detail));
    j.set("suggestion", Json(q.suggestion));
    qos.push(j);
  }
  root.set("qos_violations", qos);

  Json chains = Json::array();
  for (const auto& c : snap.chains) {
    Json j = Json::object();
    j.set("name", Json(c.name));
    j.set("domain", Json(c.domain));
    j.set("topic", Json(c.topic));
    j.set("p50_ms", Json(c.p50_ms));
    j.set("p99_ms", Json(c.p99_ms));
    j.set("samples", Json(static_cast<long long>(c.samples)));
    chains.push(j);
  }
  root.set("chains", chains);

  Json nodes = Json::array();
  for (const auto& n : snap.nodes) {
    Json j = Json::object();
    j.set("node_id", Json(n.node_id));
    j.set("domain", Json(n.domain));
    j.set("alive", Json(n.alive));
    j.set("state", Json(n.state));
    j.set("cpu", Json(n.cpu));
    j.set("mem_mb", Json(n.mem));
    j.set("last_seen", Json(wallclock_iso(n.last_seen_ms > 0 ? n.last_seen_ms * 1000000LL : 0)));
    j.set("seq", Json(static_cast<long long>(n.seq)));
    nodes.push(j);
  }
  root.set("nodes", nodes);

  Json issues = Json::array();
  for (const auto& i : snap.issues) {
    Json j = Json::object();
    j.set("code", Json(i.code));
    j.set("severity", Json(severity_name(static_cast<int>(i.severity))));
    j.set("level", Json(static_cast<int>(i.severity)));
    j.set("domain", Json(i.domain));
    j.set("topic", Json(i.topic));
    j.set("detail", Json(i.detail));
    j.set("value", Json(i.value));
    j.set("threshold", Json(i.threshold));
    issues.push(j);
  }
  root.set("issues", issues);

  return root.dump(2);
}

bool write_text_file(const std::string& path, const std::string& content, std::string* err)
{
  std::ofstream out(path);
  if (!out) {
    if (err) *err = "cannot write file: " + path;
    return false;
  }
  out << content;
  out.close();
  if (!out) {
    if (err) *err = "write failed: " + path;
    return false;
  }
  return true;
}

}  // namespace ddsmon
