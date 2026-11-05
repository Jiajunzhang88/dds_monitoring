# DDS 通信诊断报告

| 项目 | 内容 |
|---|---|
| 采集时间 | 2026-11-05 21:47:22.039 |
| 采集时长 | 10.0 s |
| 配置 | adas_dds_diagnosis |
| 健康分 | **85 / 100** |
| 问题统计 | CRITICAL 0 / ERROR 1 / WARN 0 / INFO 0 |

## 1. 域概览

| 域 | domain_id | 参与者 | 发布者 | 订阅者 | 话题数 | 健康分 |
|---|---|---|---|---|---|---|
| perception | 10 | 2 | 3 | 3 | 3 | 100 |
| planning | 20 | 1 | 4 | 4 | 4 | 85 |
| diag | 30 | 1 | 3 | 2 | 1 | 100 |

## 2. 话题指标

| 域 | 话题 | 类型 | Pub | Sub | 实测频率 | 期望 | 抖动 p99 | 时延 p99 | 丢失率 | deadline miss | 状态 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| perception | `rt/perception/camera_front` | CameraFrame | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.48 ms | 1.40 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/object_list` | ObjectList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.48 ms | 1.40 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/lidar_top` | LidarCloud | 1 | 1 | 10.0 Hz | 10.0 Hz | 0.85 ms | 1.37 ms | 0.00 % | 0 | OK |
| planning | `rt/perception/obstacle_list` | ObstacleList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.48 ms | 1.40 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/trajectory` | Trajectory | 1 | 1 | 10.0 Hz | 10.0 Hz | 0.85 ms | 1.37 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/control_cmd` | ControlCommand | 1 | 1 | 50.0 Hz | 50.0 Hz | 1.27 ms | 1.38 ms | 0.00 % | 0 | ERROR |
| planning | `rt/planning/vehicle_state` | VehicleState | 1 | 1 | 100.0 Hz | 100.0 Hz | 1.53 ms | 1.38 ms | 0.00 % | 0 | OK |
| diag | `rt/diag/heartbeat` | Heartbeat | 3 | 0 | 3.0 Hz | 1.0 Hz | 0.21 ms | 1.20 ms | 0.00 % | 0 | OK |

## 3. QoS 兼容性

| 域 | 话题 | 策略 | 不兼容说明 | 建议 |
|---|---|---|---|---|
| planning | `rt/planning/control_cmd` | RELIABILITY | 订阅者请求 RELIABLE，发布者仅提供 BEST_EFFORT，两者无法匹配 | 发布者改为 RELIABLE，或订阅者改为 BEST_EFFORT |

## 4. 跨域链路时延

| 链路 | 话题 | p50 | p99 | 样本数 |
|---|---|---|---|---|
| 感知 → 规控 | `rt/perception/obstacle_list` | 34.47 ms | 35.42 ms | 150 |

## 5. 节点心跳

| 节点 | 归属域 | 状态 | 最近心跳 | CPU | 内存 | 序号 |
|---|---|---|---|---|---|---|
| perception_node | perception | RUNNING | 2026-11-05 21:47:22.039 | 39.0% | 134.0 MB | 14 |
| planning_node | planning | RUNNING | 2026-11-05 21:47:22.039 | 39.0% | 134.0 MB | 14 |
| diag_node | diag | RUNNING | 2026-11-05 21:47:22.039 | 39.0% | 134.0 MB | 14 |

## 6. 问题清单

| 级别 | 代码 | 域 | 话题 | 说明 |
|---|---|---|---|---|
| ERROR | `DDS.QOS.RELIABILITY_INCOMPATIBLE` | planning | `rt/planning/control_cmd` | 订阅者请求 RELIABLE，发布者仅提供 BEST_EFFORT，两者无法匹配（1 对端点不兼容；建议：发布者改为 RELIABLE，或订阅者改为 BEST_EFFORT） |

## 7. 结论与建议

- 存在 1 个 ERROR 问题：数据流中断、QoS 不兼容或严重时延/丢包，会直接导致功能降级。
- 修复顺序建议：类型冲突 → QoS 兼容性 → 发布者存活 → 频率/时延/丢包。
