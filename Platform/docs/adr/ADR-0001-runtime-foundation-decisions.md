# ADR-0001：Runtime Foundation Decisions

- 状态：Accepted / Frozen for P0
- 日期：2026-08-17
- 适用范围：P0-S0 Contracts Freeze、P0-S1 Core Closed Loop，以及架构模块 M1/M2/M3 的基础边界与 M4 routing/MAC planning contracts
- 依据：`NS3-BELLHOP_P0.4_软件架构与开发实施设计基线.docx` 与第一轮旧系统审计结论

## 1. 背景

旧系统已经提供环境数据、Bellhop 离线计算、传播/噪声/定位算法、前后端和实验管理等可迁移资产，但运行核心仍由 `src/main.cpp` 的 fixed-step loop 驱动。当前代码没有真正接入 ns-3；旧 `SimulationManager` 也不是真实事件运行内核。

新 `Platform/` 是首次建立真正的 ns-3 离散事件运行内核。为防止旧运行语义、可写 owner、Trace 强耦合和广播重复发送风险进入新系统，本 ADR 冻结 Runtime Foundation 的实施决策。

本 ADR 不实现代码，不决定仍标记为 TBD 的物理层、BIN、帧、噪声参数或严格实时 HIL 细节。

## 2. 决策

### 2.1 旧系统事实与迁移边界

1. 当前旧系统没有真正接入 ns-3。
2. `src/main.cpp` 的 fixed-step loop 是旧系统真实运行内核。
3. 新 `Platform` 是首次建立真正的 ns-3 离散事件运行内核。
4. 旧 `SimulationManager`、fixed tick 和全局 MAC busy state 均不得进入新 Runtime。

旧系统只作为资产来源、算法参考和结果对照。新 Runtime 不通过兼容旧类的方式重新引入 fixed-step 状态模型。

### 2.2 公共 contracts 与 runtime owner 边界

`contracts/` 只保存：

- 跨模块需要交换的数据；
- 跨模块只读 View；
- 必要的稳定接口。

以下可写 owner 不作为公共 DTO 暴露：

- `CycleWorkingState` 的具体可写实现属于 `runtime/`；
- `ProtocolKnowledgeStore` 的具体可写实现属于 `runtime/`。

头文件边界冻结为：

- `contracts/working_state.hpp` 主要定义 `WorkingStateView` 等只读合同；
- `contracts/knowledge.hpp` 定义 `KnownNodeState`、`KnownLinkState`、`KnowledgeView`、`KnowledgeDelta` 等跨模块合同。

M3/M4/M5/M6/M8 不得通过 contracts 获取 runtime owner 的可写引用、指针或容器。

### 2.3 Trace 最小依赖

`trace.hpp` 不得 include 所有业务 contracts。

`TraceEvent` 只依赖：

- `SimTime`；
- identity；
- error/status；
- Trace 自己定义的只读摘要 payload。

禁止把完整 `WorldSnapshot`、完整 `ProtocolCyclePlan` 或其他大型业务对象直接放入 `TraceEvent`。Trace 中需要关联业务对象时，使用稳定 ID 和 Trace 自有摘要字段。

### 2.4 Transmission 与 Reception Trace 语义

旧代码在 receiver loop 内针对每个 peer 写一条 `tx` trace，该语义不得直接迁移。

新系统采用：

- 一个 `TransmissionSession` 对应一条 Transmission trace；
- 每个 `ReceptionSession` 分别对应一条 Reception trace；
- 广播接收节点数量不得影响发送次数统计。

因此，广播 fan-out 只增加 Reception trace 和 ChannelQuery，不增加 Transmission trace。

### 2.5 开发里程碑命名

开发里程碑使用以下命名：

- P0-S0 Contracts Freeze；
- P0-S1 Core Closed Loop；
- P0-S2 Protocol Baselines；
- P0-S3 PHY/Channel Integration；
- P0-S4 Environment/Bellhop Integration；
- 后续阶段继续使用 `P0-S*`。

`M1-M8` 只用于 P0.4 架构模块编号，不再用于开发阶段编号。

### 2.6 ns-3 基线和 Platform 时间值

1. 当前项目沿用暂定 ns-3.47 基线。
2. 第一阶段只依赖实现离散事件内核所需的最小 ns-3 能力，不因接入 ns-3 而引入完整网络协议栈。
3. `SimTime` 和 `SimDuration` 是 Platform 自己的强类型时间值。
4. 底层表示为 `int64` nanoseconds。
5. 时间值类型不提供 `Now()`、`Advance()` 或 `Tick()` 能力。
6. contracts 不暴露 `ns3::Time`。
7. `ns3::Time <-> SimTime` 转换只存在于 kernel/M1。
8. 在线运行唯一 `Now()` 来源仍然是 M1/ns-3。

该设计隔离 ns-3 类型，但不建立第二个时钟或第二套调度器。

### 2.7 同一仿真时刻的 EventPhase

同一 timestamp 的事件必须先按以下阶段顺序执行：

| 顺序 | EventPhase | 事件 |
|---:|---|---|
| 1 | `SESSION_FINALIZE` | TxEnd、RxFinalize |
| 2 | `SIGNAL_ARRIVAL` | 已传播到节点的 RxStart |
| 3 | `INPUT_READY` | Application input ready、External input ready |
| 4 | `RUNTIME_DECISION` | Timer、BackoffExpire、CarrierSense、RuntimeHook |
| 5 | `TX_START` | TxStart |
| 6 | `CYCLE_CLOSE` | Aggregate、Commit |

同一 phase 内继续使用稳定 ID 排序。结果不得依赖：

- unordered container 遍历顺序；
- pointer 地址；
- 线程完成顺序。

如果运行过程中在当前 timestamp 动态创建事件，只允许创建当前 phase 之后的 phase；禁止回插已经执行过的 phase。

### 2.8 P0 closed-cycle 约束

P0 采用 closed-cycle：

- Cycle Commit 时，不允许存在该周期尚未完成的 `TransmissionSession`；
- Cycle Commit 时，不允许存在该周期尚未完成的 `ReceptionSession`；
- Planner/Runtime 必须确保发送、传播和接收完成时刻位于 `CYCLE_CLOSE` 之前；
- ALOHA/CSMA 等运行时动作如果剩余周期时间不足以完整结束，则延迟到下一周期。

跨 PlanningCycle 保留 active Tx/Rx session 属于未来扩展，P0 不实现。

### 2.9 Cycle0

Cycle0 不设置 bootstrap tick，执行流程为：

```text
t = 0
WorldSnapshot S0
  -> Protocol Planner
  -> ProtocolCyclePlan
  -> CycleTiming
  -> install ns-3 events
```

Cycle0 与后续周期使用相同代码路径，不建立特殊 fixed-step 或 bootstrap scheduler。

### 2.10 周期中新业务

Packet 在周期中生成后只进入 Queue。

- 如果节点后续已有可使用的 `TxOpportunity`，TxStart 时 `PacketSelector` 可以选择该 Packet；
- 如果本周期已无可用 opportunity，该 Packet 等待下一周期；
- PacketReady 本身不触发 M3/M4 全局重新规划。

只有未来显式实现 `IMacRuntimePolicy` 的协议，才能在周期内生成协议内部动态动作。

### 2.11 NodeId{0}

`NodeId{0}` 是合法节点编号。

任何“无节点”语义必须使用 optional 或显式状态，禁止将 0 作为 invalid sentinel。

### 2.12 Bellhop 查询失败语义

P0 禁止以下行为：

- 越界自动 clamp；
- 查询失败静默 extrapolation；
- 无 multipath 时自动合成直达路径；
- 无对应频率时静默选择最近 profile。

至少区分以下结果：

| 状态 | 含义 | 分类 |
|---|---|---|
| `ASSET_NOT_FOUND` | 指定环境/声场资产不存在 | 资产/配置/查询错误 |
| `OUT_OF_COVERAGE` | 坐标、距离或深度超出资产覆盖 | 资产/配置/查询错误 |
| `FREQUENCY_PROFILE_NOT_FOUND` | 没有请求的频率档案 | 资产/配置/查询错误 |
| `NO_PHYSICAL_ARRIVAL` | 查询有效，但物理模型没有到达路径 | 合法物理结果 |

只有 `NO_PHYSICAL_ARRIVAL` 是合法物理结果；其余状态不得被静默转换成可通信链路。

### 2.13 旧 Frontend/Backend API 兼容

P0-S0 和 P0-S1 不承担旧 Frontend/Backend API 兼容。

后续通过 `LegacyScenarioConverter` 实现：

```text
old schema -> new Scenario schema
```

新 contracts 不得为兼容旧 API 反向引入 `time_step_ms` 或其他 fixed-tick 字段。

### 2.14 P0-S1 广播 invariant

Broadcast fan-out invariant 正式纳入 P0-S1。场景：

```text
N2 broadcast -> {N1, N3, N4}
```

必须精确满足：

| 指标 | 期望值 |
|---|---:|
| TxOpportunity | 1 |
| TransmissionSession | 1 |
| TxStart | 1 |
| TxEnd | 1 |
| TxEmission | 1 |
| QueueConsume | 1 |
| TxEnergySettlement | 1 |
| ReceptionSession | 3 |
| ChannelQuery | 3 |

任何 receiver 数量导致 TxStart、TxEnd、QueueConsume、TxEnergySettlement 或 Transmission trace 增长的实现均不合格。

### 2.15 ChannelFieldResponse scalar/multipath 语义

`ChannelFieldResponse` 对一个 Transmission 和一个 receiver 表达一个信道结果：

- `first_arrival_delay` 是从 Transmission/TxEmission 的 `started_at` 到最早物理路径到达的传播时间，且不得为负；
- `PropagationPath.excess_delay` 是相对 `first_arrival_delay` 的额外延迟，路径绝对传播时间必须通过二者的 checked addition 得到；
- 非空 path 集合必须至少包含一条零 `excess_delay` 路径，并依次按 `excess_delay`、`pressure_gain_linear`、`phase_radians` 升序 canonicalize；不得依赖 provider 输入顺序，三个字段完全相同的路径在当前 contract 下语义等价；
- `pressure_gain_linear` 是查询频率下绝对、无量纲的线性声压传递幅值，必须 finite 且不小于零，但可以大于 1；
- `phase_radians` 必须 finite，不强制归一化区间；
- `aggregate_transmission_loss_db` 是 scalar aggregate/fallback summary，必须 finite，但不强制非负。

Scalar processing 可以使用 source level 减去 `aggregate_transmission_loss_db`。Path-aware processing 使用每条路径的绝对 `pressure_gain_linear`、phase 和 delay。两者是同一信道传递的不同表达层级；同一个 path-aware signal 禁止先应用 aggregate transmission loss 再应用 absolute path gain，以免重复计算信道损耗。

`paths.empty()` 是合法的 scalar-only 结果。非空 paths 不改变 cardinality：一个 Transmission 与一个 receiver 仍只对应一个 `ChannelFieldResponse`，不会因 path 数量生成额外 Packet、Transmission 或 Reception。

Provider 返回结果的 `transmission_id` 和 `receiver_node_id` 必须分别匹配发起查询的对应 identity；caller 必须在结果进入后续接收流水线前执行 contracts 层的纯 identity validation，不得静默接受 mismatch。

### 2.16 ReceivedSignal 与 P0 ReceiverWindow overlap 语义

`ReceivedSignal` 是一个 `TxEmission` 经一个 `ChannelFieldResponse` 到达一个 receiver 后形成的、per Transmission/per receiver 的物理信号 value。它 value-own emission 和 response，不是 Reception、decode result 或 success/failure，也不创建 `ReceptionId`。

ReceivedSignal 的完整物理影响时间使用半开区间 `[first_arrival_at, last_effect_end_at)`：

- `first_arrival_at = started_at + first_arrival_delay`；
- scalar-only response 的终点是 first arrival 加 Tx duration；
- multipath response 的终点是 first arrival 加最大 excess delay，再加 Tx duration；
- 所有时间组合必须使用 checked arithmetic；
- 一个信号的 end 与另一个信号的 start 相等时，不构成 temporal overlap。

P0 occupied band 是根据 Tx center frequency 和 bandwidth 得到的 ideal rectangular interval。两个 band 只有存在正宽度交集时才构成 spectral overlap；一个 band 的 upper boundary 等于另一个 band 的 lower boundary 时不重叠。Doppler、filter skirt 和 adjacent-channel leakage 保持 TBD。

P0 signal overlap 精确定义为：same receiver、半开时间区间重叠、rectangular frequency interval 重叠三项同时成立。Overlap 只表示进入后续 interference evaluation 的候选，不代表 decode failure 或固定强度惩罚。

`ReceiverWindow` 是围绕 caller-selected desired `ReceivedSignal` 的一次未来 PHY 判决上下文。它只 value-own desired signal 和满足 P0 overlap 的、TransmissionId 唯一的 overlapping signals；它不选择 desired signal，也不是 receiver 的永久 signal history 或 runtime `InFlightSignalLedger`。Overlaps 必须按 `first_arrival_at`、`TransmissionId` 升序 canonicalize。

### 2.17 Noise observation 与 P0 packet-level Rx decode 语义

`NoiseObservation` 是对指定 receiver、半开时间窗口和半开频带内 equivalent in-band acoustic pressure-squared noise power 的 value observation，单位为 dB re 1 µPa²。它不是 PSD、waveform sample 或 amplitude gain，并必须与原始 `NoiseQuery` 的 receiver、时间区间和频带精确匹配。

P0 packet-level scalar Rx processing 使用 Tx source level 减去 `aggregate_transmission_loss_db` 得到 desired 和 overlapping signal 的 scalar received level。Multipath excess delay 已用于 ReceivedSignal time envelope，但 scalar decoder 不再额外应用 absolute `pressure_gain_linear` 或 phase，禁止 aggregate loss 与 path gain double-count。ReceiverWindow 中的 overlaps 仍只是可供具体 Rx PHY 评估的 interference candidates；contracts 不冻结固定 interference penalty、capture threshold 或 PER 算法。

`IRxPhy` 的完整输入是 value-owned `ReceiverWindow + NoiseObservation`，不得自行查询 WorldSnapshot、channel provider 或 scheduler。对相同 `RxDecodeRequest`，P0 Decode 必须产生确定性相同结果；基于 PER 的随机成功抽样留给未来显式 deterministic RNG mechanism，禁止 hidden global RNG、wall-clock seed 或 thread timing。

`RxDecodeResult` 只表达 desired Transmission/Packet 在 receiver 上的 packet-level decode outcome，不是 Reception identity，不创建 `ReceptionId`，也不直接修改 WorldSnapshot。未来 ReceptionManager 在 Decode 之后独立负责 Reception lifecycle。

### 2.18 TransmissionSession 与确定性传播 fan-out

`TransmissionSession` 是一次真实 physical send attempt 的完整、value-owned runtime value，恰好包含一个 `DigitalPacket`、一个 `Transmission`、一个 `TxEmission`，以及 0..N 个 receiver-specific `ReceivedSignal`。Runtime 为每次 attempt 从 instance-owned、显式起点、单调递增的 allocator 分配新的 `TransmissionId`；耗尽 `uint64` 空间时显式返回 `kOverflow`，失败 attempt 造成的 ID gap 合法，禁止 global/static mutable counter 或随机 ID。

一次 session 对 `ITxPhy::Encode` 恰好调用一次，并在验证 `TxEmission` 的 TransmissionId、PacketId、sender 和 started_at provenance、正 duration 以及 checked end time 后，才开始 receiver fan-out。`TransmissionTarget` 只表达 current-hop protocol/link intent，不限制 physical propagation candidate set；unicast target 之外的节点仍可因共享声学信道形成信号。候选 receiver 必须存在、不同于 sender、具备 receive capability 且无重复，并按 `NodeId` 升序 canonicalize，因此 ChannelQuery 顺序不依赖输入容器迭代顺序。

P0 的每个 ChannelQuery 均使用 `CycleWorkingState::ProjectNodeState(node, started_at)` 得到 Tx/Rx geometry，`emitted_at` 等于 `started_at`。本阶段不迭代求解 receiver 在 propagation delay 内继续运动造成的 geometry 变化，该策略保留为 future TBD。Fan-out 在 Encode 和 Transmission finalization 之后执行；provider error 和 response identity mismatch 均显式令整个执行失败，不允许 silent fallback，也不向 caller 外泄部分构造的 session。Fan-out loop 只产生 receiver/channel-specific `ReceivedSignal`，不得创建额外 Transmission 或放置 sender-side per-receiver mutation。

### 2.19 InFlightSignalLedger 与 receiver-side processing

`InFlightSignalLedger` 是 runtime-owned mutable working registry，不是公共 contract 或永久历史数据库。同一 `(receiver NodeId, TransmissionId)` 最多保存一个完整 `ReceivedSignal`；同一 TransmissionId 在不同 receiver 合法。每个 receiver 的查询结果按 `first_arrival_at`、`TransmissionId` 升序 canonicalize。当前 processing 不删除 desired 或 overlap signal，使相互 overlap 的 signal 均可分别构造完整 window；自动 expiry/cleanup policy 留到 scheduler/event integration 阶段冻结。

每个 ledger 中的合法 `ReceivedSignal` 都可独立作为 desired decode candidate，不依据 TransmissionTarget、PacketDestination 或 received power 预过滤。`ReceiverWindow` 只通过既有 `HasP0SignalOverlap` 选择同 receiver overlap；MAC/address filtering 留给更高层。

Receiver processing 按 window、receiver position projection、noise query/validation、Rx request、decode/validation、ReceptionId allocation、complete session construction 的顺序原子返回。Noise receiver geometry 由 `CycleWorkingState::ProjectNodeState(receiver, desired.first_arrival_at())` 采样，不使用 transmission started_at 或 wall clock；未来 time-varying noise/trajectory integration 保持 TBD。`Reception.arrival_at` 等于 desired `first_arrival_at`。

`DecodeOutcome::kNotDecoded` 是正常物理 outcome，仍产生完整 `ReceptionSession`；只有 provider/PHY 返回 `Result` error 才是 runtime execution failure，禁止 silent fallback 或伪造结果。ReceptionId 在 noise/decode 及 provenance validation 全部成功后分配，失败不消耗 ID。Receiver processing 不回调 sender transmission logic，不新增 Transmission、TxEmission 或 Encode。

### 2.20 ns-3 EventDispatcher 与 P0 signal lifecycle

ns-3 是 Platform 唯一在线 simulation-time 与 event authority。只有 kernel/M1 的 `Ns3KernelGateway` 可以读取 `ns3::Simulator::Now()`、执行 Platform 强类型时间与 `ns3::Time` 的 checked integer-nanosecond conversion，以及调用 ns-3 schedule/run/stop/destroy。Runtime event handlers 只接收 `SimTime` 和普通 C++ callback，不拥有 clock、Advance/Tick 或第二套 scheduler。

Platform event key 由 `SimTime + EventPhase + instance-owned monotonic EventSequenceId` 组成。同一 timestamp 由 dispatcher 的单个 ns-3 callback 按 key 显式派发，phase 固定为 SESSION_FINALIZE=10、SIGNAL_ARRIVAL=20、INPUT_READY=30、RUNTIME_DECISION=40、TX_START=50、CYCLE_CLOSE=90；不得依赖 ns-3 偶然 insertion order。Callback 在执行中创建同 timestamp event 时，phase 必须严格晚于当前已执行 phase，否则返回 `kFailedPrecondition`。

TxStart 只执行一次 TransmissionExecutor/Encode，并在完整 TransmissionSession 构造后为每个 ReceivedSignal 安排一个 first-arrival event 和一个 last-effect-end finalize event。Arrival 只把完整 signal 插入 cycle-owned ledger，不执行 noise/decode；finalize 才构造 ReceiverWindow 并执行 receiver processing，因为此时完整 physical influence interval 已结束且所有真实 overlap 已到达。相同 timestamp 的 finalize phase 先于 arrival phase，使 `[start,end)` 边界上 A.end==B.start 时 B 不会错误进入 A 的 window。Receiver callbacks 不回调 sender Tx logic，也不 commit 或触发 routine mid-cycle replan。

P0 ledger 按 PlanningCycle 保留：cycle 开始为空，arrival 插入，finalize 不删除。CycleClose 必须先验证不存在 `last_effect_end_at > close_time` 的 signal；等于 close time 的 signal已在同刻 SESSION_FINALIZE phase 完成。只有 CycleClose 执行 `FinalizeDeltaSet + CommitCycle`，commit 成功后才清空 ledger；失败不得静默截断 signal 或清空 working registry。一个 cycle 最多成功 commit 一次，successful commit 是后续正式 replan boundary。

### 2.21 ProtocolCyclePlan 安装与 CycleCoordinator 生命周期

`ProtocolCyclePlan` 是 planner 到 kernel/M1 的只读执行计划边界，由合法 `CycleTiming` 和最小 `MacPlan` 组成。`CycleTiming` 必须满足 `starts_at < closes_at`，其 base snapshot version 必须匹配启动 cycle 时的 authoritative snapshot；`MacPlan` 当前只 value-own `TxOpportunity`。TxOpportunity 仍仅表示 sender 在指定 simulation time 获得发送机会，不等于 packet dequeue，也不携带 packet、target、receiver candidates、channel query 或 PHY result。

计划 factory 必须把 opportunities 按 `eligible_at`、`sender NodeId` 升序 canonicalize，并拒绝相同 sender/eligible_at 的重复项。所有 opportunity 必须位于 `[starts_at, closes_at)`；`eligible_at == closes_at` 即使 TX_START phase 早于 CYCLE_CLOSE 也不合法，因为正时长 physical send 无法满足 P0 closed-cycle。

`PlanInstaller` 只把一个 canonical ProtocolCyclePlan 转换为 N 个 TX_START 事件和恰好一个 CYCLE_CLOSE 事件。它不选择 packet/route/target/receiver，不调用 PHY、Channel、Bellhop、CommitService 或 `ns3::Simulator`；事件只经既有 `EventDispatcher` 进入 ns-3。M1 internal execution hook 只携带 `TxOpportunity/CycleTiming + SimTime now`，具体 packet/target/receiver mapping 属于后续 runtime/assembly owner，本阶段只允许存在于 test fixture。

`CycleCoordinator` 负责单个 PlanningCycle 的 M1 lifecycle，不持有或修改 WorldSnapshot、CycleWorkingState、ledger、PHY 或 planner state。同一 coordinator 最多安装一个 plan；安装后的 cycle id/timing 不可变；base version mismatch、double install、double close 和 completed-cycle reuse 必须显式失败。只有既有 runtime CycleClose hook 成功完成 Commit 后 coordinator 才转为 completed，不增加第二个 CommitService 调用点，也不自动生成下一周期。

`ProtocolCyclePlan` 安装具有 Platform-level ALL-or-NONE 语义。`EventDispatcher` 必须先对完整 batch 预检 callback、time、phase、same-time dynamic phase floor 和整批 `EventSequenceId` 容量，并完成全部 EventKey 分配；只有整批合法且所需 ns-3 timestamp drain 已成功注册后，TX_START 与唯一 CYCLE_CLOSE 才能一起成为可执行 business events。失败安装不得改变 pending business event 集合；允许底层留下不包含 business callback 的 harmless empty drain。`PlanInstaller` 禁止逐项形成不可回滚 side effect，`CycleCoordinator` 只能在完整 batch 成功后从 idle 转为 active，失败后必须保持 idle 且允许合法 plan 重试。

已调度 Platform event 必须 value-own 执行所需的 immutable `TxOpportunity` 或 `CycleTiming` payload，不得保存指向 caller-owned `ProtocolCyclePlan`、`MacPlan` vector 或 span element 的引用/指针；因此 caller plan 生命周期不约束 event lifetime。Event execution hook 与 dispatcher 仍是 ScenarioRuntime/kernel 的长期 owner，必须覆盖本次 SimulationRun 生命周期。

安装失败发生在任何 plan business event 可执行之前，适用上述原子失败与 retry 语义。安装成功后的 callback error（包括 TxStart、receiver processing 或当前 zero-delay phase conflict）属于 P0 SimulationRun-fatal execution failure；本阶段不 rollback cycle、不恢复 coordinator、不继续下一周期，调用方必须终止该 SimulationRun 并丢弃对应 ScenarioRuntime。

P0 eventized runtime 暂不支持 TX_START 在同 timestamp 动态产生零传播延迟 SIGNAL_ARRIVAL：TX_START phase 50 向已执行过的 SIGNAL_ARRIVAL phase 20 回插违反冻结 causal ordering，必须返回 `kFailedPrecondition`。禁止把 arrival 静默平移到 `+1ns`，也禁止更改 phase 来掩盖该边界；未来支持方案保持独立 ADR/TBD。

### 2.22 M3 structure contracts 与确定性数据模型

M3 对一个 PlanningCycle 产生只读 `StructureSnapshot`，它只 value-own `RoleTable`、`ConnectivityGraph`、`LogicalTopology` 以及 `PlanningCycleId + base SnapshotVersion` provenance，不嵌入完整 WorldSnapshot、ProtocolCyclePlan 或 runtime owner。三个结构对象必须由同一个 canonical NodeId universe 验证，禁止把基于 S(k) 的结构结果静默用于 S(k+1)。`NodeId{0}` 在 universe、binding 和 edge 中均为合法 identity，不承担 invalid sentinel 语义。

`RoleTable` 是 `(NodeId, ProtocolRole)` binding set。一个节点可以同时持有多个不同 protocol role；完全相同的 binding 禁止重复，“没有角色”通过 binding 不存在表达，不定义零值 INVALID role。Binding 按 NodeId、ProtocolRole 显式非零 underlying value 升序 canonicalize。Sink、Controller、Relay、AccessNode 等均属于 RoleTable，不把 sink identity 复制到 topology。

`ConnectivityGraph` 是 directed one-hop feasibility candidate graph。`source -> target` 与 `target -> source` 是独立 edge；P0 禁止 self-loop 和 duplicate edge。Connectivity edge 只表达 M3 判定的一跳候选，不是 PHY reception result、Bellhop path、routing table 或 MAC schedule，也不携带 SNR、SINR、PER、TL、delay、frequency、path、noise、PHY pointer 或预先冻结的 edge weight。

`LogicalTopology` 使用与 `DirectedLink` 语义独立的 `LogicalLink`，表示协议结构实际选择的 directed adjacency；它不使用 STAR/MESH/TREE 等 topology type 标签。每个 LogicalLink 必须存在于同一 NodeId universe 的 ConnectivityGraph，因此 `LogicalTopology` 必须是 `ConnectivityGraph` 的子集。所有 directed edge set 均按 source NodeId、target NodeId 升序 canonicalize，结果不得依赖 caller insertion order、unordered iteration 或 pointer address。

### 2.23 M3 directed connectivity decision layer

P0 的 `ConnectivityGraph` 从 WorldSnapshot 的 canonical node order 确定性枚举全部非 self ordered directed pairs，外层按 source NodeId、内层按 target NodeId 升序。Planning observation time 固定使用该 WorldSnapshot 的 `committed_at`，position 直接读取对应 `NodeCommittedState`；M3 不读取 Now、不创建 StateProjector，也不访问 runtime working overlay。

每个 directed candidate 依次经过 capability gate、可选 coarse range reject gate、`ILinkFeasibilityEstimator` 和 threshold decision。只有 source `can_transmit` 且 target `can_receive` 才能继续；duplex mode 不参与本阶段静态判定。可选 range 使用完整 3D Euclidean distance，超过配置上限时只能 reject 且不得调用 estimator；未超过范围只表示允许继续估计，绝不自动接受 edge，range 不等价于水声链路模型。

M3 只依赖抽象 `ILinkFeasibilityEstimator`，不得知道 Bellhop、Channel provider 或 Tx/Rx PHY concrete implementation。Estimator 对相同 query 和相同 immutable configuration 必须 deterministic；其 score 是 finite、dimensionless、estimator-specific decision score，数值越大表示在配套 policy 下越适合作为 one-hop candidate，但不等于 SNR、PER、TL 或 received level，不要求位于 `[0,1]`，也不写入 ConnectivityGraph。

Connectivity policy 使用 inclusive threshold：上周期不存在的 edge 仅在 `score >= enter_threshold` 时进入，上周期存在的 edge 在 `score >= keep_threshold` 时保留，并要求 `enter_threshold >= keep_threshold`。Capability 或 coarse range rejection 优先于 hysteresis，不能因 previous edge 存在而强行保留。Previous graph 的完整 NodeId universe（包括 isolated node）必须与当前 WorldSnapshot 一致。

每个通过 capability/range gate 的 pair 对 estimator 恰好调用一次，并验证 estimate 的 source、target、observed_at provenance 和 finite score。任一 estimator error、provenance mismatch 或非法 result 都使整个 graph build 失败；禁止返回 partial graph、吞掉错误或回退为 range-only graph。Builder 使用纯局部构建状态，失败后可由调用方使用合法 estimator 重新 Build。Coarse range 与 enter/keep threshold 的具体协议配置值仍保持显式配置/TBD，不定义生产默认值。

### 2.24 M3 role、logical topology 与完整 StructureBuilder pipeline

P0 M3 的固定构建次序是 `ConnectivityGraph -> RoleTable -> LogicalTopology -> StructureSnapshot`。`StructureBuilder` 只编排现有 connectivity builder、role policy 和 logical-topology policy；它不调度 ns-3、不调用 Tx/Rx PHY、不生成 route/MAC plan，也不 commit WorldSnapshot。最终 StructureSnapshot 的 cycle id 来自 build request，base snapshot version 必须直接读取当前 WorldSnapshot，调用方不得另传一份可能分叉的 base version。

Role assignment 与 logical topology 保持独立语义。P0 的确定性基线 `ConfiguredRoleAssignmentPolicy` 只 value-own 并产生显式配置的 RoleBinding，不为未配置节点隐式补 MEMBER；无角色节点可表示 inactive、temporarily unavailable 或非协议参与节点。Dynamic sink/controller/access-node election 属于后续算法，不在本阶段冻结。

`AllFeasibleLinksTopologyPolicy` 仅按 ConnectivityGraph 的 canonical directed-edge 顺序，把每个 DirectedLink 显式映射为语义独立的 LogicalLink；不 mirror、不增加 edge，也不引入 MESH 类型标签。`SingleSinkStarTopologyPolicy` 只在该具体 policy 下要求 RoleTable 恰好包含一个 sink，并仅保留 ConnectivityGraph 中 source 或 target 等于该 sink 的既有 directed edge。它不制造反向链路；member 与 sink 不连通是合法结构状态，后续 routing 层负责判断是否存在 route。Sink/controller/access-node identity 只保留在 RoleTable，不复制进 LogicalTopology。

StructureBuilder 的 Build 是 complete-or-error：connectivity、role、topology 或最终 StructureSnapshot validation 任一失败都不返回 partial result，也不保存半成品 mutable state；合法输入可以随后安全重试。Configured roles 依赖 RoleTable factory canonicalize，topology policy 按 canonical connectivity 顺序映射或筛选，builder 禁止引入 unordered iteration、pointer ordering 或 thread timing。

当 StructureBuilder 把 previous ConnectivityGraph 交给 hysteresis 时，同一 SimulationRun 内配套的 ConnectivityDecisionPolicy 和 ILinkFeasibilityEstimator decision-score semantics 必须保持不变。更换 estimator、score definition 或 threshold semantics 必须启动新 run，或显式丢弃 previous connectivity history；P0 不支持 hot reconfiguration。

### 2.25 M4 next-hop routing contract 与 DirectToSink baseline

M4 routing 只允许在 M3 已选择的 LogicalTopology 上运行，不得绕过 logical edge 而直接使用 ConnectivityGraph candidate。`RouteEntry` 明确区分当前持有 packet 并准备转发的 forwarding node、DigitalPacket 的 end-to-end destination 和本次 physical send 的 next hop；forwarding node 不得等于 destination 或 next hop，但 next hop 可以等于 destination，`NodeId{0}` 在三个位置均为合法 identity，no-route 必须由查询返回空 optional 表达。

`RoutingPlan` 是 value-owned next-hop forwarding table，并直接绑定输入 StructureSnapshot 的 PlanningCycleId 和 base SnapshotVersion provenance，不 value-own StructureSnapshot、packet、TransmissionTarget、MAC 或 PHY result。Entry 按 forwarding node、destination、next hop 升序 canonicalize；P0 的每个 `(forwarding node, destination)` route key 最多有一个 next hop，不支持 ECMP/multipath route。每个 entry 的三个 NodeId 都必须属于结构 node universe，且 `(forwarding node, next hop)` 必须是已有 LogicalLink。

No-route 是正常网络状态。通用 RoutingPlan contract 只验证 identity、route-key uniqueness、logical next-hop adjacency、node universe 和 provenance，不强制 partial route entries 构成全局完整、最终到达 destination 的无环 chain；具体 planner 对自身算法结果负责。

P0 的 `DirectToSinkRoutingPlanner` 只在该具体 planner 下要求 RoleTable 恰好包含一个 sink。它仅为 LogicalTopology 中已有的 `source -> sink` directed uplink 生成 `{forwarding=source, destination=sink, next-hop=sink}`，不从 `sink -> source` 制造反向 route，也不要求 source 具有 MEMBER role。Disconnected node 或唯一 sink 下没有可用 uplink 均产生成功但可能为空的 RoutingPlan，不使整个 planning operation 失败。Build 使用纯局部 entries 并通过 RoutingPlan factory 完成最终构造，因此保持 deterministic、complete-or-error 且失败后可重试。

### 2.26 M4 configured TDMA slot-start planning

P0-S1-04B 的 TDMA baseline 是 configured fixed-slot-start planner。`ConfiguredTdmaPolicy` value-own 一个严格大于零的 slot duration 和至少一个 slot owner；slot owner vector 的输入顺序就是业务 slot 顺序，禁止按 NodeId sort，且同一 sender 在不同 slot 重复出现合法。对于 cycle start `T0`、slot duration `D` 和第 `i` 个 configured owner，planner 生成一个 `eligible_at = T0 + iD` 的 TxOpportunity，并令 cycle close 等于 `T0 + ND`；全部时间推进必须使用 checked integer-nanosecond arithmetic。

Cycle start 固定为 WorldSnapshot `committed_at`。CycleTiming 的 cycle id 来自 StructureSnapshot，base version 来自 WorldSnapshot；Build 必须同时验证 StructureSnapshot base version 与 WorldSnapshot version 相等，以及两者包含 isolated nodes 在内的完整 NodeId universe 相同。每个 configured slot owner 必须存在于该 universe 且具备 transmit capability，否则整个 Build 失败，不允许 silent skip 或替换 sender。

Configured TDMA slot allocation 不依赖 RoutingPlan、LogicalTopology connectivity 或 ProtocolRole。存在且可发送的 sink、controller、relay、access node、anchor 或无显式 role 节点都可以按配置获得 opportunity；opportunity 不代表已有 packet、route 或实际 transmission，queue empty/no-route 时可以不使用。

`MacPlanningResult` 只是 planning-internal 的 `CycleTiming + MacPlan` 阶段结果，不替代或生成 ProtocolCyclePlan。Planner 使用纯局部 schedule，最终经 MacPlan factory canonicalize/validate，保持 deterministic、complete-or-error 且 overflow 后可重试。当前 TxOpportunity 只表达 slot start，不携带 slot-end deadline；因此 P0-S1-04B 不保证 runtime Encode 后的实际 TxEmission duration 自动小于 slot duration，也不声称 physical send 必然包含在对应 slot。Strict physical slot occupancy enforcement 必须经后续独立 contract/runtime 审查，禁止 planner 猜测 duration 或调用 PHY。

### 2.27 M4 routed ProtocolCyclePlan 与组合规划

`ProtocolCyclePlan` 是标准 cycle planning/execution bundle，并 value-own `CycleTiming + MacPlan + optional<RoutingPlan>`。原有 schedule-only factory 保留为 kernel/MAC-only 兼容入口并明确产生 `routing_plan = nullopt`；该空值只表示兼容模式，不表示 routing planner 失败。生产组合规划路径必须使用 routed factory，并要求 RoutingPlan 的 cycle id、base snapshot version 与 CycleTiming 完全一致。Routed factory 必须使用既有 MacPlan factory 对传入 MacPlan 按当前 CycleTiming 重新 canonicalize 和验证，不能信任阶段对象曾在其他 timing 下有效。

`IProtocolCyclePlanner` 是 planning-internal 的同步组合规划接口。P0 基线 `CompositeProtocolCyclePlanner` 只引用 caller-owned `IRoutingPlanner` 与 `IMacPlanner`，不持有 runtime owner、scheduler 或跨调用 mutable partial result。每次 Build 首先验证 WorldSnapshot/StructureSnapshot base version 和包含 isolated nodes 的完整 NodeId universe；preflight 失败不得调用任一 child planner。随后固定按 routing、MAC、final compose 顺序执行，任一 child error 或输出 provenance mismatch 都使整个 Build complete-or-error，禁止 fallback、partial ProtocolCyclePlan 或跳过失败阶段；合法输入可在同一组合器上重试。

组合器再次核验 RoutingPlan 的 cycle/base 与 StructureSnapshot 一致，并核验 MAC CycleTiming 的 cycle 与 StructureSnapshot、base 与 StructureSnapshot/WorldSnapshot 一致。通用组合器不要求 `CycleTiming.starts_at == WorldSnapshot.committed_at`；该关系是 ConfiguredTdmaMacPlanner 的具体策略，不是通用 M4 composition invariant。RoutingPlan 与 MacPlan 保持正交：组合器不根据 route presence 过滤 TxOpportunity，因此无 route 的 sender 仍可拥有 opportunity。PlanInstaller 继续只消费 timing/MAC，RoutingPlan 到 packet selection、next-hop 和 TransmissionTarget 的绑定留给 P0-S1-04D TxStart resolver。

### 2.28 M2 packet queue、FIFO selection 与 transmission target resolution

Packet queue 是 M2 runtime-internal side state，不进入 public contracts。`PacketQueueStore` value-own canonical NodeId universe 和每节点 FIFO queue；queue owner 表示当前持有 packet 并准备 forwarding 的节点，与 `DigitalPacket.source_node_id` 表示的端到端原始 source 相互独立。Enqueue 必须验证 owner、packet source 和 unicast destination 属于固定 universe；同一 PacketId 只禁止在同一 owner queue 内重复，不建立网络范围的全局唯一性约束。Queue 暂不进入 WorldSnapshot formal state，snapshot/checkpoint integration 保持 TBD。

P0 selector baseline 是 strict FIFO。`FifoPacketSelector` 只复制选择 TxOpportunity sender queue 的 front packet，不消费、不重排、不做 route lookup，也不因 front packet 无 route 而扫描后续 packet。Empty queue 是正常 NoPacket/opportunity-unused outcome。Selection 和 Ready preparation 必须 value-own DigitalPacket，不保存 deque iterator、element reference 或 queue pointer；实际消费必须使用 expected PacketId 条件删除，并留到未来成功创建 physical TransmissionSession 之后。

Target resolution 只读取 RoutingPlan，不回退到 ConnectivityGraph 或 LogicalTopology。Broadcast packet 直接解析为 BroadcastTransmissionTarget，不要求 routing binding；unicast packet 使用 `RoutingPlan.FindNextHop(TxOpportunity.sender_node_id, end-to-end destination)`，命中结果是 current-hop UnicastTransmissionTarget，不一般等于 end-to-end destination。RoutingPlan binding 缺失是 `kFailedPrecondition`，而 binding 存在但 route entry 缺失是正常 NoRoute/opportunity-unused outcome。Selected queue owner 与 opportunity sender 不同、或 outgoing unicast destination 等于 sender，均为 runtime invariant violation。

Selection、resolution 和 P0-S1-04D1 preparation 全部不修改 queue，也不调用 TransmissionExecutor、PHY、Channel 或 scheduler。TransmissionTarget 只表达 protocol/link-layer send intent，与未来 physical candidate receiver enumeration 保持分离。未来执行顺序冻结为 `Select -> Resolve -> Execute -> successful physical TransmissionSession -> conditional ConsumeSelected once`，其 TxStart integration、candidate receiver fan-out 和消费绑定属于 P0-S1-04D2。

### 2.29 Plan-bound TxStart、physical candidate fan-out 与条件消费

`PlanBoundTxRuntime` value-own 已安装的 immutable `ProtocolCyclePlan`，因此 caller 销毁原始 plan 不影响后续 TxStart。创建时必须把 plan 的 PlanningCycleId、base SnapshotVersion 和包含 isolated node 的完整 NodeId universe 分别绑定到 `CycleWorkingState` 与 `PacketQueueStore`；任一不一致均在事件执行前失败。每次 TxStart 必须是 plan MacPlan 中的精确 `TxOpportunity` 成员，并要求 dispatcher 传入的 `now` 精确等于 `eligible_at`，禁止近似时间、隐式 slot 或未计划发送。

P0 physical candidate resolver 只从 cycle base snapshot 的 canonical node order 枚举所有不同于 sender 且 `can_receive` 的节点。结果因此严格按 NodeId 升序，允许为空，并且不读取 TransmissionTarget、PacketDestination、RoutingPlan、ConnectivityGraph 或 LogicalTopology。Unicast 与 broadcast 使用同一 shared-channel fan-out 规则：target 是 current-hop protocol/link intent，不是唯一 physical receiver，也不是 candidate filter。

TxStart 固定执行 `Prepare -> ResolveCandidates -> CycleSignalRuntime::HandleTxStart -> eventize完整TransmissionSession -> ConsumeFront(expected PacketId)`。NoPacket 与 NoRoute 是成功但 opportunity-unused 的正常 outcome；routing binding 缺失及其他 invariant violation 仍是 error。Encode、channel fan-out、session construction 或 eventization 任一失败时不得消费 queue；只有完整 session 已成功建立并成功登记全部 signal lifecycle events 后才条件消费一次 queue front。为保持 runtime 不依赖 kernel，eventization 通过 runtime-internal sink interface 由 assembly/kernel-side adapter 实现，runtime 不 include kernel、ns-3 或 planning internal header。

如果 physical execution 与 eventization 已成功后，expected PacketId 条件消费仍失败，该状态表示不可恢复的内部并发/invariant violation，SimulationRun 必须 fatal；P0 不 rollback 已注册事件，也不伪造 queue 恢复。Zero-delay propagation 仍会因同 timestamp phase 回插而在 eventization 阶段失败，并保持 packet 未消费。Packet queue 仍位于 WorldSnapshot 之外，其 checkpoint/restart 与 authoritative persistence 保持 TBD；本阶段也不实现 Rx forwarding、energy accounting 或 strict slot-duration enforcement。

### 2.30 Directed minimum-hop shortest-path-to-sink routing

P0 的 `ShortestPathToSinkRoutingPlanner` 只在当前 `StructureSnapshot::logical_topology()` 的 directed edges 上计算到唯一 sink 的 minimum-hop route。该具体 planner 要求 RoleTable 恰好包含一个 `ProtocolRole::kSink`；RoleTable 的通用 contract 仍允许零个或多个 sink。除识别 sink 外，planner 不按 MEMBER、RELAY、CONTROLLER、ACCESS_NODE、ANCHOR 或 role absence 过滤 forwarding node。

Planner 从 sink 在 reversed logical graph 上执行一次 BFS，得到每个 node 到 sink 的 hop distance；原图 `A -> B` 只允许 A 通过 B 接近 sink，不隐含 `B -> A`。Unreachable 和 isolated node 是合法 no-route 状态，不使 Build 失败；sink 自身不产生 self route。Directed cycle 必须通过已访问 distance 安全终止，无法到达 sink 的 cycle 不产生 route。

对于 reachable node X，next hop 必须是原始 LogicalTopology 中满足 `distance[H] == distance[X] - 1` 的 outgoing neighbor。不同 hop count 时 shorter path 优先；多个 equal-hop neighbor 中显式选择 NodeId 最小者，不依赖 BFS discovery、caller edge order、unordered iteration、pointer address 或随机数。输出仍为现有 next-hop `RoutingPlan`，并通过 `RoutingPlan::Create` 完成 canonicalization、provenance 和 logical-edge validation。

Planner 每次 Build 只使用局部 adjacency、distance、frontier 和 entries，保持 deterministic、complete-or-error 且失败后可安全重试。本阶段不引入 edge weight、full-path contract、ECMP、随机负载均衡、ConnectivityGraph fallback、distance/delay/TL/SNR/energy metric，也不修改 runtime relay/queue 行为。

### 2.31 Transmission record 与 reception network disposition

`ReceptionSession` 继续表达 receiver-specific physical reception result，不承载完整 `DigitalPacket` 或 current-hop `TransmissionTarget`。Rx network disposition 必须以 Reception 的 TransmissionId 查询 runtime-owned `TransmissionRecordStore`，不得根据 PacketId/sender/receiver 猜测，也不得使用当前 RoutingPlan、LogicalTopology 或 ConnectivityGraph 重新解释过去的 transmission。`TransmissionRecord` value-own 恰好一个完整 DigitalPacket 和对应 Transmission，只验证二者 PacketId 一致；packet 的 end-to-end original source 与 transmission 的 current-hop sender 可以不同，基础 record contract 不强制 destination 与 target 组合一致。

TransmissionRecord 按 TransmissionId 唯一注册，duplicate registration 返回 `kAlreadyExists`，missing lookup 是 runtime invariant failure 而不是 NotDecoded、Overheard 或 NoRoute。Record 至少从 successful TxStart 保留到该 transmission 的全部 ReceptionSession finalize 完成；实际 TxStart registration、SessionFinalize disposition wiring 与 closed-cycle cleanup 留给 P0-S1-05B2。

`ReceptionDispositionService` 是无 side effect 的纯判定。它在任何正常 outcome 前验证 Reception、RxDecodeResult、desired ReceivedSignal 及 TransmissionRecord 的 TransmissionId、PacketId、receiver 和 current-hop sender provenance，并拒绝 self reception。Identity 合法的 `kNotDecoded` 是正常 `NotDecodedReception`；decoded physical reception 不自动等于 protocol acceptance。

对于 unicast，只有 receiver 等于 actual Transmission 的 current-hop target 才能接受 packet，其他 decoded receiver 均为 `OverheardReception`，即使它恰好是 end-to-end destination。Target 等于 end-to-end destination 时产生 value-owned `LocalDeliveryReception`，否则产生 value-owned `RelayEnqueueReception`；relay action 必须原样保留 PacketId、original source、destination 和 payload。匹配的 broadcast destination/target 对每个 decoded non-sender receiver产生 LocalDelivery，但不自动 relay、flood 或重新广播。Decoded 状态下的 unicast-target/broadcast-destination 或 broadcast-target/unicast-destination 返回 `kFailedPrecondition`。

P0-S1-05B1 只生成 disposition value，不修改 PacketQueueStore、WorldSnapshot、ledger 或 ReceptionResultAccumulator，不执行 application delivery、relay enqueue、Rx callback、ACK、retransmission 或 routing recompute。ReceptionResultAccumulator 继续保存全部 physical ReceptionSession，包括 NotDecoded、Overheard 和 accepted reception；这些 action 的应用与 record lifecycle integration 属于后续阶段。

### 2.32 Transmission record lifecycle 与 reception side effects

`CycleSignalRuntime` 在 `TransmissionExecutor` 已返回完整 `TransmissionSession`、且 transmission 与全部 received signal 均通过 closed-cycle boundary validation 后，为一次 physical send 注册恰好一个 `TransmissionRecord`，然后才把 session 返回给 `PlanBoundTxRuntime` 进行 lifecycle event publication。NoPacket、NoRoute、Tx/Channel failure 和 cycle-crossing rejection 均不注册 record；record create/register failure 是 SimulationRun-fatal runtime error，不得继续 publish 或消费 sender queue。Event publication 在 record registration 之后失败时，不 rollback 已形成的 record，sender queue 仍不消费。

`TransmissionRecordStore` 是 cycle-transient join state。Receiver finalize 必须以 `ReceptionSession.reception.transmission_id` 精确查询 record，并固定执行 `ReceiverProcessor::ProcessReceivedSignal -> ReceptionDispositionService::Decide -> ReceptionResultAccumulator::Append -> ReceptionDispositionApplier::Apply`。Decide 失败时不 append physical result 且不执行 network side effect；Decide 成功后所有 NotDecoded、Overheard、LocalDelivery 和 RelayEnqueue 的合法 physical `ReceptionSession` 都先进入 accumulator。后续 Apply 失败是 SimulationRun-fatal，但不 rollback 已累积的 reception、发送端消费或此前 signal lifecycle state。

`NotDecodedReception` 与 `OverheardReception` 没有 network side effect。`LocalDeliveryReception` value-own ReceptionId、TransmissionId、receiver NodeId 和完整 DigitalPacket，并写入 runtime-owned `ApplicationDeliveryStore`，不得进入 receiver outgoing queue。该 store value-own canonical NodeId universe，只按 ReceptionId 禁止重复；同一 PacketId 经不同 ReceptionId/TransmissionId 到达同一或不同 receiver 均合法，broadcast 的 per-receiver deliveries 也不得按 PacketId 去重。

`RelayEnqueueReception` 把完整 DigitalPacket 同步写入 receiver 的 `PacketQueueStore`，保持 PacketId、end-to-end original source、destination 与 payload 不变。enqueue 在 finalize callback 返回前对同周期后续 TxOpportunity 立即可见，不触发 M3/M4 replan，也不增加 ready-at 或 next-cycle-only 状态。同一 owner queue 的 duplicate PacketId 继续由 PacketQueueStore 返回 error；duplicate suppression 留给后续独立 policy。目标节点 NotDecoded 不恢复已经因 successful physical send 消费的 sender packet；ACK、ARQ 和 retransmission 尚未实现。

CycleClose 固定先验证 ledger、finalize delta 并成功 CommitCycle，再依次清理 in-flight ledger 与 TransmissionRecordStore。Commit failure 保留 records；零 receiver transmission 仍注册一个 record 并在成功 close 后清理。PacketQueueStore 与 ApplicationDeliveryStore 作为跨 cycle side-state 保留，当前不属于 WorldSnapshot formal state，因此 queue/application delivery 的 checkpoint/restart 完整恢复继续保持 TBD。

### 2.33 P0 multi-cycle multi-hop data-plane acceptance

P0 data plane 已通过 test-side per-cycle acceptance harness 证明，同一个 `DigitalPacket` 可以跨多个 PlanningCycle 依次 relay 并最终交付 sink。PacketId、end-to-end original source、destination 和 payload 在所有 hop 中保持不变；每次 physical hop 则独立拥有新的 TransmissionId、实际 current sender 和由当期 RoutingPlan 解析出的 current-hop TransmissionTarget。非 target 节点即使成功物理解码仍只形成 Overheard，不得产生 relay 或提前 local delivery。

WorldStateStore、PacketQueueStore、ApplicationDeliveryStore、CommunicationIdAllocator 以及确定性测试 PHY/provider 是 run-level state。每个 cycle 重新从最新 committed WorldSnapshot 创建 CycleWorkingState，并重新运行 StructureBuilder、ShortestPathToSinkRoutingPlanner、ConfiguredTdmaMacPlanner 与 CompositeProtocolCyclePlanner；StructureSnapshot、RoutingPlan 和 CycleTiming 的 cycle/base provenance 必须匹配该周期 authoritative snapshot。Relay queue 只在下一个正式 planning boundary 被下一周期计划使用，不在当前周期触发 M3/M4 recompute。

05C 的多周期执行只由 integration test 中的 per-cycle harness 依次创建新的 TransmissionRecordStore、CycleSignalRuntime、PlanBoundTxRuntime、EventDispatcher、PlanInstaller 和 CycleCoordinator。当前 production CycleCoordinator 仍是 single-cycle owner；本验收不增加 reset/reinstall、production continuous-cycle coordinator，不改变 same-time EventPhase floor，也不使用 `+1ns` boundary workaround。每周期使用上一 snapshot 的绝对 committed_at 作为 start，并在成功 close 后精确 Commit 一次。

Target decode failure 继续不恢复已消费的 sender packet；后续周期会正常观察到 NoPacket，ACK/ARQ/retransmission 不在 P0-S1-05C 范围。Commit failure 后既有 PacketQueueStore/ApplicationDeliveryStore side effects 当前不提供事务回滚，SimulationRun 按 fatal 处理。上述 run-level side-state 仍不属于 WorldSnapshot formal state，checkpoint/restart 因而继续不完整。

### 2.34 Production continuous-cycle ScenarioRuntime

P0-S1-05 数据面至此 CLOSED。Production continuous-cycle orchestration 属于 assembly composition root：`ScenarioRuntime` 组合 kernel、runtime、structure 与 planning，但这些模块不得反向依赖 assembly。`CycleCoordinator` 继续是 one-cycle owner，`EventDispatcher` 继续只负责一个 cycle/event dispatch epoch；每个 PlanningCycle 必须新建 EventDispatcher、PlanInstaller、CycleCoordinator 以及 CycleWorkingState、signal ledger、reception accumulator、transmission records、CycleSignalRuntime 和 PlanBoundTxRuntime 等 cycle-scoped owner，禁止给 coordinator 增加 Reset/Reopen/reinstall 语义。

一次 `RunCycles(N)` 使用同一个 caller-owned Ns3KernelGateway 和同一个 ns-3 Simulator lifetime。上一周期 `CycleClose@T` 已完成且该 dispatcher 的 Run 返回后，assembly 同步运行下一周期 M3/M4，此过程不推进 simulation clock；随后新 EventDispatcher 可以从同一精确 Platform time T 安装下一周期的 `TxStart@T`。EventPhase monotonicity 只约束单个 dispatcher dispatch epoch 内的 same-time dynamic insertion；跨 dispatcher 的正式 cycle boundary 不是 phase90 callback 中的回插。不得使用 `+1ns`，也不得放宽 same-time dynamic event 必须采用更晚 phase 的既有规则。整个 RunCycles 成功或失败退出时只 Destroy simulator 一次，cycle-scoped owner 不得自行 Destroy。

每个周期必须重新读取最新 committed WorldSnapshot，校验 kernel Now 不晚于 committed_at，并真正执行 `StructureBuilder::Build` 与 `IProtocolCyclePlanner::Build`。StructureSnapshot、CycleTiming 和可选 RoutingPlan 的 cycle id/base SnapshotVersion provenance 必须再次由 ScenarioRuntime 验证；plan start 必须同时不早于 snapshot committed time 和 kernel Now。WorldStateStore、PacketQueueStore、ApplicationDeliveryStore、CommunicationIdAllocator 与 previous ConnectivityGraph 是 run-level state。Previous graph value-owned 于 ScenarioRuntime，只把上一 successful cycle 的 graph传给下一次 M3，并且只在当前 cycle 成功 commit 与 post-run invariant validation 后更新；failed cycle candidate 不得污染 hysteresis history。

Production `Ns3TransmissionSessionEventSink` 只把完整 TransmissionSession 转换为 SignalArrival/SessionFinalize intents，不注册 record、不消费 queue、不判定 disposition、不 commit。它必须按 event time、EventPhase、receiver NodeId、TransmissionId 显式形成 deterministic order，并通过一次 `ScheduleBatch` 发布完整 session 的 lifecycle events。零传播延迟在 TX_START@T 动态生成 SIGNAL_ARRIVAL@T 时仍按既有 rule 返回 `kFailedPrecondition`，禁止 inline arrival、phase rewrite 或时间平移。Production execution hook 仅把 PlanBoundTxRuntime 的 Executed、UnusedNoPacket、UnusedNoRoute 正常 outcome 映射为 success，并把 CycleClose 委托给 CycleSignalRuntime；runtime Error 原样传播。

`RunCycles` 只允许从 Ready 进入 Running，成功后为 Completed，任一 fatal error 后为 Failed；Completed/Failed object 均不得原地 retry、resume 或 reset。N=0 返回 `kInvalidArgument`；`first_cycle_id + N - 1` 必须在任何 cycle 执行前 checked preflight，溢出返回 `kOverflow`。每个 successful cycle 仍是独立正式 transaction，必须由 CommitService 完成一次 authoritative `V -> V+1`，并在 Run 返回后验证 coordinator completed、committed time、last cycle id、空 ledger 和空 transmission record store。整个 RunCycles 是 prefix-commit 而非全 run atomic：后续周期失败时保留之前成功 commit，立即停止且不 retry、不 skip、不进入再后续周期。Current-cycle queue/delivery 等 side-state rollback、checkpoint/restart 与 failed-run recovery 继续保持 TBD。

### 2.35 P0-S2 cross-module PHY provider integration

P0-S1 core runtime 已 CLOSED，P0-S2 开始在 integration branch 上进行跨模块 provider 合流。`Platform::phy` 只依赖 `Platform::contracts`，不得依赖或 include runtime、kernel、planning、structure、assembly、ns-3、Bellhop 或 ScenarioRuntime；其他模块不得为了接入旧 PHY 实现而反向修改冻结的 runtime execution semantics。

DigitalPacket 继续只表达 PacketId、end-to-end source/destination 和 payload 信息，不增加 waveform sample、symbol、sample rate 或 carrier state。WaveformBuffer、BitFrame、modulation/demodulation、multipath application、waveform noise synthesis、BER/PER statistics 和完整 waveform pipeline 都保持 PHY internal。TxEmission 继续是 runtime 所需的 Tx physical metadata，不携带完整 waveform，也不建立 global TransmissionId-to-waveform cache。

Production Tx-side adapter 必须实现既有 `ITxPhy::Encode(DigitalPacket, TxEncodeRequest) -> TxEmission`，并保持 TransmissionId、PacketId、sender 和 started_at provenance。一次 physical send 只调用一次 Tx PHY 和一次 modulation；receiver fan-out 在 TxEmission 创建之后通过独立 ChannelQuery 完成，receiver 数量不得重复 payload serialization、modulation 或 sender-side Encode。Configured waveform Tx adapter 可在内部生成 waveform 以确定 duration 和 metadata，但 waveform 生命周期不得泄漏到 runtime。

`IChannelFieldProvider` 继续独占 position/frequency/time 到 TL/delay/path 的 provider boundary；PHY channel processor 只能把既有 ChannelFieldResponse 作用于 internal waveform，不决定 topology、receiver set 或运行 Bellhop。`INoiseFieldProvider` 继续提供 runtime packet/scalar NoiseObservation；internal waveform noise synthesis 不能替代或改变该 contract。所有进入 production provider path 的结果对 equal request/configuration 必须 deterministic；random_device、wall-clock seed、implicit/global RNG 禁止使用，waveform noise 只允许显式 deterministic seed/state。

现有 RxDecodeRequest 只包含 ReceiverWindow 和 NoiseObservation，不能承载原始时域 waveform。因此本阶段不把 waveform demodulator 伪装为 production IRxPhy，也不扩张 ReceivedSignal、RxDecodeRequest 或 Channel contracts。Full waveform propagation/Rx binding 如需 waveform handle、sample provenance 或 explicit deterministic RNG contract，必须后续独立 ADR 和 contract review；Bellhop provider、环境资产与高级 MAC/routing 同样不属于本阶段。

### 2.36 P0 normalized static acoustic-field provider

P0 runtime environment data 固定为每个 run 一个 normalized、immutable `AcousticFieldAsset`。Asset 拥有显式 frequency、source-depth、receiver-depth、horizontal-range axes，以及固定的 frequency-major → source-depth → receiver-depth → range cell layout。每个 cell 显式区分 signal 与 no-arrival；signal cell 包含 aggregate transmission loss、integer-nanosecond first-arrival delay 和既有 contract `PropagationPath`，no-arrival cell 不保存 TL/delay/path sentinel。Runtime provider 通过 immutable shared lifetime 持有 asset；asset 不保存 node/transmission identity，provider 不使用 random、cache-dependent result、wall clock、filesystem、network 或 subprocess。

Raw Bellhop `.env`/`.bty`/`.arr` parsing、Bellhop execution、WOA/Argo/GEBCO acquisition 和 manifest/HDF5/NetCDF persistence 属于 offline import/build pipeline。`AcousticFieldChannelProvider::Query` 只消费 normalized in-memory asset；它不选择 candidate receiver，不决定 topology/routing，不运行 Bellhop。Query 越出 asset domain 时明确失败，禁止 clamp、extrapolate、free-space/fixed-loss fallback 或 synthetic direct path。

Depth conversion 必须显式配置 `surface_z_meters` 和 positive-up/positive-down vertical-axis direction，禁止从 `Position3d.z_meters` 正负号猜深度。Bellhop-style P0 range 固定为 `hypot(tx.x-rx.x, tx.y-rx.y)`，不是 3D slant range，因为 source/receiver depth 已是独立 field dimensions。P0 asset 是 range-only/axisymmetric，不使用 bearing/azimuth；azimuth-dependent 3D field 留待后续设计。

Frequency selection 是 discrete profile selection：选择与 query center frequency 绝对差最小的 profile，等距时选择较低 frequency，并且 offset 必须不大于显式配置的 finite non-negative maximum。`ChannelQuery.bandwidth_hz` 仍是合法 metadata，但不触发 multi-profile averaging 或 broadband integration。`ChannelQuery.emitted_at` 当前不选择或插值 environment snapshot；因此相同空间/frequency query 在不同 simulation time 的 physical values 相同，而 response transmission/receiver identity 仍逐 query 继承并验证。

选定 frequency profile 后，在全 signal interpolation stencil 内，aggregate TL 与 first-arrival delay 在 source depth、receiver depth、horizontal range 上做 trilinear interpolation。Delay 以 `long double` nanoseconds 插值，checked `int64` range，并 round 到 nearest integer nanosecond。Multipath vector 不做 cell 间 matching 或 interpolation，完整取自 nearest spatial cell；每个维度等距时选择较低 axis coordinate/index。因此 P0 有意采用 mixed-resolution response：interpolated scalar first-arrival delay 配合 nearest-cell relative-excess-delay paths。Coverage discontinuity 的额外规则由 2.39 冻结。Scalar 与 path representation 继续是互斥处理选择，不得同时作用于同一 waveform transfer。Coherent path-field interpolation、broadband integration、time-varying field、raw asset persistence 和 acoustic-field-based M3 feasibility estimator 均留待后续。

### 2.37 Bellhop ASCII raw arrival offline import

Bellhop raw `.arr` 属于 environment 的 offline import 输入；runtime provider 只消费 normalized in-memory `AcousticFieldAsset`，不得解析 `.arr`，也不得在线运行 Bellhop。P0 只支持经 legacy 实文件、Python parser 和直接 token inspection 确认的、以精确 quoted `'2D'` header 开始的 2D ASCII arrival dialect；binary、3D 和旧式无明确 header 的格式均不支持。

Import caller 必须通过 `BellhopReceiverRangeUnit` 明确声明原始 receiver-range axis 使用 meters 或 kilometers，parser 禁止根据数值、行数或文件长度猜测单位。文件 frequency 字段按 grammar 直接作为 Hz 保存，不受旧 preprocess CLI 输入单位影响。Raw dataset 的 source-depth、receiver-depth 和转换后的 receiver-range axes 必须 non-empty、finite、non-negative、strictly increasing，禁止 parser sort、clamp 或 canonical repair；最终 cell order 固定为 source-depth → receiver-depth → receiver-range。

`BellhopRawArrival` 无损 value-own 文件中的 raw magnitude、degrees phase、real/imaginary seconds delay、source/receiver angles 与 top/bottom bounce counts。Raw magnitude 只验证 finite/non-negative，暂不解释为 absolute pressure gain、TL 或其他新平台物理量；phase 原样保留 degrees，不 wrap 或转换 radians；complex delay 的 real/imaginary parts 全部保留，signed imaginary delay 不得在 parser 层丢弃或解释。`Narr == 0` 是合法 empty cell，不生成 synthetic arrival、fallback TL 或 fallback path。

`BellhopRawArrivalBundle` 只组织 frequency strictly increasing、spatial axes 完全一致的多个单频 raw datasets，不执行 resample、interpolate、nearest merge、coherent sum、aggregate TL、first-arrival selection、CIR 或 `PropagationPath` 构造。`RawArrival -> AcousticFieldAsset` normalization 属于下一阶段独立的 physics/contract review；P0-S2-03 到 raw bundle 为止。

### 2.38 Explicit channel no-arrival outcome

一个合法 `ChannelQuery` 可以正常得到 `ChannelFieldResponse` 或 `ChannelNoArrival`。两者组成通用、非 Bellhop-specific 的 `ChannelFieldOutcome`，并都必须保留 query 的 TransmissionId 与 receiver NodeId provenance。`ChannelNoArrival` 表示 provider 正常完成、但该 receiver 没有任何可进入 signal lifecycle 的 physical arrival；它是 normal physical outcome，不是 `Error`，也不携带 synthetic TL、delay、path 或 provider metadata。Provider `Error` 继续只表达 invalid query、out-of-domain、configuration/provider failure、corrupt asset 或 unsupported request 等真正失败；out-of-domain 不得转换为 no-arrival。

Candidate receiver enumeration 继续只依据 node universe、sender exclusion 和 receive capability，独立于 channel outcome；`TransmissionTarget` 也不裁剪 physical candidate fan-out。Runtime 对每个 candidate 完成 ChannelQuery 和 outcome identity validation：response 创建一个具体 `ReceivedSignal`，no-arrival 不创建 signal 并继续处理后续 candidate。No-arrival 不进入 SignalArrival、SessionFinalize、NoiseQuery、RxDecode 或 ReceptionSession pipeline，也不产生 placeholder `NotDecodedReception`；它与已有 physical signal 进入 Rx 后得到 `DecodeOutcome::kNotDecoded` 是不同语义。

一次 physical send 因而允许产生 0..N 个 `ReceivedSignal`。即使所有 candidates 都是 no-arrival，该 send 仍只有一个 TransmissionSession、TxEmission、Encode 和 TransmissionRecord；session 的 received-signals 可以为空，event sink 必须正常接受空 lifecycle batch。Successful TxStart 仍消费 sender packet 恰好一次，CycleClose/Commit 正常完成；P0 未实现 ACK/ARQ，不恢复该 packet。

P0-S2-04B 起 normalized `AcousticFieldAsset` 显式表达 identity-free no-arrival cell，`AcousticFieldChannelProvider` 按 2.39 的 coverage policy 返回 `ChannelFieldResponse` 或带 query identity 的 `ChannelNoArrival`。该映射不把 channel outcome 接入 M3 feasibility estimation。

### 2.39 Bellhop raw-arrival normalization and coverage-aware asset

Bellhop 2D raw normalization 采用 Acoustics Toolbox/Bellhop 上游 coherent contribution semantics。核对依据为 legacy mirror `third_party/bellhopcuda` commit `b396d40ba49c2f349258b9687cfae8ff8323828f`：`src/influence.hpp::ApplyContribution` 的 coherent field contribution 为 `amplitude * exp(-i * (omega * complex_delay - phase))`，`src/mode/arr.cpp` 将内部 phase radians 转成 degrees 并把 complex delay 的 real/imaginary parts写入 ASCII `.arr`。因此在 `omega = 2*pi*frequency` 下，raw arrival 的 effective linear pressure gain 固定为 `magnitude * exp(omega * imag(delay))`，raw phase degrees 只转换为 path phase radians，不 wrap；real delay 独立表达 propagation time，禁止把 `-omega * real(delay)` 再写入 `PropagationPath.phase_radians`，避免 carrier propagation phase 双重计数。

Normalizer 所有关键中间计算优先使用 `long double`。Angular frequency、imaginary-delay exponent、gain、phase、delay conversion 与 aggregate 必须 finite；非零 raw magnitude 若在 double runtime representation 中 overflow/underflow 为 infinity/zero，必须明确返回 Error，禁止 clamp 到 `DBL_MIN`/`DBL_MAX`、silent zero 或 infinity。Real delay 转为 `seconds * 1e9` 后 checked `int64` range，并采用 `std::round` 的 nearest-nanosecond、half-away-from-zero 规则；Bellhop real delay 非负，因此 0.5 ns tie 向上。每个非空 cell 先对每条 absolute delay 独立取整，再取 minimum 作为 first arrival；每条 `PropagationPath.excess_delay` 是 absolute integer delay 减 first arrival 的 checked difference。Raw angle、bounce count 和 imaginary delay 不进入 public `PropagationPath`，raw dataset 继续作为 provenance/source。

P0 aggregate transmission loss 是 incoherent power-equivalent pressure fallback：对 normalized effective gains `g_i` 用 hypot-style accumulation 得到 `g_agg = sqrt(sum(g_i^2))`，再计算 `TL = -20*log10(g_agg)`。它不表达 coherent narrowband cancellation；coherent/path-aware processing 由 paths 的 gain、phase 与 delay 表达，且不得同时再应用 scalar TL。改变 path phase 不改变该 scalar aggregate。`Narr > 0` 但 aggregate gain 为零是 explicit normalization Error，不得改写成 no-arrival；`Narr == 0` 才映射为 asset 内 identity-free no-arrival cell。

`BellhopRawArrivalBundle` 的 frequency、source-depth、receiver-depth、receiver-range axes 按既有 canonical order 原样映射到 asset，禁止 sort、resample 或 interpolate。Normalizer caller 必须显式提供 `EnvironmentCoordinateFrame`（surface z 与 vertical direction），不得从 Bellhop positive-down depth axis 猜测 Platform z 方向。Persistence、Bellhop execution、WOA/GEBCO/SSP acquisition 仍不属于 runtime normalization。

Provider 先按既有 discrete frequency tolerance 选择 profile，再以 source depth、receiver depth、horizontal range 各自的 nearest lower-tie index决定 coverage。Nearest cell 为 no-arrival 时正常返回携带 query TransmissionId/receiver NodeId 的 `ChannelNoArrival`；nearest cell 为 signal 且 scalar interpolation stencil 全为 signal 时，继续 trilinear interpolation TL/first-arrival 并取 nearest-cell paths；nearest cell 为 signal 但 stencil 含 no-arrival 时，完整采用 nearest signal cell 的 TL、first-arrival 和 paths，不跨 coverage boundary 插值或填 synthetic TL。Out-of-domain 与缺失 frequency profile 始终是 Error，绝不能变成 no-arrival。该 asset 是 static：emitted_at 不改变 signal/no-arrival classification。所有路径禁止 synthetic direct path、infinite-TL sentinel、silent clamp 或 fallback。

## 3. 影响

### 3.1 正向影响

- 首次建立真正基于 ns-3 的离散事件内核，不继承旧 fixed-step 运行债务。
- contracts 保持轻量，runtime owner 不会通过公共 DTO 泄漏。
- Trace 不会形成指向所有业务模块的依赖中心。
- 广播与多播从结构上保证“一次物理发送、多个接收后果”。
- 同时刻事件具有明确、可复现的因果顺序。
- closed-cycle 降低 P0 状态提交和跨周期会话管理复杂度。
- Bellhop 查询错误与合法无到达结果被明确区分。

### 3.2 约束和成本

- runtime 必须实现只读 View/Delta 边界，不能把 owner 直接暴露给 planner 或 PHY。
- M1 必须提供稳定 phase 调度机制，同时仍以 ns-3 为唯一事件底座。
- Planner 和 Runtime 需要验证一次动作能否在 CycleClose 前完整结束。
- 旧场景和前后端不能直接调用 P0-S0/P0-S1，需要等待 LegacyScenarioConverter 和新 API。
- 旧 Bellhop 查询的 clamp、nearest-frequency 和 synthetic-direct-path 行为必须在迁移时删除或显式转为错误。

## 4. P0-S0/P0-S1 评审检查项

### P0-S0

- contracts 中不存在可写 `CycleWorkingState` owner。
- contracts 中不存在可写 `ProtocolKnowledgeStore` owner。
- `trace.hpp` 只依赖 time/identity/error-status 和 Trace 自有 payload。
- `SimTime/SimDuration` 为 `int64` nanoseconds 强类型，未暴露 `ns3::Time`。
- NodeId 构造和校验允许 0。
- Bellhop/Channel 错误模型能表达四种冻结状态。
- 里程碑、测试和文档不再把 M1-M8 当开发阶段名称。

### P0-S1

- ns-3.47 仅引入事件内核所需最小能力。
- 没有自建全局 EventQueue、fixed tick 或全局 MAC busy state。
- 同 timestamp 事件严格按冻结 EventPhase 和稳定 ID 执行。
- 当前 timestamp 动态事件不能回插已执行 phase。
- Cycle0 与后续周期使用同一路径。
- CycleClose 前 active Tx/Rx session 数量为 0。
- PacketReady 只入队，不触发 M3/M4 重规划。
- 广播 invariant 的九项计数全部精确满足。
- 一次 TransmissionSession 只有一条 Transmission trace，各 Reception 独立记录。

## 5. 仍未决定的事项

本 ADR 不决定以下事项：

- PHY/Waveform 的调制、同步、解调、DAC/ADC、时长、源级、门限和能耗细节；
- BIN 除 16 bit 外的 signedness、endianness、采样率、实数/IQ、载频和帧格式；
- Preamble、CRC、FrameCodec 和 FrameDetector；
- 噪声的带宽积分、量纲、参考量和场景 preset；
- 严格实时 HIL、主机时钟同步和迟到数据策略；
- AS-MAC/AB-MAC 的首个复现优先级和 AlgorithmMemory schema；
- 声场轴采样密度、最终存储、压缩/分块和 3D/方向性扩展；
- M3 的候选距离、质量阈值和滞回参数；
- 各协议 CycleTiming/guard time 的默认配置；
- kernel 对外最终使用 `ScheduleAt` 还是同时提供命名明确的 `ScheduleAfter`；
- Trace sink 的缓冲、背压、降级和落盘策略；
- ns-3.47 的具体获取方式、编译器和 CI 交付矩阵。
- zero-delay propagation 在 TX_START 同刻进入 SIGNAL_ARRIVAL 的未来 eventization 方案。

这些事项保持 TBD/配置化/后续 ADR，不得由实现人员写成不可替换的隐式默认值。
