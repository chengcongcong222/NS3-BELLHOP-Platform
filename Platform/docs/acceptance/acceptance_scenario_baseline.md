# P0 Acceptance Scenario Baseline

本文件区分第三方验收硬指标与项目标准演示参数。右栏参数用于构造可重复的 Platform 场景，但不扩大或改写左栏验收口径。

| 第三方验收硬指标 | 项目标准演示参数 |
| --- | --- |
| 通信原理演示网络：3～4 nodes | `Acceptance4Node`：3 个移动探测节点 + 1 个固定融合中心，是第三方验收基线 |
| 通信速率：60 bit/s | `RateBasedTxPhy` 按 payload bytes × 8 计算 airtime；非整纳秒结果确定性向上取整 |
| 通信误码率：BER ≤ 1e-4 | `ber_requirement = 1e-4` 作为场景约束保存；当前 Rx provider 不提供可审计 BER，正式状态为 `NotEvaluated` |
| 分布式探测：特征级融合 | `DetectionFeatureReportV1` 经正式网络路径送达并由 feature-level bearing solver 融合 |
| 方位观测：≥ 5 points | 每个独立 `FusionResult` 从自身 canonical observation identities 统计，标准结果为 6 points |
| 完整探测周期：≤ 180 s | 从每个 fusion window 首条 observation 到该结果 completion 计算；标准四节点正常窗口为 24 s |
| 3～4 nodes 是验收规模 | `Extended6Node`：5 个移动探测节点 + 1 个固定融合中心，只是项目扩展示范，不称为第三方硬指标 |

## 场景参数

- 移动探测节点以 5 km/h（`5000 / 3600 m/s`）做低速匀速水平运动；位置由 committed `SimTime` 差值通过既有 StateProjector 语义推进，不按 callback 次数累加固定距离。
- 固定融合中心通过 `NodeCapabilityProfile` 与 `ProtocolRole::kSink` 表达，不依赖特殊 NodeId。标准 fixture 位于 8 m 深度，移动节点位于不同深度。
- 初始几何为可解释的中心辐射布局，移动节点到融合中心的平均水平距离约 1 km。
- M3/M4 使用既有 directed topology、routing 与 planner 路径，正式逻辑链路为 mobile sensor → fusion center。
- MAC 为 TDMA。Slot duration 为 4 s，其中 maximum planned payload 为 15 bytes，在 60 bit/s 下 airtime 为 2 s，guard interval 为显式配置的 2 s。
- `Acceptance4Node` communication cycle 为 3 slots × 4 s = 12 s；`Extended6Node` 为 5 slots × 4 s = 20 s。
- `NetworkUpdateIntervalCycles = 10`。每个 PlanningCycle 仍是一个 communication cycle，并且每周期 Commit；M3 structure 与 applied M4 schedule 只在周期 1、11、21……更新，M4 candidate 仍每周期计算。
- Acoustic configuration：center frequency 25 kHz、source level 110 dB、occupied bandwidth 4 kHz、environment depth 75 m（标准允许范围 50～100 m）。`AcceptanceScenarioConfig::TxPhyConfig()` 把该值直接写入 `RateBasedTxPhyConfig::source_level_db_re_1upa_at_1m`，随后由 `RateBasedTxPhy` 写入同单位的 `TxEmission` 字段，因此 P0 acceptance simulation 明确暂按 **110 dB re 1 uPa @ 1 m** 解释。Hardware source-level reference/calibration 仍等待 communication-device parameter confirmation；此 fixture 解释不替代后续硬件标定。
- `EnvironmentAssetId` 已在场景中保留；当前 synthetic field ID 可由 assembly 替换为后续验证过的黄海/南海 package，而无需修改 runtime hot path。

## Runtime architecture note

M1 是 Platform 对 ns-3 `Simulator` time 和 event scheduling 的封装，不是替代 scheduler。Acceptance runtime 仍通过 `Ns3KernelGateway`、`EventDispatcher`、`PlanInstaller` 和 `CycleCoordinator` 安装并执行所有 simulation-time event；双时间尺度只决定某周期是否更新 applied network structure/schedule。实现没有新增 fixed-step loop、自定义 EventQueue 或第二 simulation clock。

12-cycle production integration 固定验证：12 次 World Commit、12 次 M4 Candidate Build、2 次 M3 formal refresh、2 次 applied schedule update；周期 1 的 schedule 用于周期 1～10，周期 11 更新并由周期 12 复用。测试同时验证移动、固定 sink、Transmission/ChannelOutcome/Reception/CycleCommit trace 计数，以及 no-arrival 不进入 decode/reception 但不阻止运动与 Commit。

## Detection feature workload V1

`DetectionFeatureReportV1` 是 acceptance workload 的业务 payload，不是硬件帧、PHY frame 或 MAC frame。它不添加虚构 header、CRC 或 waveform 字段，固定为 15 bytes，并按 little-endian 编码：

| Offset | Size | Field | Semantics |
| ---: | ---: | --- | --- |
| 0 | 1 | type/version | 固定 `0x11`，表示 Detection Feature V1 |
| 1 | 2 | observation sequence | sender-local unsigned sequence |
| 3 | 4 | sample time | milliseconds from run start |
| 7 | 2 | sensor x | signed meter quantization |
| 9 | 2 | sensor y | signed meter quantization |
| 11 | 2 | bearing | signed centidegrees，范围 `[-18000, 18000)` |
| 13 | 1 | confidence | integer percent，范围 `[1, 100]` |
| 14 | 1 | flags | V1 当前只允许零 |

15 bytes 等于 120 payload bits，在 acceptance rate 60 bit/s 下 airtime 精确为 2 s；配合显式 2 s guard 继续形成 4 s TDMA slot。该 rate 和 payload 定义只属于 acceptance workload，不改变通用 Tx PHY 或未来真实 frame codec。

## Bearing point and feature-level fusion

每个 mobile sensor 在自己的 TDMA TxStart timestamp 之前，由 M1/ns-3 `INPUT_READY` event 生成一份报告。报告位置是 `CycleWorkingState::ProjectNodeState(sample_time)` 得到的当时 x/y，不是 run 初始位置或 cycle-end 位置。Global-frame bearing 使用 `atan2(target_y-sensor_y, target_x-sensor_x)` 并确定性量化为 0.01 degree；P0 不注入随机测量噪声。

一个 bearing point 的唯一身份是 `(sender NodeId, observation_sequence)`。重复 delivery 不重复计数；同一 sensor 在不同 cycle 的不同 sequence 是不同 observation point。因此 `>= 5 bearing points` 不等于 `>= 5 distinct network nodes`，也绝不能因为 Acceptance4Node 只有三个 sensor 而把阈值降为三。

Feature-level fusion 只使用每条成功业务 observation 自带的 sensor position 与 bearing，通过 2D bearing-line least-squares intersection 估计目标。算法不读取 raw waveform、ADC sample、ChannelFieldResponse 或真实 target answer，并对 non-finite/rank-deficient geometry 明确失败。只有到 fusion center 的 `LocalDelivery` 可进入 accumulator；ChannelNoArrival、NotDecoded、Overheard 与 RelayEnqueue 都不能计数。

`FusionResult` 是 run-level application output，不写入 `WorldSnapshot`。它保存 fusion sequence、第一条有效 observation 的 sample time、controlled `RUNTIME_DECISION` completion time、observation count、canonical observation identities、estimated target、fusion-center bearing、residual RMS 和 period requirement result。P0 fusion period 定义为：当前 fusion window 第一条有效 observation 的 `sample_time` 到 `FusionResult.completed_at`。成功形成结果时整个 active window 被 seal 并消费，后续 window 只能接收新的 identity，禁止 sliding-window reuse。Acceptance4Node 无丢包 golden run 每两个 12 s cycles 累计六点并形成一个独立结果；四周期 run 因而形成两个互不重叠、各 24 s 的结果。Extended6Node 是五 sensor、一个 20 s cycle 完成五点的项目扩展示范，不是第三方节点数硬指标。

BER requirement `1e-4` 仍只作为 acceptance 配置约束保存。当前 decoded/not-decoded packet outcome 或 packet delivery ratio 都不是 physical BER；BER metric plumbing 与可信物理 BER source 继续留给后续 M5/metrics 阶段。

## Run projection and acceptance report

`AcceptanceRunProjection` 是 run 完成后的只读 result-side 投影。它只读取 immutable `AcceptanceScenarioConfig`、实际 applied `RateBasedTxPhy` 配置、typed Trace sequence、`FusionResultStore` 与 final `WorldSnapshot`，不持有或访问可写 `CycleWorkingState`、`ProtocolKnowledgeStore` 或 runtime owner，也不向运行时反馈控制。相同输入 sequence/value 必须产生完全相同的 projection。

| Result / metric | Authoritative evidence source |
| --- | --- |
| Run start、node/mobile/fusion-center count | immutable `AcceptanceScenarioConfig` |
| Run end、duration、final snapshot version | final read-only `WorldSnapshot` |
| Cycle/Transmission/Channel/Reception/disposition counts | typed Trace events |
| Effective communication rate | actually applied `RateBasedTxPhy::config()` |
| Fusion count、first/latest summary | `FusionResultStore` |
| Bearing points | each `FusionResult.observation_count` |
| Fusion period | each `FusionResult.started_at/completed_at` and checked period |
| BER | future auditable M5 Rx quality source（当前不存在） |

第三方 `Acceptance4Node` report 对 NetworkNodeCount、CommunicationRate、FeatureLevelFusion、BearingPointCount 与 FusionPeriod 分别输出 `Pass`/`Fail`，BER 输出 `NotEvaluated`，原因固定为 physical Rx provider 尚未暴露可审计 BER。只要任何正式项是 `NotEvaluated`，overall 必须是 `NotFullyEvaluated`，绝不能显示 PASS。`Extended6Node` 只生成运行 projection，不生成 3～4 node 第三方 acceptance verdict。

`NoArrival`、`NotDecoded`、`Overheard`、`LocalDelivery` 与 `RelayEnqueue` 是运行质量统计，可与融合指标同时呈现；packet delivery ratio、decode success ratio 或 no-arrival ratio 均不得替代 BER。Deterministic text formatter 只负责把 typed result 映射成无 ANSI、无 terminal-dependent 行为的展示文本，不进入 runtime contract 或因果路径。
