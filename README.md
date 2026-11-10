# ddsmon —— 三域 DDS 通信诊断工具

面向车载/机器人 DDS 通信的**诊断**软件（不是简单的"看板"）：同时连上 **感知域（perception）**、
**规控域（planning）**、**监控诊断域（diag）** 三个 DDS domain，采集真实数据与 DDS 内置发现信息，
定位"话题不流、流得慢、流得抖、丢样本、端点消失、QoS 配不上、类型撞车、节点掉线、跨域链路超时"
这类问题，并输出可读报告；可选择把诊断结论写回监控诊断域，形成闭环。

- 语言/环境：C++17 + C11，Linux（WSL2 Ubuntu 已验证），Cyclone DDS 0.10.5（`idlc` 代码生成）
- 产物：终端彩色快照、Markdown 报告、JSON 报告、写回 DDS 的 `DiagReport` / `DiagEvent`
- 可视化：内置 Prometheus 指标端点（`/metrics`），配套 Grafana 看板（数据源/看板自动 provisioning）

---

## 1. 它能诊断什么

| 诊断项 | 数据来源 | 判定方式 |
|---|---|---|
| 话题频率 | 实际订阅样本 | 5s 滑窗实测 Hz 与配置 `expect_hz` 比较（低于 70% 警告，低于 30% 错误） |
| 到达抖动 | 同一发布者的相邻样本间隔 | 间隔与周期的偏差 p99 > 周期 x 0.5 报警 |
| 端到端时延 | 样本 `source_timestamp`（发布端写入时间） | recv - source 的 p50/p99/max |
| 丢包 / 丢样本 | 发布者 `seq` 连续性（按发布者分别统计） | 序号空洞 / 期望样本数 |
| deadline miss | 相邻样本间隔 > 1.5 周期 | 每发布者累计 |
| 发布者/订阅者存活 | DDS 内置 `DCPSPublication` / `DCPSSubscription` | 端点实例 ALIVE/DISPOSED |
| 参与者存活 | DDS 内置 `DCPSParticipant` | 同上，并按域统计 |
| 跨域链路时延（感知到规控） | `ObstacleList.source_stamp_ns`（透传的感知输出时间戳） | recv - origin 的 p50/p99 |
| QoS 兼容性（RxO） | 内置主题里的 writer/reader 完整 QoS | RELIABILITY / DURABILITY / DEADLINE / LIVELINESS / OWNERSHIP |
| 类型冲突 | 同话题上所有端点的 `type_name` | 出现多个类型则 CRITICAL；实际类型与配置不符也 CRITICAL |
| 节点心跳 | `rt/diag/heartbeat` 的 `Heartbeat` 样本 | 超过 lease（默认 3s）未更新则节点丢失 |
| 数据陈旧 | 最后样本到现在的时间 | 超过 2 个周期报警 |
| 健康总分 | 上述问题的加权扣分 | Critical -40 / Error -15 / Warn -5 / Info -1 |

问题代码（可直接对接 CI / 告警系统）：

```
DDS.DOMAIN.NO_PARTICIPANT        DDS.NODE.HEARTBEAT_LOST
DDS.TOPIC.NO_PUBLISHER           DDS.NODE.DEGRADED
DDS.TOPIC.NO_SUBSCRIBER          DDS.NODE.FAULT
DDS.TOPIC.RATE_LOW               DDS.CHAIN.LATENCY_HIGH
DDS.TOPIC.JITTER_HIGH            DDS.QOS.RELIABILITY_INCOMPATIBLE
DDS.TOPIC.LATENCY_HIGH           DDS.QOS.DURABILITY_INCOMPATIBLE
DDS.TOPIC.SAMPLE_LOSS            DDS.QOS.DEADLINE_INCOMPATIBLE
DDS.TOPIC.STALE                  DDS.QOS.LIVELINESS_INCOMPATIBLE
DDS.TOPIC.TYPE_MISMATCH          DDS.QOS.OWNERSHIP_INCOMPATIBLE
```

所有规则带 **debounce**（连续 N 个评估窗口成立才落问题）与启动宽限期（`no_publisher_sec`），
避免发现延迟导致的误报。

---

## 2. 快速开始

### 2.1 依赖

```bash
sudo apt-get install -y --no-install-recommends cyclonedds-dev cyclonedds-tools
# 校验：pkg-config --modversion CycloneDDS ; idlc -v
```

### 2.2 构建

```bash
cd ~/git/dds_monitoring
bash scripts/build.sh          # 等价于 cmake -S . -B build && cmake --build build -j && ctest
```

产物：`build/ddsmon`（诊断工具）、`build/ddsmon-sim`（三域模拟节点）。

### 2.3 先跑一遍（两个终端）

```bash
# 终端 A：起三域模拟节点（感知域 10 / 规控域 20 / 诊断域 30）
./build/ddsmon-sim -c config/ddsmon.json --node all

# 终端 B：诊断
./build/ddsmon snapshot -c config/ddsmon.json -d 6                    # 终端快照
./build/ddsmon watch    -c config/ddsmon.json                         # 实时看板（Ctrl-C 退出）
./build/ddsmon report   -c config/ddsmon.json -d 10 -o reports        # Markdown + JSON
./build/ddsmon qos      -c config/ddsmon.json -d 4                    # 只看拓扑与 QoS 兼容性
./build/ddsmon daemon   -c config/ddsmon.json                         # 常驻诊断，结论写回 diag 域
```

一条命令跑完整链路并回到监控域：

```bash
bash scripts/e2e.sh
```

健康时的快照（实测）：

```
DDS 通信诊断快照  2026-11-05 21:09:43.455    健康分 100/100   问题 0

【域概览】
域              domain  参与者  发布者  订阅者    话题  健康分
perception          10       2       3       3       3     100
planning            20       1       4       4       4     100
diag                30       1       3       1       1     100

【话题指标】
perception  rt/perception/camera_front        1    1    30.0Hz      30     3.2ms     5.1ms   0.00%       OK
planning    rt/planning/control_cmd           1    1    50.0Hz      50     4.7ms     5.2ms   0.00%       OK

【跨域链路】
  感知 -> 规控  rt/perception/obstacle_list    p50   36.09 ms   p99   38.60 ms
```

### 2.4 Grafana 可视化

```bash
# 终端 A：被监控侧
./build/ddsmon-sim     -c config/ddsmon.json --node all -d 1800

# 终端 B：指标端点（Prometheus 文本格式，默认 9200；9100 常被 node_exporter 占用）
./build/ddsmon metrics -c config/ddsmon.json --metrics-port 9200

# 终端 C：装 Prometheus + Grafana 并自动配置数据源/看板（需要 sudo）
bash scripts/grafana.sh
```

打开 <http://localhost:3000/d/ddsmon-main>（账号 `admin` / `admin`），看板包含健康分、问题分级、
话题频率实测 vs 期望、时延 p50/p99、抖动、丢包率、跨域链路时延、QoS 不兼容、节点心跳年龄，
以及"当前活跃问题"表格。`daemon` 也可以直接带 `--metrics-port 9200` 同时回写诊断域与出指标。

指标清单与看板说明见 [`docs/grafana.md`](docs/grafana.md)。

本机实测（健康基线，`Prometheus -> Grafana` 全链路已跑通）：

```
Prometheus target  ddsmon  UP (127.0.0.1:9200, scrape 1s)
Grafana            13.3.0，`/api/health` OK
数据源 ddsmon-prom  "Successfully queried the Prometheus API."
看板 ddsmon-main    12 个 panel，`/api/ds/query` 返回 ddsmon_health_score = 100
实测指标            topics=8, camera_front 30Hz, trajectory 时延 p99 1.13ms,
                    感知->规控链路 p99 35.2ms
```

安装时的两个坑：Ubuntu 自带 `prometheus-node-exporter` 固定占 9100，所以指标端口用 9200；
官方 `apt.grafana.com` 在国内可能只有几十 KB/s，`scripts/grafana.sh` 支持
`GRAFANA_APT_MIRROR=https://mirrors.aliyun.com/grafana/apt` 走镜像源。

---

## 3. 命令行参考

### 3.1 `ddsmon`

```
ddsmon <子命令> [选项]

子命令
  snapshot            采集 --duration 秒后输出一次快照；有 ERROR 及以上时退出码 1
  watch               实时刷新（默认 1s），Ctrl-C 退出
  report              采集后写 reports/<prefix>.md 与 .json，并打印快照
  daemon              常驻：按 --report-interval 发布 DiagReport，并对问题出现/恢复发 DiagEvent
  metrics             常驻：只暴露 Prometheus 指标（/metrics），供 Prometheus 抓取 / Grafana 展示
  qos                 只列话题拓扑与 QoS 不兼容明细

选项
  -c, --config <path>        配置文件，默认 config/ddsmon.json
  -d, --duration <sec>       采集时长（snapshot/report/qos 默认 5；daemon/metrics 不传则常驻）
  -o, --out <dir>            report 输出目录，默认 reports
      --json <path>          snapshot 额外导出 JSON
      --interval <sec>       watch 刷新间隔，默认 1
      --report-interval <sec>  daemon 写回间隔，默认 5
      --metrics-port <port>  metrics/daemon 暴露指标的端口（metrics 默认 9200，daemon 默认关闭）
      --metrics-addr <addr>  指标监听地址，默认 127.0.0.1（跨机抓取用 0.0.0.0）
      --prefix <name>        报告文件名前缀
      --no-color             关闭 ANSI 颜色（输出非 TTY 时自动关闭）
```

退出码：`0` 健康或仅 WARN，`1` 存在 ERROR 或 CRITICAL，`2` 参数或配置错误。

### 3.2 `ddsmon-sim`

```
ddsmon-sim [选项]
  --node perception|planning|monitor|all     启动哪些节点（默认 all）
  -c, --config <path>                        域 id / 频率跟着配置走
  -f, --fault none|loss|stale|stall|qos|type|kill|all[,..]
  -d, --duration <sec>                       默认一直运行
      --rate-scale <k>                       频率倍率（压测用）
      --stale-ms <ms>                        stale 故障的时间戳回退量
```

故障语义：

| 故障 | 注入方式 | 期望被诊断出来 |
|---|---|---|
| `loss` | 相机每 5 帧丢 1 帧（seq 继续自增） | `DDS.TOPIC.SAMPLE_LOSS`（实测约 20%） |
| `stale` | 感知 `object_list` 时间戳回退 300ms | `DDS.TOPIC.LATENCY_HIGH` + `DDS.CHAIN.LATENCY_HIGH` |
| `stall` | `trajectory` 有发布者但完全不出数 | `DDS.TOPIC.STALE` / `DDS.TOPIC.RATE_LOW` |
| `qos` | `control_cmd` 用 BEST_EFFORT 发布，而控制器 RELIABLE 订阅 | `DDS.QOS.RELIABILITY_INCOMPATIBLE` |
| `type` | 另起 participant 用 `Trajectory` 类型发布到 `control_cmd` | `DDS.TOPIC.TYPE_MISMATCH`（CRITICAL） |
| `kill` | 停发 `lidar_top` | `DDS.TOPIC.NO_PUBLISHER` |

```bash
bash scripts/demo_faults.sh stale   # 也可以 loss / qos / type / kill / all
```

---

## 4. 配置文件

`config/ddsmon.json`，结构：

```jsonc
{
  "name": "adas_dds_diagnosis",
  "diag": {                       // 监控诊断域与保留话题
    "domain": "diag",
    "report_topic": "rt/diag/report",
    "event_topic": "rt/diag/event",
    "heartbeat_topic": "rt/diag/heartbeat"
  },
  "domains": [                    // 每个域 = domain_id + 被监控话题
    {
      "name": "perception", "domain_id": 10,
      "topics": [
        { "name": "rt/perception/camera_front", "type": "CameraFrame",
          "expect_hz": 30, "consumer_required": true, "critical": true }
      ]
    }
  ],
  "nodes": [                      // 期望在线的节点（心跳租期）
    { "node_id": "perception_node", "domain": "perception", "lease_sec": 3 }
  ],
  "rules": {                      // 全部阈值
    "rate_low_ratio": 0.7, "rate_critical_ratio": 0.3,
    "latency_warn_ms": 40, "latency_error_ms": 100,
    "jitter_warn_ratio": 0.5, "loss_warn": 0.01, "loss_error": 0.05,
    "stale_warn_periods": 2, "no_publisher_sec": 3, "no_consumer_sec": 3,
    "chain_warn_ms": 80, "chain_error_ms": 150, "heartbeat_lease_sec": 3,
    "debounce_windows": 2, "eval_interval_ms": 1000
  }
}
```

- `type` 必须能映射到内置类型表（`include/ddsmon/types.hpp` 的 `PayloadAdapter`）；
  新增业务类型时在 `idl/ddsmon.idl` 加 struct，并在适配器表补一行（见 `docs/design.md`）。
- `critical=false` 的话题只产生 WARN，不拉低关键路径。
- 心跳话题的 `expect_hz` 是所有节点合计（当前 3 节点 x 1Hz 约 3Hz）；抖动按单发布者分别计算，
  不会被多节点交错到达干扰。

---

## 5. 目录结构

```
dds_monitoring/
├── CMakeLists.txt              # idlc 代码生成 + 静态库 + 两个可执行 + ctest
├── idl/ddsmon.idl              # 三域 13 个 IDL 类型
├── config/ddsmon.json          # 域/话题/节点/规则阈值
├── include/ddsmon/             # 公共头
│   ├── util.hpp                # 时间/格式化/UTF-8 宽度对齐/GUID
│   ├── json.hpp                # 自研极简 JSON（DOM + 解析 + 序列化）
│   ├── types.hpp               # 类型适配器表（seq/stamp/origin 取值函数）
│   ├── config.hpp              # 配置加载与校验
│   ├── stats.hpp               # 频率/抖动/时延/丢包/跨域链路统计
│   ├── qos_model.hpp           # QoS 读取与 RxO 兼容性判定
│   ├── topology.hpp            # 内置主题 -> 参与者/端点拓扑
│   ├── monitor.hpp             # 每话题诊断 reader
│   ├── engine.hpp              # 多域引擎、快照汇总、健康分
│   ├── rules.hpp               # 规则与 debounce
│   ├── report.hpp              # 文本 / Markdown / JSON 三种输出
│   └── publisher.hpp           # 结论写回监控诊断域
├── src/                        # 对应实现 + cli.cpp + sim.cpp
├── tests/                      # test_json / test_stats / test_qos / test_rules
├── scripts/                    # build.sh / e2e.sh / demo_faults.sh
└── docs/design.md              # 设计说明与扩展点
```

---

## 6. 实现要点（踩过的坑）

1. **内置主题就是拓扑来源**：`DDS_BUILTIN_TOPIC_DCPSPARTICIPANT / DCPSPUBLICATION /
   DCPSSUBSCRIPTION` 直接当 topic 句柄建 reader，可拿到全网端点、话题名、类型名和完整 QoS；
   端点存活用 `sample_info.instance_state` 判断（DISPOSED 时从 map 移除）。
2. **GUID 必须用满 16 字节**做 map key。只取前 6 字节会让同进程内多个 participant/endpoint 的
   GUID 前缀碰撞，拓扑被静默合并成一个端点（本项目实测踩到：3 个 writer 只显示 1 个）。
3. **诊断 reader 用 BEST_EFFORT + VOLATILE + KEEP_LAST**：既能接 RELIABLE 也能接 BEST_EFFORT
   发布者，不会因为诊断自己订阅不上而"看不见问题"；QoS 冲突只能靠内置主题里 writer/reader 的
   QoS 对比判断，不能靠"是否收到数据"倒推。
4. **`dds_take` 的样本必须用 `dds_return_loan` 归还**，用 `dds_sample_free` 会 double free。
   自己 `dds_alloc` 的序列（发布报告时）才用 `dds_free`。
5. **到达时间就是轮询时间**：本工具按样本轮询（1ms tick），时延/抖动里含最多约 1ms 采集开销；
   对 100Hz 级话题足够，亚毫秒级同步精度需改为 waitset 驱动。
6. **频率统计的观测窗口**取 `min(5s, now - 首帧)`，否则进程刚启动时以满 5s 做分母会把 30Hz
   算成 12Hz，直接误报 `RATE_LOW`。
7. **多发布者话题**（如 3 个节点的心跳）的到达间隔、seq 空洞都按发布者分别统计，不能混在一条
   时间轴上算抖动。
8. **规则防抖**：`issue_key = code|domain|topic`，连续 `debounce_windows` 个评估窗口成立才落问题，
   条件不成立立即撤销；启动 `no_publisher_sec` 内不报"没有发布者"。
9. **零样本话题必须显式判定**：只看 `rate_hz`、`stale_ms` 会把"发布者在线但从不出数"（订阅端死锁、
   上游卡住）漏成健康；本项目对 `publishers > 0 && 从未收到样本` 同时落 `RATE_LOW`(ERROR) 与
   `STALE`(ERROR)，并把该话题状态列标红（`stall` 故障用例即验证此项）。

---

## 7. 已知限制

- 当前面向单机多域（同一主机上的多个 participant / 多进程）；跨主机诊断走 DDS 标准 RTPS 发现即可，
  但需保证组播可达或配置 cyclonedds 的 peer 列表。
- QoS 兼容性按 DDS RxO 规则做静态比对（RELIABILITY / DURABILITY / DEADLINE / LIVELINESS /
  OWNERSHIP），不判断 `PARTITION`、`LATENCY_BUDGET`、内容过滤表达式等更细的策略。
- 时延基于 `source_timestamp`，要求发布端用 `dds_write_ts` 打时间戳；发布端不打时只能显示 `-`。
- 拓扑刷新默认 200ms、规则评估 1s，适合秒级诊断，不适合逐帧级微基准。

---

## 8. 验证情况

- `ctest`：4 个单测（JSON 往返与错误输入、统计口径、QoS 兼容矩阵、规则与防抖）全部通过。
- 端到端：`scripts/e2e.sh` 起三域模拟，快照 100/100，生成 Markdown/JSON；daemon 写回后模拟器侧
  `monitor_node` 既能收到 `DiagReport`（`health=95 issues=1`），也能收到 `DiagEvent`
  （`DDS.TOPIC.NO_PUBLISHER ... 问题出现`），闭环成立。
- 故障注入：`loss / stale / stall / qos / type / kill` 六类故障均按预期报出对应问题代码，
  报告样例在 `reports/FAULT_*.md`（`E2E_basic.md` 为健康基线）。
- 实测数值（同机回环，WSL2）：30Hz 话题抖动 p99 约 1–3ms、端到端时延 p99 约 1.4ms、
  感知→规控整链 p50 约 34–36ms（含两段 30Hz 采样与调度）。
