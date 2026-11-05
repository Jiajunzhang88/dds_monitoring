# DDS 通信诊断报告

| 项目 | 内容 |
|---|---|
| 采集时间 | 2026-11-05 21:47:35.081 |
| 采集时长 | 10.0 s |
| 配置 | adas_dds_diagnosis |
| 健康分 | **60 / 100** |
| 问题统计 | CRITICAL 1 / ERROR 0 / WARN 0 / INFO 0 |

## 1. 域概览

| 域 | domain_id | 参与者 | 发布者 | 订阅者 | 话题数 | 健康分 |
|---|---|---|---|---|---|---|
| perception | 10 | 2 | 3 | 3 | 3 | 100 |
| planning | 20 | 2 | 5 | 4 | 4 | 60 |
| diag | 30 | 1 | 3 | 2 | 1 | 100 |

## 2. 话题指标

| 域 | 话题 | 类型 | Pub | Sub | 实测频率 | 期望 | 抖动 p99 | 时延 p99 | 丢失率 | deadline miss | 状态 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| perception | `rt/perception/camera_front` | CameraFrame | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.37 ms | 1.42 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/object_list` | ObjectList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.37 ms | 1.42 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/lidar_top` | LidarCloud | 1 | 1 | 10.0 Hz | 10.0 Hz | 1.58 ms | 1.43 ms | 0.00 % | 0 | OK |
| planning | `rt/perception/obstacle_list` | ObstacleList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.37 ms | 1.42 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/trajectory` | Trajectory | 1 | 1 | 10.0 Hz | 10.0 Hz | 1.58 ms | 1.43 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/control_cmd` | ControlCommand | 2 | 1 | 50.0 Hz | 50.0 Hz | 1.21 ms | 1.40 ms | 0.00 % | 0 | CRITICAL |
| planning | `rt/planning/vehicle_state` | VehicleState | 1 | 1 | 100.0 Hz | 100.0 Hz | 1.33 ms | 1.40 ms | 0.00 % | 0 | OK |
| diag | `rt/diag/heartbeat` | Heartbeat | 3 | 0 | 3.0 Hz | 1.0 Hz | 1.03 ms | 1.32 ms | 0.00 % | 0 | OK |

## 3. QoS 兼容性

全部发布者/订阅者 QoS 组合兼容（RELIABILITY / DURABILITY / DEADLINE / LIVELINESS / OWNERSHIP 均满足 RxO 约束）。

## 4. 跨域链路时延

| 链路 | 话题 | p50 | p99 | 样本数 |
|---|---|---|---|---|
| 感知 → 规控 | `rt/perception/obstacle_list` | 34.53 ms | 35.71 ms | 150 |

## 5. 节点心跳

| 节点 | 归属域 | 状态 | 最近心跳 | CPU | 内存 | 序号 |
|---|---|---|---|---|---|---|
| perception_node | perception | RUNNING | 2026-11-05 21:47:35.080 | 39.0% | 134.0 MB | 14 |
| planning_node | planning | RUNNING | 2026-11-05 21:47:35.080 | 39.0% | 134.0 MB | 14 |
| diag_node | diag | RUNNING | 2026-11-05 21:47:35.080 | 39.0% | 134.0 MB | 14 |

## 6. 问题清单

| 级别 | 代码 | 域 | 话题 | 说明 |
|---|---|---|---|---|
| CRITICAL | `DDS.TOPIC.TYPE_MISMATCH` | planning | `rt/planning/control_cmd` | 同一话题上出现多个类型：ddsmon::planning::ControlCommand / ddsmon::planning::Trajectory，端点无法互通 |

## 7. 结论与建议

- 存在 1 个 CRITICAL 问题（多为类型/拓扑冲突），相关端点之间无法建立通信，需优先修复。
- 修复顺序建议：类型冲突 → QoS 兼容性 → 发布者存活 → 频率/时延/丢包。
