# ddsmon 设计说明

## 1. 目标与边界

**目标**：在一条真实的 DDS 通信链路上（感知域 → 规控域，外加监控诊断域），回答三个问题：

1. 现在的数据到底流没流？流得对不对？（频率、抖动、时延、丢包、陈旧、端点存活）
2. 如果不对，是哪一环、什么原因？（QoS RxO 不兼容、类型冲突、跨域链路超时、节点掉线）
3. 结论能不能被机器消费？（Markdown / JSON / 写回 DDS 的 `DiagReport` 与 `DiagEvent`）

**边界**：单机多域、样本级诊断（秒级时间尺度）；不做逐帧微基准、不做 `PARTITION` /
`LATENCY_BUDGET` 等细粒度 QoS 策略比对；不做控制算法评估。

## 2. 分层结构

```
                    ┌─────────────────────────────┐
   IDL (idlc)  ───► │ ddsmon_types (C 生成代码)    │
                    └──────────────┬──────────────┘
                                   │
                    ┌──────────────▼──────────────┐
                    │ ddsmon_core (C++ 静态库)     │
                    │  util / json / types / config│
                    │  stats / qos_model / topology│
                    │  monitor / engine / rules    │
                    │  report / publisher          │
                    └──────────┬───────────┬──────┘
                               │           │
                     ┌─────────▼──┐   ┌────▼─────────┐
                     │ ddsmon CLI │   │ ddsmon-sim   │
                     │ snapshot   │   │ 三域模拟节点  │
                     │ watch      │   │ 六类故障注入  │
                     │ report     │   └──────────────┘
                     │ daemon     │
                     │ qos        │
                     └────────────┘
```

### 模块职责

| 模块 | 职责 | 关键点 |
|---|---|---|
| `util` | 时间戳、格式化、UTF-8 显示宽度对齐、GUID 字符串 | 中文按 2 列宽，终端表格才能对齐；GUID 必须用满 16 字节 |
| `json` | 极简 JSON DOM、解析（含 `//` 注释、`\uXXXX`）、序列化 | 不引第三方依赖，配置文件与报告共用 |
| `types` | `PayloadAdapter` 表：类型描述符 + 取 `seq` / `stamp` / `origin` 的函数指针 | 新增业务类型只需改 IDL + 加一行适配器 |
| `config` | JSON 配置加载与校验（domain_id 0–232、`expect_hz > 0`、类型必须存在） | 配置错误在启动期暴露，不带到运行期 |
| `stats` | 每话题滑窗统计：频率、按发布者的间隔/抖动、时延、seq 空洞、跨域 `origin` 时延 | 多发布者必须按 writer 分别算间隔与空洞 |
| `qos_model` | 读取内置主题 QoS，做 5 条 RxO 判定 | 未设置的策略按 DDS 默认值（RELIABLE / VOLATILE / SHARED / AUTOMATIC）解析 |
| `topology` | 内置主题 → 参与者 / 端点；存活判定 | `instance_state != ALIVE` 时从 map 移除 |
| `monitor` | 每个被监控话题一个诊断 reader | BEST_EFFORT + VOLATILE + KEEP_LAST(128) |
| `engine` | 每个域一个 participant + 3 个内置 reader + 各话题 monitor + 心跳 reader | 统计时排除自身端点；每域记录自身 GUID |
| `rules` | 把指标/拓扑翻译成 `Issue`，做 debounce 与启动宽限 | `issue_key = code\|domain\|topic` |
| `report` | 文本快照 / Markdown / JSON | JSON schema `ddsmon.report/1` |
| `publisher` | 把结论写回 diag 域 | RELIABLE + KEEP_LAST(8)，序列手工 `dds_alloc` |

## 3. 一次诊断的数据流

```
tick() 每 1ms：
  ① 各域 monitor.poll()          dds_take → SampleRec{recv_ns, src_ns, origin_ns, seq, writer}
                                 → TopicStats::add()（60s / 20000 条上限）
  ② 每 200ms 读三个内置主题      参与者 / 发布者 / 订阅者 → Topology（含 QoS）
  ③ 读心跳 reader                Heartbeat → Engine::nodes_
refresh() 每 eval_interval（1s）：
  ④ 每话题 metrics(now, 5s)      → TopicRow（含 types_seen）
  ⑤ QoS 交叉比对                 writers × readers（排除自身）→ QosCheckRow
  ⑥ 健康分 + RulesEngine
  ⑦ report / publisher           文本、Markdown、JSON、DiagReport
```

`topic.publishers / subscribers` 只统计**外部端点**（`participant_guid != 自身 GUID`），
否则诊断工具自己会被算成一个订阅者，`NO_SUBSCRIBER` 永远不会触发。

## 4. 关键设计决策

### 4.1 为什么用内置主题而不是只看"能不能收到"

"收到数据"只能证明匹配成功，无法区分"发布者不存在"、"QoS 不兼容"和"类型冲突"。内置主题给出
端点级事实（topic、type_name、完整 QoS、存活状态），使诊断可以做因果判断，而不是猜。

### 4.2 为什么诊断 reader 用 BEST_EFFORT

RELIABLE 的 reader 无法匹配 BEST_EFFORT 的 writer。若诊断 reader 用 RELIABLE，遇到 BE 发布者
就完全收不到数据，反而把"QoS 冲突"误判成"没有发布者"。用 BEST_EFFORT 可以两类都收，QoS 冲突
另外由内置主题的 QoS 比对报出。

### 4.3 跨域链路怎么算

`ObstacleList` 携带 `source_stamp_ns`（感知输出时间戳）。规控节点把感知 `ObjectList` 的
`stamp_ns` 透传进来，诊断侧对 `obstacle_list` 的样本计算 `recv_ns - source_stamp_ns`，即
"感知出帧 → 规控出障碍物列表"的整链时延（实测同机约 36ms p50，含两段 30Hz 采样与调度）。

### 4.4 健康分

`100 - (40×Critical + 15×Error + 5×Warn + 1×Info)`，下限 0。分数只用于人读，机器消费用
`issues[]` 与 `summary`。

### 4.5 防抖与宽限

- `apply()` 维护每个 `issue_key` 的连续命中计数，达到 `debounce_windows` 才写入 `issues`；
  条件不再成立立即清零（问题"恢复"由 daemon 对比上一轮 known_issues 发 `DiagEvent`）。
- 启动 `no_publisher_sec` 秒内不判"没有发布者/参与者/订阅者"，给 DDS 发现留时间。

## 5. 报告格式

### JSON（schema `ddsmon.report/1`）

```json
{
  "schema": "ddsmon.report/1",
  "config": "adas_dds_diagnosis",
  "generated_at": "2026-11-05T21:09:43.455+08:00",
  "duration_sec": 6,
  "health_score": 100,
  "summary": {"critical": 0, "error": 0, "warn": 0, "info": 0},
  "domains": [{"name": "perception", "domain_id": 10, "participants": 2, "writers": 3, ...}],
  "topics": [{
    "domain": "perception", "topic": "rt/perception/camera_front", "type": "CameraFrame",
    "publishers": 1, "subscribers": 1,
    "metrics": {"rate_hz": 30.0, "jitter_ms": 3.2, "latency_p99_ms": 5.1, "loss_ratio": 0},
    "qos_violations": []
  }],
  "chains": [{"name": "感知 → 规控", "topic": "rt/perception/obstacle_list", "p50_ms": 36.1, "p99_ms": 38.6}],
  "nodes": [{"node_id": "perception_node", "alive": true, "state": 1, "last_seen": "..."}],
  "issues": [{"severity": "WARN", "code": "DDS.TOPIC.JITTER_HIGH", "domain": "diag", "topic": "..."}]
}
```

### 写回监控诊断域

- `rt/diag/report`（`DiagReport`）：`report_id / stamp_ns / health_score / issue_total /
  critical_num / error_num / warn_num / issues[] / topics[]`，RELIABLE、KEEP_LAST(8)。
- `rt/diag/event`（`DiagEvent`）：问题出现/恢复时各发一条，带 `code / severity / domain /
  topic / message`。
- 闭环验证：`ddsmon-sim` 的 `monitor` 节点订阅 `rt/diag/report` 并打印
  `收到诊断报告 RPT_x health=.. issues=..`。

## 6. 扩展指南

### 6.1 新增业务类型

1. `idl/ddsmon.idl` 加 struct（放在对应 module 下），重新 `cmake --build`（idlc 会自动重生成）。
2. `include/ddsmon/types.hpp` 的 `PayloadAdapter` 增加一行：
   `{"MyType", "ddsmon::x::MyType", &ddsmon_x_MyType_desc, seq_of<...>, stamp_of<...>, nullptr}`；
   若该类型携带上游时间戳，把 `origin_of` 指向对应字段（跨域链路时延依赖它）。
3. 配置里 `"type": "MyType"` 即可被监控。

### 6.2 新增规则

在 `rules.cpp` 中构造 `Issue` 并调用 `apply(snap, cfg, now, issue, cond)`，`code` 保持
`DDS.<类别>.<含义>` 的命名，severity 用 `Severity::{Info,Warn,Error,Critical}`。
若需要新阈值，在 `RuleCfg` 与 `config/ddsmon.json` 同步加字段。

### 6.3 跨主机

当前每域一个 participant，走标准 RTPS 发现。跨主机场景下：

```bash
export CYCLONEDDS_URI='<CycloneDDS><Domain><Discovery><Peers><Peer address="192.168.1.20"/></Peers></Discovery></Domain></CycloneDDS>'
```

诊断端与被诊断端配置同样的 peer 列表即可，其余逻辑不变。

### 6.4 接入更多域

`Config` 的 `domains[]` 是数据驱动的：加一个域（domain_id + topics）即可，`Engine` 会为它创建
participant、内置 reader 与各话题 monitor；报告里自动多出一行域概览。
