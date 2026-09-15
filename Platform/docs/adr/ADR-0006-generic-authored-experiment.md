# ADR-0006：通用 authored Experiment 执行定义

- 状态：Accepted for P0-EXP-01
- 日期：2026-09-15

普通 authored run 由不可变 Scenario、Experiment、Environment 与 Trajectory 引用形成
`RunExecutionDefinition`，Worker 只执行该通用定义。Acceptance4Node/Extended6Node 是显式
preset，它们先转换为同一种定义，不再构成 Runtime 的隐式默认语义。

当前正式可执行协议能力只包括 DirectToFusionCenter、ShortestPath 与配置式 TDMA。
Static、Pure ALOHA、FDMA、Basic CSMA/CA 尚无完整执行器，因此不得作为可运行选项发布。
后续实现必须先形成 planning/runtime 能力和回归门禁，再扩展 Experiment 枚举与 UI。

Workload 明确区分 BearingFusion 与 CommunicationOnly。前者生成检测特征、网络传输并在
有效交付后融合；后者只生成确定性普通网络 payload，不解码为特征、不产生融合结果，也不
生成 AcceptanceReport。没有 Fusion 对 CommunicationOnly 是正常结果。

正式 authored Scenario 使用 `TrajectoryDriven`，运行时只查询绑定轨迹；
`ConstantVelocity` 仅保留在 Acceptance preset 兼容适配器中作为离线轨迹生成输入。
ns-3 仍是唯一仿真时间和事件调度权威。
