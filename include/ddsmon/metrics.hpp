#pragma once

#include <memory>
#include <string>

namespace ddsmon {

struct Config;
struct Snapshot;

/// 把一次诊断快照渲染成 Prometheus 文本格式（text/plain; version=0.0.4）
std::string render_prometheus(const Snapshot& snap, const Config& cfg);

/// 极简 HTTP 服务：只提供 GET /metrics 和 GET /healthz，供 Prometheus 抓取。
/// 单线程串行处理连接，足够支撑 1s 级抓取间隔。
class MetricsServer {
 public:
  MetricsServer();
  ~MetricsServer();

  /// addr 传 127.0.0.1 只监听本机；需要跨机抓取时传 0.0.0.0
  bool start(const std::string& addr, int port, std::string* err);
  void update(std::string body);
  void stop();

  int port() const;
  bool running() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace ddsmon
