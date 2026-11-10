# Grafana 可视化

`ddsmon` 自己不做长期存储，它把每次诊断评估的结果直接渲染成 **Prometheus 文本格式**，
由 Prometheus 每 1s 抓一次，Grafana 只负责查询与展示。整条链路没有第三方中间件：

```
ddsmon metrics (127.0.0.1:9200/metrics)
        │  scrape 1s
        ▼
   Prometheus (127.0.0.1:9090)
        │  PromQL
        ▼
   Grafana (127.0.0.1:3000)  ── 看板 uid: ddsmon-main
```

## 0. 本机验证记录（已跑通）

```
Prometheus target ddsmon     UP，scrape 间隔 1s
Grafana                     13.3.0，/api/health {"database":"ok"}
数据源 ddsmon-prom          "Successfully queried the Prometheus API."
看板 ddsmon-main            "DDS 三域通信诊断" 12 panels
Grafana /api/ds/query       ddsmon_health_score -> 100（instance=127.0.0.1:9200）
Prometheus 查询             topics=8；camera_front 30Hz；
                            trajectory 时延 p99 1.13ms；感知->规控链路 p99 35.2ms
```

只要 `ddsmon metrics` 没在跑，target 就会掉成 `down`（`connection refused`），
看板所有曲线为空 —— 这是预期行为，不是配置错误。

## 1. 启动

```bash
# 1) 被监控侧（模拟器或真实业务进程）
./build/ddsmon-sim -c config/ddsmon.json --node all -d 1800 &

# 2) 指标端点（默认 9200；9100 通常被 node_exporter 占用）
./build/ddsmon metrics -c config/ddsmon.json --metrics-port 9200 &

# 3) 一次性安装与配置（需要 sudo；自动装 prometheus / grafana、写抓取配置、装数据源与看板）
bash scripts/grafana.sh
```

打开 <http://localhost:3000/d/ddsmon-main>，默认账号 `admin` / `admin`。

只想要"诊断 + 闭环 + 看板"三合一，可以只用守护进程：

```bash
./build/ddsmon daemon -c config/ddsmon.json --metrics-port 9200 --report-interval 2
```

`daemon` 会同时：
- 每 1s 刷新指标（Grafana 用）；
- 每 2s 把 `DiagReport` 写回 `diag` 域；
- 问题出现/恢复时发 `DiagEvent`。

## 2. 指标清单

| 指标 | 类型 | 标签 | 含义 |
|---|---|---|---|
| `ddsmon_health_score` | gauge | — | 综合健康分 0–100 |
| `ddsmon_issues_by_severity` | gauge | `severity` | 活跃问题数（CRITICAL/ERROR/WARN/INFO） |
| `ddsmon_issue_active` | gauge | `code,severity,domain,topic,detail` | 单条活跃问题（值为 1，用于表格） |
| `ddsmon_topics_monitored` | gauge | — | 被监控话题数 |
| `ddsmon_domain_participants` | gauge | `domain,domain_id` | 域内 participant 数 |
| `ddsmon_domain_publishers` / `ddsmon_domain_subscribers` | gauge | 同上 | 域内 writer / reader 数 |
| `ddsmon_domain_health` | gauge | 同上 | 域健康分 |
| `ddsmon_topic_rate_hz` | gauge | `domain,topic,type` | 实测频率 |
| `ddsmon_topic_rate_expected_hz` | gauge | 同上 | 期望频率（配置值，画基准线） |
| `ddsmon_topic_jitter_p99_ms` | gauge | 同上 | 到达间隔抖动 p99 |
| `ddsmon_topic_latency_p50_ms` / `_p99_ms` | gauge | 同上 | 端到端时延（source_timestamp 差） |
| `ddsmon_topic_loss_ratio` | gauge | 同上 | 丢样本率 0–1 |
| `ddsmon_topic_stale_ms` | gauge | 同上 | 距最近一帧的时间 |
| `ddsmon_topic_received_total` | gauge | 同上 | 观测窗口内样本数 |
| `ddsmon_topic_deadline_miss_total` | gauge | 同上 | deadline miss 次数 |
| `ddsmon_topic_publishers` / `ddsmon_topic_subscribers` | gauge | 同上 | 话题端点数量 |
| `ddsmon_topic_status` | gauge | 同上 | 0=OK 1=INFO 2=WARN 3=ERROR 4=CRITICAL |
| `ddsmon_qos_incompatible_pairs` | gauge | — | 不兼容端点对数 |
| `ddsmon_qos_incompatible` | gauge | `domain,topic,policy` | 具体 QoS 冲突（值 1） |
| `ddsmon_chain_latency_p50_ms` / `_p99_ms` | gauge | `chain,topic,domain` | 跨域链路（感知→规控）时延 |
| `ddsmon_chain_samples` | gauge | 同上 | 链路样本数 |
| `ddsmon_node_alive` | gauge | `node,domain` | 心跳存活（1/0） |
| `ddsmon_node_cpu_percent` / `_mem_mb` | gauge | 同上 | 节点自报 CPU / 内存 |
| `ddsmon_node_heartbeat_age_seconds` | gauge | 同上 | 距最近心跳秒数 |
| `ddsmon_snapshot_timestamp_seconds` | gauge | — | 最近一次评估时间戳 |
| `ddsmon_up` | gauge | — | 进程存活（进程退出即抓取失败） |

> 指标名里 `_total` 的几项实际是"当前窗口的快照值"，不是单调累加，只是沿用 Prometheus 习惯命名。

## 3. 看板面板

| 面板 | 数据 |
|---|---|
| 健康分 | `ddsmon_health_score`，≥95 绿 / ≥80 橙 / 否则红 |
| 活跃问题（按级别） | `ddsmon_issues_by_severity` 四级别 |
| 在线节点 | `sum(ddsmon_node_alive)`，3 节点为满 |
| QoS 不兼容端点数 | `ddsmon_qos_incompatible_pairs` |
| 监控话题数 | `ddsmon_topics_monitored` |
| 话题频率：实测 vs 期望 | `ddsmon_topic_rate_hz` / `ddsmon_topic_rate_expected_hz` |
| 端到端时延 p99 / p50 | 阈值线 40ms / 100ms 对应规则配置 |
| 到达抖动 p99 | `ddsmon_topic_jitter_p99_ms` |
| 样本丢失率 | `ddsmon_topic_loss_ratio`（1% / 5% 阈值） |
| 跨域链路时延 | `ddsmon_chain_latency_p99_ms`，阈值 80 / 150ms |
| 节点心跳年龄 | `ddsmon_node_heartbeat_age_seconds`，3s / 6s 阈值 |
| 当前活跃问题 | `ddsmon_issue_active` 即时查询成表，列出代码/级别/域/话题/说明 |

看板顶部有 `域` / `话题` 两个变量，支持多选与 All（All 展开为 `.*` 正则）。

## 4. 故障演示怎么看

```bash
# 起一个带故障的模拟器（例如 stall：trajectory 有发布者但完全不出数）
./build/ddsmon-sim -c config/ddsmon.json --node all -f stall -d 1800 &
./build/ddsmon metrics -c config/ddsmon.json --metrics-port 9200 &
```

看板上应在 1–2 个评估周期内看到：
- 健康分下降；
- "活跃问题（按级别）"出现 ERROR；
- `rt/planning/trajectory` 的频率曲线掉到 0（期望线仍在 10Hz）；
- "当前活跃问题"表出现 `DDS.TOPIC.RATE_LOW` 与 `DDS.TOPIC.STALE`。

其它故障（`loss` / `stale` / `qos` / `type` / `kill`）分别对应丢包率、时延、QoS 端点数、
话题数不变但出现类型冲突、节点/端点消失等面板变化。

## 5. 配置与排错

| 项 | 位置 |
|---|---|
| Prometheus 抓取配置 | 仓库 `config/prometheus.yml` → 系统 `/etc/prometheus/prometheus.yml` |
| Grafana 数据源 | `grafana/provisioning/datasources/ddsmon.yaml` → `/etc/grafana/provisioning/datasources/` |
| 看板 provider | `grafana/provisioning/dashboards/ddsmon.yaml` → `/etc/grafana/provisioning/dashboards/` |
| 看板 JSON | `grafana/dashboards/ddsmon.json` → `/var/lib/grafana/dashboards/` |

常见问题：

- **安装 Grafana 卡在下载**：官方 `apt.grafana.com` 在国内可能只有几十 KB/s（397MB 装不完）。
  用镜像源 `GRAFANA_APT_BASE=https://mirrors.aliyun.com/grafana/apt bash scripts/grafana.sh`，
  或 `GRAFANA_DEB_URL=<deb 地址> bash scripts/grafana.sh` 直接 dpkg 安装（实测镜像 17MB/s）。
  注意 deb 不要下到 `/tmp`：WSL 的 `/tmp` 是 tmpfs，实例重启会清掉。
- **端口 9100 被占用**：Ubuntu 装 `prometheus` 时会带 `prometheus-node-exporter`（固定占 9100）。
  所以 `ddsmon` 默认用 **9200**；如需改端口，`--metrics-port` 与 `config/prometheus.yml` 同步改。
- **看板无数据**：先看 <http://localhost:9090/targets>，`ddsmon` job 必须是 `UP`；
  再确认 `curl http://127.0.0.1:9200/metrics` 有输出（说明 `ddsmon metrics/daemon` 在跑）。
- **Grafana 打开是空的**：`sudo systemctl status grafana-server`，日志
  `sudo journalctl -u grafana-server -n 50`；provisioning 有语法错误时 Grafana 会拒绝启动。
- **跨机抓取**：`--metrics-addr 0.0.0.0`，并在 Prometheus 的 targets 里改成该主机地址；
  Grafana 的数据源 URL 同步指向 Prometheus。
- **改了看板想保留**：`allowUiUpdates: true`，在 UI 里改完可从
  `Share → Export → Save to file` 覆盖仓库里的 `grafana/dashboards/ddsmon.json`。

## 6. 替换左上角 logo

Grafana OSS 没有 white-labeling 配置项，header 图标是打包进 `assets-manifest.json` 的静态 SVG，
而且 URL 带 immutable 缓存、入口 bundle 还带 SRI 校验。所以改法不是"覆盖文件"这么简单：

```bash
bash scripts/branding.sh     # 一步完成替换，之后正常刷新看板即可
```

脚本做的事：

1. 用自制 SVG 覆盖 `/usr/share/grafana/public/build/static/img/grafana_icon.<hash>.svg`
   （老缓存过期后也正确）；
2. 生成带内容指纹的新 SVG（`grafana_icon.dds<sha8>.svg`），绕过 immutable 缓存；
3. 改写入口 bundle 里对图标的引用，把 `app.<hash>.js` 另存为 `app.dds<sha16>.js`，
   **重算 sha384 SRI** 并更新 `assets-manifest.json`（只改名不重算 SRI 会被浏览器拒载）。

配套文件与备份：

- 图标源文件：`grafana/branding/ddsmon_icon.svg`（三节点 = 感知/规控/诊断三域，
  连线 = DDS 通信，底部折线 = 诊断波形）
- 原文件备份：`/var/backups/grafana-branding/grafana_icon.8da070fb.pristine.svg` 是原始图标；
  仓库 `grafana/branding/backup/grafana_icon.8da070fb.orig.svg` 另存一份
- 还原最稳的方式：重新安装同一个 deb 包（`sudo dpkg -i grafana_<ver>_amd64.deb` 会还原
  `public/` 与 `assets-manifest.json`），再重启 Grafana；只还原图标文件不够，
  因为 manifest 里的入口 bundle 名字与 SRI 也被改过
- **升级 Grafana 会重置 public 目录与 manifest**，升级后重跑 `bash scripts/branding.sh` 即可
- 登录页的大字 logo 与页头用的是**同一个** `grafana_icon` 资源，不需要单独改；
  登录页背景渐变大图是 `g8_login_dark.svg` / `g8_login_light.svg`（本脚本不动）
- 登录页标题文案在共享 chunk 里（当前文件名 `18631.dds1adb7797d269d381.js`，原
  `18631.e012edea7c7c9657.js`）：`LoginTitle="Welcome to Grafana"`
  → `LoginTitle="Welcome to DdsMon"`，由 `scripts/branding.sh` 第 4 步自动改写并更新 manifest；
  想换成别的文案，改脚本里的 `new` 字符串即可
- 字标文件 `grafana_text_logo_dark.svg` / `grafana_text_logo_light.svg` 目前登录页没有用到，
  需要时可同样覆盖


