# P0 Acceptance Scenario Baseline

本文件区分第三方验收硬指标与项目标准演示参数。右栏参数用于构造可重复的 Platform 场景，但不扩大或改写左栏验收口径。

| 第三方验收硬指标 | 项目标准演示参数 |
| --- | --- |
| 通信原理演示网络：3～4 nodes | `Acceptance4Node`：3 个移动探测节点 + 1 个固定融合中心，是第三方验收基线 |
| 通信速率：60 bit/s | `RateBasedTxPhy` 按 payload bytes × 8 计算 airtime；非整纳秒结果确定性向上取整 |
| 通信误码率：BER ≤ 1e-4 | `ber_requirement = 1e-4` 作为场景约束保存；S3-02 不伪造 BER 测量值，指标计算留给后续 PHY/metrics 集成 |
| 分布式探测：特征级融合 | S3-02 只实现网络运行场景；特征级融合留 P0-S3-03 |
| 方位观测：≥ 5 points | S3-02 不制造业务观测；方位点采集与验收留 P0-S3-03 |
| 完整探测周期：≤ 180 s | 标准场景 communication cycle 为 12 s；12-cycle 网络回归为 144 s。业务 fusion period 的最终验收留 P0-S3-03 |
| 3～4 nodes 是验收规模 | `Extended6Node`：5 个移动探测节点 + 1 个固定融合中心，只是项目扩展示范，不称为第三方硬指标 |

## 场景参数

- 移动探测节点以 5 km/h（`5000 / 3600 m/s`）做低速匀速水平运动；位置由 committed `SimTime` 差值通过既有 StateProjector 语义推进，不按 callback 次数累加固定距离。
- 固定融合中心通过 `NodeCapabilityProfile` 与 `ProtocolRole::kSink` 表达，不依赖特殊 NodeId。标准 fixture 位于 8 m 深度，移动节点位于不同深度。
- 初始几何为可解释的中心辐射布局，移动节点到融合中心的平均水平距离约 1 km。
- M3/M4 使用既有 directed topology、routing 与 planner 路径，正式逻辑链路为 mobile sensor → fusion center。
- MAC 为 TDMA。Slot duration 为 4 s，其中 maximum planned payload 为 15 bytes，在 60 bit/s 下 airtime 为 2 s，guard interval 为显式配置的 2 s。
- `Acceptance4Node` communication cycle 为 3 slots × 4 s = 12 s；`Extended6Node` 为 5 slots × 4 s = 20 s。
- `NetworkUpdateIntervalCycles = 10`。每个 PlanningCycle 仍是一个 communication cycle，并且每周期 Commit；M3 structure 与 applied M4 schedule 只在周期 1、11、21……更新，M4 candidate 仍每周期计算。
- Acoustic configuration：center frequency 25 kHz、source level 110 dB、occupied bandwidth 4 kHz、environment depth 75 m（标准允许范围 50～100 m）。Source-level 的最终验收 reference/calibration 仍为 TBD；runtime metadata 继续遵守既有 TxEmission contract。
- `EnvironmentAssetId` 已在场景中保留；当前 synthetic field ID 可由 assembly 替换为后续验证过的黄海/南海 package，而无需修改 runtime hot path。

## Runtime architecture note

M1 是 Platform 对 ns-3 `Simulator` time 和 event scheduling 的封装，不是替代 scheduler。Acceptance runtime 仍通过 `Ns3KernelGateway`、`EventDispatcher`、`PlanInstaller` 和 `CycleCoordinator` 安装并执行所有 simulation-time event；双时间尺度只决定某周期是否更新 applied network structure/schedule。实现没有新增 fixed-step loop、自定义 EventQueue 或第二 simulation clock。

12-cycle production integration 固定验证：12 次 World Commit、12 次 M4 Candidate Build、2 次 M3 formal refresh、2 次 applied schedule update；周期 1 的 schedule 用于周期 1～10，周期 11 更新并由周期 12 复用。测试同时验证移动、固定 sink、Transmission/ChannelOutcome/Reception/CycleCommit trace 计数，以及 no-arrival 不进入 decode/reception 但不阻止运动与 Commit。
