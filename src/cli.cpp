#include <atomic>
#include <chrono>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <unistd.h>
#include <thread>

#include "ddsmon/config.hpp"
#include "ddsmon/engine.hpp"
#include "ddsmon/metrics.hpp"
#include "ddsmon/publisher.hpp"
#include "ddsmon/report.hpp"
#include "ddsmon/util.hpp"

namespace {

volatile std::sig_atomic_t g_stop = 0;
void on_signal(int) { g_stop = 1; }

struct Args {
  std::string cmd;
  std::string config = "config/ddsmon.json";
  std::string json_out;
  std::string out_dir = "reports";
  std::string prefix;
  double duration_sec = 5.0;
  bool duration_set = false;
  double interval_sec = 1.0;
  double report_interval_sec = 5.0;
  int metrics_port = 0;
  std::string metrics_addr = "127.0.0.1";
  bool color = true;
  bool verbose = false;
};

void usage()
{
  std::cout <<
      "ddsmon - 车载 DDS 通信诊断工具（感知域 / 规控域 / 监控诊断域）\n"
      "\n"
      "用法:\n"
      "  ddsmon snapshot [选项]          采集一段时间，输出一次诊断快照\n"
      "  ddsmon watch    [选项]          实时刷新看板（Ctrl-C 退出）\n"
      "  ddsmon report   [选项]         采集一段时间，生成 Markdown + JSON 报告\n"
      "  ddsmon daemon   [选项]          常驻诊断，持续把报告/事件写回监控诊断域\n"
      "  ddsmon metrics  [选项]          常驻诊断，只暴露 Prometheus 指标（供 Grafana）\n"
      "  ddsmon qos      [选项]          只做拓扑与 QoS 兼容性检查\n"
      "\n"
      "选项:\n"
      "  -c, --config <path>   配置文件（默认 config/ddsmon.json）\n"
      "  -d, --duration <sec>  采集时长（snapshot/report/qos，默认 5s）\n"
      "  -o, --out <dir>       报告输出目录（report，默认 reports）\n"
      "      --json <path>     snapshot 额外输出 JSON 文件\n"
      "      --interval <sec>  watch 刷新间隔（默认 1s）\n"
      "      --report-interval <sec> daemon 写回报告的间隔（默认 5s）\n"
      "      --metrics-port <port>   metrics/daemon 暴露 Prometheus 指标的端口（0=关闭）\n"
      "      --metrics-addr <addr>   指标监听地址（默认 127.0.0.1，跨机抓取用 0.0.0.0）\n"
      "      --no-color        关闭 ANSI 颜色\n"
      "  -h, --help            显示帮助\n";
}

bool parse_args(int argc, char** argv, Args* a)
{
  if (argc < 2) {
    usage();
    return false;
  }
  a->cmd = argv[1];
  if (a->cmd == "-h" || a->cmd == "--help" || a->cmd == "help") {
    usage();
    return false;
  }
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    auto next = [&](const char* name) -> std::string {
      if (i + 1 >= argc) {
        std::cerr << "缺少参数值: " << name << "\n";
        std::exit(2);
      }
      return argv[++i];
    };
    if (arg == "-c" || arg == "--config") a->config = next("--config");
    else if (arg == "-d" || arg == "--duration") {
      a->duration_sec = std::stod(next("--duration"));
      a->duration_set = true;
    }
    else if (arg == "-o" || arg == "--out") a->out_dir = next("--out");
    else if (arg == "--json") a->json_out = next("--json");
    else if (arg == "--interval") a->interval_sec = std::stod(next("--interval"));
    else if (arg == "--report-interval") a->report_interval_sec = std::stod(next("--report-interval"));
    else if (arg == "--metrics-port") a->metrics_port = std::stoi(next("--metrics-port"));
    else if (arg == "--metrics-addr") a->metrics_addr = next("--metrics-addr");
    else if (arg == "--prefix") a->prefix = next("--prefix");
    else if (arg == "--no-color") a->color = false;
    else if (arg == "-v" || arg == "--verbose") a->verbose = true;
    else if (arg == "-h" || arg == "--help") {
      usage();
      return false;
    } else {
      std::cerr << "未知参数: " << arg << "\n";
      return false;
    }
  }
  return true;
}

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

bool load_config(const Args& a, ddsmon::Config* cfg)
{
  std::string err;
  if (!ddsmon::Config::load(a.config, cfg, &err)) {
    std::cerr << "配置加载失败: " << err << "\n";
    return false;
  }
  return true;
}

int worst_level(const ddsmon::Snapshot& snap)
{
  int worst = 0;
  for (const auto& i : snap.issues) worst = std::max(worst, static_cast<int>(i.severity));
  return worst;
}

void collect(ddsmon::Engine& engine, double seconds)
{
  int64_t end = ddsmon::now_ns() + static_cast<int64_t>(seconds * 1e9);
  while (!g_stop) {
    engine.tick();
    if (ddsmon::now_ns() >= end) break;
    sleep_ms(1);
  }
  engine.refresh();
}

int cmd_snapshot(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }
  std::cerr << "采集中 (" << a.duration_sec << "s)...\n";
  collect(engine, a.duration_sec);

  const auto& snap = engine.snapshot();
  std::cout << ddsmon::render_text(snap, cfg, a.color);

  if (!a.json_out.empty()) {
    std::string err2;
    if (!ddsmon::write_text_file(a.json_out, ddsmon::render_json(snap, cfg, a.duration_sec), &err2)) {
      std::cerr << err2 << "\n";
      return 2;
    }
    std::cerr << "JSON 已写入 " << a.json_out << "\n";
  }
  return worst_level(snap) >= ddsmon::kSeverityError ? 1 : 0;
}

int cmd_watch(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }

  int64_t next_render = 0;
  while (!g_stop) {
    engine.tick();
    int64_t now = ddsmon::now_ns();
    if (now >= next_render) {
      if (a.color) std::cout << "\033[H\033[2J";
      std::cout << ddsmon::render_text(engine.snapshot(), cfg, a.color) << std::flush;
      next_render = now + static_cast<int64_t>(a.interval_sec * 1e9);
    }
    sleep_ms(1);
  }
  return worst_level(engine.snapshot()) >= ddsmon::kSeverityError ? 1 : 0;
}

int cmd_report(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }
  std::cerr << "采集中 (" << a.duration_sec << "s)...\n";
  collect(engine, a.duration_sec);

  const auto& snap = engine.snapshot();
  std::filesystem::create_directories(a.out_dir);

  std::string base = a.prefix;
  if (base.empty()) {
    base = ddsmon::make_report_id();
  }
  std::string md_path = (std::filesystem::path(a.out_dir) / (base + ".md")).string();
  std::string json_path = (std::filesystem::path(a.out_dir) / (base + ".json")).string();

  std::string werr;
  if (!ddsmon::write_text_file(md_path, ddsmon::render_markdown(snap, cfg, a.duration_sec), &werr)) {
    std::cerr << werr << "\n";
    return 2;
  }
  if (!ddsmon::write_text_file(json_path, ddsmon::render_json(snap, cfg, a.duration_sec), &werr)) {
    std::cerr << werr << "\n";
    return 2;
  }

  std::cout << ddsmon::render_text(snap, cfg, a.color);
  std::cout << "\n报告已生成:\n  " << md_path << "\n  " << json_path << "\n";
  return worst_level(snap) >= ddsmon::kSeverityError ? 1 : 0;
}

int cmd_qos(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }
  collect(engine, a.duration_sec);

  const auto& snap = engine.snapshot();
  std::cout << "DDS QoS 兼容性检查  " << ddsmon::wallclock_str(snap.at_ns) << "\n\n";
  std::cout << ddsmon::pad_right("域", 12) << ddsmon::pad_right("话题", 30)
            << ddsmon::pad_right("类型", 34) << ddsmon::pad_left("Pub", 5) << ddsmon::pad_left("Sub", 5)
            << "\n";
  for (const auto& t : snap.topics) {
    std::cout << ddsmon::pad_right(t.domain, 12) << ddsmon::pad_right(ddsmon::ellipsize(t.topic, 29), 30)
              << ddsmon::pad_right(ddsmon::ellipsize(t.type, 33), 34)
              << ddsmon::pad_left(std::to_string(t.publishers), 5)
              << ddsmon::pad_left(std::to_string(t.subscribers), 5) << "\n";
  }
  std::cout << "\n";
  if (snap.qos_checks.empty()) {
    std::cout << "结论: 各话题的 writer / reader QoS 组合均满足 DDS RxO 约束。\n";
  } else {
    std::cout << "发现 " << snap.qos_checks.size() << " 处 QoS 不兼容:\n";
    for (const auto& q : snap.qos_checks) {
      std::cout << "  [" << q.policy << "] " << q.domain << "/" << q.topic << "\n"
                << "    writer " << q.writer << " vs reader " << q.reader << "\n"
                << "    " << q.detail << "\n"
                << "    建议: " << q.suggestion << "\n";
    }
  }
  return snap.qos_checks.empty() ? 0 : 1;
}

int cmd_metrics(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }

  ddsmon::MetricsServer server;
  int port = a.metrics_port > 0 ? a.metrics_port : 9200;
  if (!server.start(a.metrics_addr, port, &err)) {
    std::cerr << "指标服务启动失败: " << err << "\n";
    return 2;
  }
  std::cerr << "Prometheus 指标已暴露: http://" << a.metrics_addr << ":" << port << "/metrics\n";

  int64_t next_eval = 0;
  int64_t hard_end = a.duration_set && a.duration_sec > 0
                         ? ddsmon::now_ns() + static_cast<int64_t>(a.duration_sec * 1e9)
                         : 0;
  int64_t eval_ns = static_cast<int64_t>(cfg.rules.eval_interval_ms) * 1000000LL;
  while (!g_stop) {
    engine.tick();
    int64_t now = ddsmon::now_ns();
    if (now >= next_eval) {
      engine.refresh();
      const auto& snap = engine.snapshot();
      server.update(ddsmon::render_prometheus(snap, cfg));
      std::cout << "[" << ddsmon::wallclock_str(now) << "] " << ddsmon::render_health_line(snap)
                << std::flush << "\n";
      next_eval = now + eval_ns;
    }
    if (hard_end > 0 && now >= hard_end) break;
    sleep_ms(1);
  }

  engine.refresh();
  server.update(ddsmon::render_prometheus(engine.snapshot(), cfg));
  server.stop();
  std::cerr << "指标服务已停止。\n";
  return 0;
}

int cmd_daemon(const Args& a)
{
  ddsmon::Config cfg;
  if (!load_config(a, &cfg)) return 2;

  ddsmon::Engine engine;
  std::string err;
  if (!engine.init(cfg, &err)) {
    std::cerr << "诊断引擎初始化失败: " << err << "\n";
    return 2;
  }

  ddsmon::DiagPublisher publisher;
  if (!publisher.init(cfg, &err)) {
    std::cerr << "监控域发布端初始化失败: " << err << "\n";
    return 2;
  }

  std::cerr << "诊断守护进程已启动，报告写回域: " << cfg.diag_domain << " / " << cfg.report_topic
            << "\n";

  ddsmon::MetricsServer server;
  bool metrics_on = a.metrics_port > 0;
  if (metrics_on) {
    if (!server.start(a.metrics_addr, a.metrics_port, &err)) {
      std::cerr << "指标服务启动失败: " << err << "\n";
      return 2;
    }
    std::cerr << "Prometheus 指标已暴露: http://" << a.metrics_addr << ":" << a.metrics_port
              << "/metrics\n";
  }

  std::map<std::string, ddsmon::Issue> known_issues;
  int64_t next_report = 0;
  int64_t next_eval = 0;
  int64_t eval_ns = static_cast<int64_t>(cfg.rules.eval_interval_ms) * 1000000LL;
  int64_t hard_end = a.duration_set && a.duration_sec > 0
                         ? ddsmon::now_ns() + static_cast<int64_t>(a.duration_sec * 1e9)
                         : 0;

  while (!g_stop) {
    engine.tick();
    int64_t now = ddsmon::now_ns();
    if (now >= next_eval) {
      engine.refresh();
      const auto& snap = engine.snapshot();
      if (metrics_on) server.update(ddsmon::render_prometheus(snap, cfg));

      if (now >= next_report) {
        publisher.publish_report(snap);

        // 问题出现 / 恢复 → 事件
        std::map<std::string, ddsmon::Issue> current;
        for (const auto& i : snap.issues) current[i.code + "|" + i.domain + "|" + i.topic] = i;
        for (const auto& kv : current) {
          if (known_issues.find(kv.first) == known_issues.end()) {
            publisher.publish_event(static_cast<int>(kv.second.severity), kv.second.code,
                                    kv.second.domain, kv.second.topic,
                                    "问题出现: " + kv.second.detail);
            std::cout << "  [事件] " << kv.second.code << " " << kv.second.domain << "/"
                      << kv.second.topic << " 出现\n";
          }
        }
        for (const auto& kv : known_issues) {
          if (current.find(kv.first) == current.end()) {
            publisher.publish_event(ddsmon::kSeverityInfo, kv.second.code, kv.second.domain,
                                    kv.second.topic, "问题恢复");
            std::cout << "  [事件] " << kv.second.code << " " << kv.second.domain << "/"
                      << kv.second.topic << " 恢复\n";
          }
        }
        known_issues = std::move(current);

        std::cout << "[" << ddsmon::wallclock_str(now) << "] " << ddsmon::render_health_line(snap)
                  << std::flush << "\n";
        next_report = now + static_cast<int64_t>(a.report_interval_sec * 1e9);
      }
      next_eval = now + eval_ns;
    }
    if (hard_end > 0 && now >= hard_end) break;
    sleep_ms(1);
  }

  engine.refresh();
  publisher.publish_report(engine.snapshot());
  if (metrics_on) {
    server.update(ddsmon::render_prometheus(engine.snapshot(), cfg));
    server.stop();
  }
  std::cerr << "守护进程退出，已发布最后一帧报告。\n";
  publisher.shutdown();
  return 0;
}

}  // namespace

int main(int argc, char** argv)
{
  std::signal(SIGINT, on_signal);
  std::signal(SIGTERM, on_signal);

  Args args;
  if (!parse_args(argc, argv, &args)) return 0;
  // 输出被重定向到文件/管道时不带 ANSI 颜色，避免日志里出现转义序列
  if (args.color && !isatty(STDOUT_FILENO)) args.color = false;

  if (args.cmd == "snapshot") return cmd_snapshot(args);
  if (args.cmd == "watch") return cmd_watch(args);
  if (args.cmd == "report") return cmd_report(args);
  if (args.cmd == "daemon") return cmd_daemon(args);
  if (args.cmd == "metrics") return cmd_metrics(args);
  if (args.cmd == "qos") return cmd_qos(args);

  std::cerr << "未知子命令: " << args.cmd << "\n\n";
  usage();
  return 2;
}
