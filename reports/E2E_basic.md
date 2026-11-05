# DDS 通信诊断报告

| 项目 | 内容 |
|---|---|
| 采集时间 | 2026-11-05 21:46:19.831 |
| 采集时长 | 8.0 s |
| 配置 | adas_dds_diagnosis |
| 健康分 | **100 / 100** |
| 问题统计 | CRITICAL 0 / ERROR 0 / WARN 0 / INFO 0 |

## 1. 域概览

| 域 | domain_id | 参与者 | 发布者 | 订阅者 | 话题数 | 健康分 |
|---|---|---|---|---|---|---|
| perception | 10 | 2 | 3 | 3 | 3 | 100 |
| planning | 20 | 1 | 4 | 4 | 4 | 100 |
| diag | 30 | 1 | 3 | 2 | 1 | 100 |

## 2. 话题指标

| 域 | 话题 | 类型 | Pub | Sub | 实测频率 | 期望 | 抖动 p99 | 时延 p99 | 丢失率 | deadline miss | 状态 |
|---|---|---|---|---|---|---|---|---|---|---|---|
| perception | `rt/perception/camera_front` | CameraFrame | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.30 ms | 1.40 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/object_list` | ObjectList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.30 ms | 1.41 ms | 0.00 % | 0 | OK |
| perception | `rt/perception/lidar_top` | LidarCloud | 1 | 1 | 10.0 Hz | 10.0 Hz | 0.82 ms | 1.40 ms | 0.00 % | 0 | OK |
| planning | `rt/perception/obstacle_list` | ObstacleList | 1 | 1 | 30.0 Hz | 30.0 Hz | 1.30 ms | 1.41 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/trajectory` | Trajectory | 1 | 1 | 10.0 Hz | 10.0 Hz | 0.82 ms | 1.40 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/control_cmd` | ControlCommand | 1 | 1 | 50.0 Hz | 50.0 Hz | 1.33 ms | 1.39 ms | 0.00 % | 0 | OK |
| planning | `rt/planning/vehicle_state` | VehicleState | 1 | 1 | 100.0 Hz | 100.0 Hz | 1.56 ms | 1.39 ms | 0.00 % | 0 | OK |
| diag | `rt/diag/heartbeat` | Heartbeat | 3 | 0 | 3.0 Hz | 1.0 Hz | 0.73 ms | 1.34 ms | 0.00 % | 0 | OK |

## 3. QoS 兼容性

全部发布者/订阅者 QoS 组合兼容（RELIABILITY / DURABILITY / DEADLINE / LIVELINESS / OWNERSHIP 均满足 RxO 约束）。

## 4. 跨域链路时延

| 链路 | 话题 | p50 | p99 | 样本数 |
|---|---|---|---|---|
| 感知 → 规控 | `rt/perception/obstacle_list` | 34.46 ms | 35.52 ms | 150 |

## 5. 节点心跳

| 节点 | 归属域 | 状态 | 最近心跳 | CPU | 内存 | 序号 |
|---|---|---|---|---|---|---|
| perception_node | perception | RUNNING | 2026-11-05 21:46:19.806 | 43.0% | 138.0 MB | 18 |
| planning_node | planning | RUNNING | 2026-11-05 21:46:19.806 | 43.0% | 138.0 MB | 18 |
| diag_node | diag | RUNNING | 2026-11-05 21:46:19.806 | 43.0% | 138.0 MB | 18 |

## 6. 问题清单

未发现问题。

## 7. 结论与建议

- 感知域、规控域、监控诊断域的发布/订阅拓扑完整，QoS 组合兼容，数据流频率与时延均在阈值内。
- 建议保持当前配置，并将本报告纳入版本基线，便于后续对比。
