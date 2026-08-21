# ADR-0001：Runtime Foundation Decisions

- 状态：Accepted / Frozen for P0
- 日期：2026-08-17
- 适用范围：P0-S0 Contracts Freeze、P0-S1 Core Closed Loop，以及架构模块 M1/M2/M3 的基础边界
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
