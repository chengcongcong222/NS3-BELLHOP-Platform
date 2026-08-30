# ADR-0001：Runtime Foundation Decisions

- 状态：Accepted / Frozen for P0；P0-S3、P0-S4-01、P0-S4-02、P0-S4-03、P0-S4-04、P0-S4-05 CLOSED
- 日期：2026-08-17
- 适用范围：P0-S0 Contracts Freeze、P0-S1 Core Closed Loop、P0-S2 Cross-Module Provider Integration、P0-S3 Trace/Acceptance Scenario，以及 P0-S4 Application Boundary
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
- P0-S2 Cross-Module Provider Integration；
- P0-S3 Deterministic Read-Only Trace Core；
- 后续 provider、environment 与 productization 阶段按独立任务冻结；
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

### 2.40 Canonical environment asset package and repository

P0 normalized acoustic field 的持久化格式固定为一个包含 `manifest.txt` 与 `field.bin` 的 canonical package directory。Manifest 使用严格、有固定字段顺序的 `key=value` text grammar，必须声明 `NS3_FACTORY_ACOUSTIC_FIELD` format identifier、package/asset format version、producer/source/normalization provenance、coordinate frame、四条 axis 的 unit/count、cell/signal/no-arrival count、payload byte count 和 checksum。Unknown format/version、缺失/多余/乱序字段或不支持的 unit/checksum 必须明确 reject；producer 不得隐含用户名、机器绝对路径或旧 repository path，Bellhop import 必须记录 raw source logical name 与 normalization policy version。Writer 不自动加入 wall-clock timestamp；相同 asset 与 provenance 必须产生完全相同的 manifest 和 payload bytes。

`field.bin` 固定使用 little-endian fixed-width integers 与 IEEE-754 binary64，不 raw-dump C++ struct/variant。Payload 依次保存独立 magic/version、asset format/provenance、coordinate frame、四个完整 axis value vector，以及 frequency-major → source-depth → receiver-depth → range 的逐 cell 数据。Signal cell 显式编码 aggregate TL、signed int64 first-arrival nanoseconds、path count，并对每条 canonical path 编码 signed int64 excess-delay nanoseconds、pressure gain 与 phase；NoArrival cell 只编码独立 kind，不保存 synthetic TL/delay/path。Loader 必须验证 magic/version、count arithmetic、enum、finite/domain/canonical path rules、cell count、truncation、unexpected trailing bytes 和 manifest/payload consistency，并把 decoded value 再交给 `AcousticFieldAsset::Create` canonical validation；禁止 partial load、repair、resort 或 renormalize。

Payload checksum 固定为 deterministic FNV-1a 64，仅用于 accidental corruption detection，不是 cryptographic integrity 或 authentication guarantee。Checksum mismatch 必须拒绝 package；安全防篡改、签名和 trust distribution 留待后续独立设计。

`EnvironmentAssetId` 是 repository-internal stable identity，不是 filesystem path，只允许受限的单 path-component 字符集，禁止 absolute path、separator injection 与 `..` traversal。同 ID 已存在返回 `AlreadyExists`，不自动改名或覆盖；registered package immutable，更新必须使用新 ID/version。Repository root 由 application/assembly 显式提供，repository 可 register/load/list/find validated packages，但不把 root/package path 暴露为 domain identity。Register 必须先在同一 repository root 的保留临时目录完成 write 与 loader validation，再以 rename 发布最终 AssetId；正常 API failure 必须清理自己创建的临时状态且不得留下可见 final ID。Power-loss durable transaction、`fsync` protocol 与 multi-process concurrency guarantee 仍不属于 P0。

`AcousticFieldChannelProvider` 仍只持有 `shared_ptr<const AcousticFieldAsset>`。Scenario/application assembly 必须在 `ScenarioRuntime` 开始前通过 repository load/validate 一次；TransmissionExecutor → `IChannelFieldProvider::Query` 热路径禁止 filesystem、repository、network 或 subprocess I/O。HDF5、NetCDF、SQLite、cryptographic integrity、Bellhop subprocess、WOA/GEBCO acquisition 和 in-place registered-asset update 均不在本 P0 package 范围。

P0-S2 至此 CLOSED：configured Tx PHY、waveform internal pipeline、normalized acoustic-field provider、Bellhop raw import/normalization，以及 canonical environment asset package/repository 已通过各自冻结测试。Runtime hot path 继续只消费已装配的 contracts provider，不反向依赖 PHY 或 environment 实现。

### 2.41 Deterministic read-only trace core

P0-S3-01 的 trace 边界固定在 `contracts/trace.hpp`。该 header 只依赖 Platform time、strong identity、error/status 与 trace 自有的小型只读摘要；不得嵌入 WorldSnapshot、ProtocolCyclePlan、runtime owner、provider response 大对象或 ns-3 类型。`TraceEvent` 由 `occurred_at`、与 payload 一致的 `TraceKind` 和四种 typed payload 组成：CycleCommit、Transmission、ChannelOutcome、Reception。Transmission target 使用 trace 自有 Unicast/Broadcast 摘要；ChannelOutcome 使用 Signal/NoArrival variant，NoArrival 不保存伪造 TL、delay 或 path；Reception 只保存最终 NotDecoded、Overheard、LocalDelivery 或 RelayEnqueue disposition。

Trace emission 固定为 causal result 之后的 best-effort 只读旁路。Transmission trace 每个成功 TransmissionSession 精确一条，且只在完整 receiver fan-out、session event batch publication 与 sender queue conditional consume 全部成功后发送；随后按 canonical candidate receiver 顺序为每个 receiver 发送一条 ChannelOutcome trace。Provider 在 receiver loop 中途失败时，此次 transmission/channel success trace 必须全部为零。Receiver trace 只在 reception finalize、disposition decision 与 disposition application 成功后发送。CycleCommit trace 只在 CommitService 已完成 authoritative `V -> V+1` 后发送。广播仍是一条 Transmission trace、每个 candidate 一条 ChannelOutcome trace、每个实际 ReceptionSession 一条 Reception trace。

`ITraceSink::Emit` 是 `noexcept -> Status`，但 causal caller 必须无条件忽略该 Status。NullTraceSink 是默认 assembly composition；测试可使用 recording 或 always-fail sink。Sink failure 不得改变 snapshot/version/time、packet queue、delivery、ID allocation、cycle outcome、runtime terminal state 或事件顺序。Core 不增加全局 trace sequence number；输出顺序直接沿用既有 deterministic simulation order，包括 cycle order、EventPhase、canonical NodeId fan-out 和稳定 communication identity。

Production core 不实现 buffer、backpressure、retry、persistence、serialization、SSE/WebSocket、metrics aggregation 或 frontend projection policy。`runtime` 只依赖 contracts trace，不依赖 observability；`observability` 只依赖 contracts；具体 sink 由 assembly 注入。上述产品化策略继续保持后续独立设计。

P0-S3-01 至此 CLOSED：上述 typed trace、causal emission、best-effort sink isolation 和 deterministic ordering 已通过 OFF/ON 测试冻结。

### 2.42 Acceptance scenario dual-timescale TDMA execution

一个 `PlanningCycle` 继续严格表示一个 communication cycle，不引入第二种 cycle identity。World authoritative state 在每个 communication cycle 都执行一次正式 Commit，保持逐周期 `Vn -> Vn+1`；`NetworkUpdateIntervalCycles` 只控制正式 M3 structure 与 applied M4 MAC schedule 的低频更新，不改变 Commit 频率、simulation time 或 `PlanningCycleId` 语义。以 interval 10 为例，第一、十一、二十一等周期刷新 M3，其余周期使用最近一次已生效的 RoleTable、ConnectivityGraph 与 LogicalTopology，并以当前 cycle/version provenance 重新绑定。

M4 planner 可以每个 communication cycle deterministic 地建立 `CandidatePlan`。只有 network-update cycle 的 candidate MAC schedule 在该周期成功 Commit 后成为新的 applied schedule；非 update cycle 仍使用最近一次 applied schedule 的 slot owner、relative offset 与 cycle duration，同时使用当前周期 candidate 的 routing/provenance。未施加 candidate 不改变 queue、WorldSnapshot、Transmission 或 Reception，也不产生 causal state Trace；未来 planner diagnostics 如有需要必须使用单独评审的 trace 类型。

P0-S3-02 acceptance 配置采用 TDMA，并显式配置 2 s guard interval、60 bit/s rate 与 payload-only airtime（payload bytes × 8，非整纳秒时向上取整）。这些数值属于 acceptance scenario，不是 Platform 全局 MAC 规则或 `ITxPhy` 常量；既有 `ConfiguredTxPhy` 保持不变。Slot 必须能容纳 maximum planned packet airtime 与 guard，communication cycle duration 由 applied slot count 和 slot duration 计算。

M1 继续只是 Platform 对 ns-3 `Simulator` time/event scheduling 的封装层。双时间尺度逻辑不得增加 fixed-step execution、custom EventQueue 或 second simulation clock；位置推进继续由 committed `SimTime` difference 经 StateProjector 计算。

P0-S3-02 至此 CLOSED：applied execution template 保存 update-cycle 的 routing entries、TDMA owner ordering、relative slot offsets 与 cycle duration，但不保存旧绝对 event time 或旧 provenance。每个 reuse cycle 必须从当前 StructureSnapshot 的 `PlanningCycleId`/base version 和当前 committed cycle start 重新构造 plan。Non-update CandidatePlan 的 routing、timing 与 opportunities 均不得进入执行、World 或 causal Trace。

### 2.43 Acceptance detection feature and bearing fusion

Acceptance business input 必须经 M1/ns-3 事件进入现有 network path。ScenarioRuntime 对每个配置 sensor 的首个 applied TxOpportunity 在相同 SimTime 安装 `INPUT_READY` callback；该 callback 从只读 CycleWorkingState projection 生成业务包并只执行 PacketQueue enqueue，之后既有 `TX_START -> TransmissionExecutor` 路径发送。Fusion evaluation 固定在 cycle close timestamp 的既有 `RUNTIME_DECISION` phase，先于 `CYCLE_CLOSE` Commit 且晚于该时刻的 session finalize；禁止外部 fixed-step loop、第二 scheduler、wall-clock timer或新 EventPhase。

`DetectionFeatureReportV1` 固定为 acceptance-specific 15-byte little-endian payload：type/version、sender-local observation sequence、run-relative sample time milliseconds、signed meter-quantized sensor x/y、signed bearing centidegrees、confidence percent 和 flags。它不是硬件/PHY/MAC frame，不添加 header、CRC 或 waveform。报告 position 必须来自其 sample SimTime 的 projected node state；bearing 是 target 与该 sample position 的 deterministic global-frame `atan2` 结果，P0 不使用隐式 RNG noise。

Feature-level fusion 只处理成功送达 fusion center 的 LocalDelivery 中的位置与方位 feature，不读取 raw waveform、ADC 或 ChannelResponse。Observation identity 固定为 `(sender NodeId, observation_sequence)`；duplicate delivery 不重复计数，同一 sender 在不同时间的不同 sequence 是不同 point，因此 `>=5 points` 可以跨 cycle 且不要求五个不同节点。ChannelNoArrival、NotDecoded、Overheard 和 RelayEnqueue 均不得进入 accumulator。

P0 fusion 使用经过 finite/rank/conditioning 检查的 2D bearing-line least-squares intersection；solver 只接收 received feature report 中的 sensor position、bearing、confidence/metadata 以及用于结果方位表达的 fusion-center position，绝不接收 acceptance true-target position。True target 只允许用于 workload bearing generation 与 test-side error comparison。退化几何明确失败，不返回伪 target。Fusion window 在成功结果后完整 seal 并消费，结果保存该 window 的 canonical observation identities；任何 identity 不得进入后续结果，禁止 sliding-window reuse。Period 从该 window 第一条有效 observation 的 sample time 计到 cycle-end `RUNTIME_DECISION` completion time。`FusionResult` 是 run-level application output，不写入 authoritative WorldSnapshot，也不加入 core TraceEvent；core Trace 继续只记录实际 Transmission、ChannelOutcome、Reception 与 CycleCommit。

Acceptance scenario 的 110 dB 通过 `AcceptanceScenarioConfig -> RateBasedTxPhyConfig -> TxEmission` 直接映射到 `source_level_db_re_1upa_at_1m`，因此 P0 simulation 固定解释为 110 dB re 1 uPa @ 1 m。Hardware source-level reference/calibration 仍等待 communication-device parameter confirmation，当前 fixture 不宣称完成硬件标定。

Acceptance `ber_requirement = 1e-4` 与 packet delivery/decoded ratio 是不同概念。当前只有 decoded/not-decoded outcome，因此不得声称已验证 physical BER；BER metric plumbing 和可信物理 BER source 留给后续 M5/metrics 工作。

P0-S3-03 至此 CLOSED：一个 `FusionResult` 对应一个完整 seal 的独立 window，保存 canonical observation identities；后续 window 只能使用新 identity。四周期 Acceptance4Node 形成 cycles 1～2 与 cycles 3～4 两个互不重叠的 24 s 结果；首窗口存在 NoArrival/NotDecoded 时，旧 observation 在结果形成后仍全部 seal，不得泄漏到下一窗口。Least-squares solver 的输入接口不包含 acceptance true-target position。

### 2.44 Acceptance run projection and metric verdict

`AcceptanceRunProjection` 属于 assembly acceptance result side，只在 run 完成后读取 immutable scenario config、实际 applied `RateBasedTxPhy` config、typed Trace sequence、`FusionResultStore` 与 final read-only `WorldSnapshot`。它不得读取可写 `CycleWorkingState`、`ProtocolKnowledgeStore` 或 runtime owner，不得向 runtime、planner、PHY 或后续 cycle 反馈任何控制。相同输入必须生成相同 projection；不使用 wall clock、random 或 unordered output ordering。

Projection 从 Trace 分别统计 Transmission、signal/no-arrival ChannelOutcome、Reception 以及 NotDecoded/Overheard/LocalDelivery/RelayEnqueue；从 final snapshot 读取 run end/version，从 config 读取 run start/node profile，从实际 applied PHY config 读取 effective rate，从 `FusionResultStore` 读取 result count、first/latest summary、minimum observation count 与 maximum completed period。运行 delivery/decode/no-arrival 统计只表达运行质量，不是 BER。

第三方 acceptance metric status 固定支持 `Pass`、`Fail`、`NotEvaluated`，不得压缩为 bool。Acceptance4Node 正式评估 NetworkNodeCount、CommunicationRate、FeatureLevelFusion、BearingPointCount 与 FusionPeriod；bearing point 必须来自每个 `FusionResult.observation_count`，fusion period 必须来自每个结果的 checked timestamps，rate 必须来自实际 applied `RateBasedTxPhy` config。Extended6Node 只生成 projection，不生成 3～4 node 第三方 verdict。

S3-04 阶段 Rx provider 尚无 auditable BER，因此该阶段 BitErrorRate 固定为 `NotEvaluated`；禁止从 packet delivery ratio、decode success ratio、NoArrival ratio 或其他 packet-level count 推导伪 BER。任何正式项为 `NotEvaluated` 时 overall 不得 PASS，固定为 `NotFullyEvaluated`（若另有失败项则为 `Fail`）。Human-readable formatter 只是 deterministic typed-result projection，不使用 ANSI 或 terminal-dependent formatting，也不进入因果路径。

P0-S3-04 至此 CLOSED：run projection、typed metric status、deterministic formatter 与“packet-level ratio 不得冒充 BER”的边界已经 OFF/ON 测试冻结。

### 2.45 Receiver physical quality evidence and acceptance BER

`RxQualityEvidence` 是 standalone contracts value，保存 finite SNR dB、finite Eb/N0 dB、范围 `[0,1]` 的 BER，以及必须显式区分的 `Modeled`、`Measured`、`External` source。`RxDecodeResult` 以 optional evidence 做 additive extension，旧 provider 可继续只返回 identity 与 `DecodeOutcome`。Quality evidence 与 decode causality 必须分离：acceptance requirement `1e-4` 不是 decoder threshold，BER observer 不得据此改写 inner receiver 的 Decoded/NotDecoded outcome。

P0 acceptance scalar provider 是现有 `IRxPhy` 的独立 decorator。它使用 `ComputeP0ScalarReceivedLevelDbRe1upa` 的 `source level - aggregate TL`，不得再次应用 PropagationPath gain；使用已覆盖 desired interval 和 occupied band 的 `NoiseObservation` equivalent noise power，不得再次按 bandwidth 积分。P0 中 dB re 1 uPa pressure level 与对应 pressure-squared level 的 dB 数值等价，因此 `SNR_dB = received_level_db - noise_power_db`。Occupied bandwidth 来自 `TxEmission`，bit rate 是显式配置，`Eb/N0_dB = SNR_dB + 10 log10(bandwidth/bit_rate)`，modeled BER 固定为 coherent BPSK-AWGN theoretical `0.5 erfc(sqrt(10^(Eb/N0_dB/10)))`。25 kHz carrier 不重复塞入公式；Acceptance 的 15-byte/120-bit feature、2 s Tx duration 与 60 bit/s 必须有 consistency test。

`ReceptionTrace` 只增加 Trace 自有 optional quality summary，不嵌入完整 decode request、noise/window 或 PHY object。`ChannelNoArrival` 没有 ReceivedSignal，因而没有 BER evidence，禁止填 0、1、infinity 或其他 sentinel；Signal 到达但 NotDecoded 时可以具有正常 quality evidence。P0 scalar model 只在 `ReceiverWindow.overlapping_signals()` 为空时提供 Modeled evidence，因为当前计算只覆盖 desired signal 与 environmental noise；interference-aware SINR/BER 留待后续。Inner Decode 成功后，overlap 或额外 quality 数值计算失败只能产生相同 DecodeOutcome 加 empty quality，不得升级为 Decode Error。非法 bit-rate 等 provider 配置仍必须在 creation/config 阶段拒绝。Quality 与 Trace emission 都是 non-causal best-effort：有无 evidence 及 sink success/failure都不得改变 World、queue、delivery、decode、fusion 或 event outcome。

Acceptance BER 只评价每条 unicast Transmission 的正式 current-hop target reception，overheard receiver 不计入。完整覆盖要求每个 target attempt 都存在 target signal reception 与 quality；完整时报告 evaluated count、maximum/mean BER 和 source counts，以 maximum target-link BER `<= 1e-4` 判定 Pass，任一超限判定 Fail。NoArrival、Reception 缺失或 provider 无 evidence 均使整次 BER `NotEvaluated`，reason 固定为 `Incomplete auditable BER coverage.`，禁止对剩余样本取平均后声明 Pass。

当前 BPSK-AWGN 结果只可称为 Modeled BER，不是 Measured hardware BER；110 dB source-level hardware reference/calibration 仍为 TBD。未来 full waveform Rx 或实际 BIN/hardware provider 可改为生产 Measured/External evidence，但不得改变 acceptance report contract。Packet delivery、decode success 与 NoArrival ratio 始终不得推导 BER。

P0-S3-05 至此 CLOSED：quality/decode 非因果分离、overlap evidence gate、target-link 完整覆盖与 modeled BER acceptance closure 已通过 OFF/ON 测试冻结。P0-S3 Trace/Acceptance Scenario 整体 CLOSED。

### 2.46 Application run domain and frontend-facing boundary

Application domain 位于 `ScenarioRuntime` 与 assembly composition 之上。Frontend 或未来 HTTP adapter 只能调用 application service 和消费 application DTO，不得直接持有 `ScenarioRuntime`、`CycleWorkingState`、`ProtocolKnowledgeStore`、`ProtocolCyclePlan`、TransmissionRecordStore 或 Environment package path。Environment 继续由既有 validated `EnvironmentAssetRepository` 管理；Scenario 只捕获稳定 asset identity/format version。

`ScenarioId`、`ExperimentId`、`RunId` 是互不兼容的 caller-provided validated string strong types，不复用通信/规划 ID，也不接受 filesystem path grammar。ScenarioDefinition 与 ExperimentDefinition 是带显式非零 version 的 immutable value；Run 创建时捕获 exact Experiment、Scenario、Environment identity/version。RunId 只标识应用资源，不得参与 simulation communication ID allocation、event ordering、provider calculation、业务 packet generation 或其他因果输入；相同 definitions、asset、seed/config 必须得到相同 simulation result。

P0 Run lifecycle 固定为 `Created -> Running -> Completed` 或 `Created -> Running -> Failed`。Completed/Failed 是 terminal，不允许同一 RunId retry/reset/re-execute。同步 `RunService` 提供 CreateRun、ExecuteRun、GetRun、GetResult；成功时 terminal record 与 RunResult 一起发布，失败时保留具体 ErrorCode 与 owned summary，不把 Scenario/Experiment/Environment missing、duplicate ID、invalid lifecycle 和 simulation/provider failure 合并成通用 “Run failed”。

RunResult 只包含 application-owned run projection、optional acceptance summary、fusion summaries 与 node summaries，不泄漏 runtime/environment internal object。Acceptance preset adapter 保留既有 Acceptance4Node/Extended6Node config，并通过已注册 Environment asset 和现有 M1–M5/M8/feature workload 路径执行。HTTP/FastAPI、JSON、SSE、authentication、database persistence、cancel/retry 和 run-event retention 均留后续阶段；core TraceEvent 本阶段不修改。

P0-S4-01 至此 CLOSED：Run 创建时捕获 exact immutable Experiment、Scenario 与 Environment identity/version；后续注册的新 version 不改变既有 Run 的执行输入。只有 Completed Run 可以发布正式 RunResult；Failed Run 保留具体 failure summary，但不得留下 formal result。Run repository 的状态迁移能力只对 RunService 开放，普通 caller 不能绕过 terminal lifecycle。

### 2.47 Run-local event journal and replay cursor

Application 将既有 typed `TraceEvent` 通过当前 Run 专属的 `RunEventSink` 包装为 `RunEventRecord`。Core `TraceEvent` 不增加 RunId、SSE cursor 或 application sequence；`RunEventSequence` 只属于单个 Run，从 1 开始并严格按 `ITraceSink::Emit` 调用顺序递增。不同 Run 各自从 1 开始，相同 simulation timestamp 的事件不得按时间重新排序。0 只表示 before-first-event cursor，不是已写入 record 的 sequence。

`IRunEventJournal` 对普通调用者只公开 `ReadAfter` 与 `GetLatestSequence`。Append capability 是接口私有 mutation，仅由 RunService 构造的 RunEventSink 使用，因此 future API reader 不能伪造 record。P0 in-memory journal 在 append 时一次性固定 sequence；重复读取不生成新 identity。`ReadAfter(cursor, limit)` 只返回 `sequence > cursor` 的 ascending records，limit 必须位于显式 bounded range；cursor 等于 latest 返回空，cursor 大于 latest 明确返回 OutOfRange，禁止 silent reset。

RunEventSequence 只属于成功写入 journal 的正式 record。Append 失败不消耗 sequence；success/fail/success 必须形成 1、2，而不是 1、3。Event-stream completeness 一旦因任意 append failure 变为 false 就保持 sticky，后续成功不得恢复。Sequence successor 使用 checked allocation；latest 为 `UINT64_MAX` 时下一 append 明确 Overflow，不得 wrap 到 0，且该 observation failure 仍不改变 simulation causality。

Event recording 延续 S3-01 的 non-causal best-effort 原则。Journal append failure 不得改变 simulation state、event order、RunResult 或 Completed/Failed lifecycle；RunRecord 独立保存 event-stream completeness diagnostic。Simulation failure 前已经成功 append 的 records 不得删除，Completed 与 Failed Run 都可继续 replay。Events、formal RunResult 与 future snapshots 是三个独立读取资源：Result 不依赖 replay 重建，journal 不保存 WorldSnapshot，journal 不完整也不使正式 simulation result 失效。

Journal 当前只保存 simulation Trace observations，不保存或推导 Run lifecycle transition。`RunRecord` 是 Created、Running、Completed、Failed 的唯一权威；event presence、absence 或最后一条 Trace kind 都不得替代 RunRecord。存在的 Run 在任意 lifecycle 均允许只读 ReadEvents：Created 通常为空，Running 返回当前成功写入的 prefix，Completed/Failed 支持 replay。

P0-S4-02 不实现 HTTP、SSE socket、serialization、Last-Event-ID、database retention、authentication、cancel/retry 或 concurrent scheduler。Future transport 只能序列化既有 cursor/read DTO，不得通过 event read 创建 Run、推进 simulation time 或取得 mutable journal handle。

P0-S4-02 至此 CLOSED：run-local successful-record sequence、bounded cursor replay、private append ownership、sticky completeness、overflow safety、terminal replay 与 Result/snapshot independence 已通过 OFF/ON 测试冻结。

### 2.48 Out-of-process simulation worker boundary

P0 后端目标架构固定为 control plane 与 execution plane 分离：未来 FastAPI 只负责 API/resource validation、authorization 与 worker process management，不在其主进程加载或运行 ns-3；每个 C++ `platform_sim_worker` 进程只执行一个 Run，并继续通过 `RunService -> production executor -> ScenarioRuntime -> ns-3` 正式路径运行。Worker 不复制 ScenarioRuntime，不建立第二套 scheduler、clock 或 simulation lifecycle。

每个 worker process 独占自己的 ns-3 Simulator lifecycle。Run 完成或失败后进程退出；后续 Run 由新进程从独立 time zero 与空 Simulator state 开始。该边界隔离 simulation/provider failure 及未来 native crash，不等于本阶段已经实现 cancel、retry、crash recovery、parallel run 或 multi-run scheduler。

Worker backend bridge 使用 codec-independent typed variant，明确区分 WorkerStarted、保持原 RunId/RunEventSequence/Trace payload order 的 WorkerRunEvent、携带 authoritative Completed RunRecord 与 formal RunResult 的 WorkerCompleted，以及携带具体 Error/optional terminal record 的 WorkerFailed。RunService 继续是 event sequence 生成权威；worker、future FastAPI 与 SSE adapter 都不得重编号。Run lifecycle、events 与 result 是独立输出，RunRecord 仍是 lifecycle 唯一权威。

Process exit 0 只表示 simulation/worker 正常 Completed；process/protocol/simulation failure 使用非零。Acceptance metric verdict Fail 是成功生成的业务结果，不是 process failure，因此仍 exit 0。RunId 继续只作为 application/bridge identity，不进入 simulation causality；相同 definition、asset、seed/config 在 direct RunService 与 worker boundary 必须产生相同 simulation summaries 与 Trace payload order。

仓库当前没有获批 JSON library 或既有 IPC codec。跨进程序列化只能位于 worker/backend adapter，禁止把 JSON 引入 contracts、runtime、planning 或 PHY，也禁止手写通用 JSON parser。nlohmann-json、Boost.JSON 或 binary framing 等方案必须后续评审 schema/version、malformed input、framing、size limit 与依赖交付后再选择；P0-S4-03 只实现 typed in-process protocol 与 codec-independent process isolation smoke，不冻结 HTTP/SSE transport。

P0-S4-03 至此 CLOSED：one-Run-per-process、独立 ns-3 time-zero lifecycle、typed bridge、terminal cardinality、Acceptance verdict/process-exit 分离、direct/worker equivalence，以及 non-causal bridge failure gate 已通过 OFF/ON 测试冻结。

### 2.49 Worker wire protocol 与 backend process controller

P0-S4-04 固定 stdin command/stdout message 的 newline-delimited JSON schema v1；stderr 只允许 human diagnostics。正式模式每行恰好一个 JSON object，每条 command/message 都携带 `schema_version=1` 和显式 `type`。Command、message、RunEvent 与 terminal result 继续映射既有 typed model；JSON 只属于 `worker/codec` 与 `worker/adapter`，不得进入 contracts、application domain、kernel、runtime、structure、planning、PHY 或 environment。

所有 strong numeric identity、ResourceVersion、RunEventSequence、计数值和 simulation nanoseconds 在 wire 上统一使用 canonical decimal string，避免 JavaScript Number 精度成为权威表示；schema version 与布尔值仍使用其自然 JSON scalar。仿真时间只以 integer nanoseconds 表达，不使用 floating seconds。RunEventSequence 保持 RunService 生成值且不重编号，并冻结为 future SSE event id 的来源，但本阶段不实现 SSE。

JSON 实现固定为仓库内离线 vendored `nlohmann/json v3.12.0`，只通过局部 `Platform::third_party_nlohmann_json` target 暴露给 worker codec。版本、官方 release URL、archive SHA-256 和 MIT license provenance 保存在 `Platform/third_party`；configure/build/test 不允许联网获取依赖或搜索系统安装副本。

Wire framing 的 input line 上限为 1 MiB，output message 上限为 4 MiB。Invalid JSON、missing/unknown schema、unknown type、missing/unknown field、非法 typed ID、非 canonical/out-of-range decimal 和 oversized frame 必须显式失败。Worker terminal 固定为 `Started -> RunEvent* -> exactly one Completed|Failed`；Completed 必须携带 authoritative RunRecord、formal RunResult 与 event completeness，不能从最后一个 event 推导。

Backend `WorkerProcessController` 是未来 FastAPI 可复用但不依赖 HTTP 的 OS process owner。其 NotStarted/Starting/Running/Completed/Failed 与 RunLifecycle 是不同状态机；它负责 spawn、command write、bounded message read、event callback、wait 和 terminal/exit consistency。Completed+exit0 与 Failed+nonzero 才是有效组合；terminal 前 EOF、signal crash、malformed stdout、pipe failure、Completed+nonzero、Failed+exit0 均为 process/protocol failure。FastAPI control plane 仍不得加载 ns-3；C++ worker execution plane 仍是唯一 simulation owner。

### 2.50 FastAPI Run control plane 与 Python worker gateway

P0-S4-05 FastAPI backend 是纯 control/API plane。它通过 Python
`asyncio.create_subprocess_exec` 直接、非 shell 地启动一个
`platform_sim_worker`，不调用 C++ WorkerProcessController，也不加载 ns-3。
Python gateway 消费与 C++ 完全相同的 schema v1 NDJSON typed model；所有 strong
numeric identity/version/count/sequence/time 在 HTTP 与 wire 上继续使用 canonical
decimal string，simulation nanoseconds 不转为 floating seconds。

POST `/runs` 只暴露 Acceptance4Node preset 的显式执行参数，并由 backend 生成
opaque RunId；RunId 仍不进入仿真因果输入。P0 明确采用 single-active Run，第二个
并发创建请求返回 BackendBusy。Run catalog、events 与 results 全部 in-memory；backend
restart 会丢失这些 control-plane 资源，不引入 database、retry、cancel 或 generic
scheduler。

RunRecord lifecycle 仍是 Created/Running/Completed/Failed 的唯一权威。Event 数量和
最后一条 Trace 不得推导 terminal state。WorkerCompleted+exit0 才发布 formal result；
WorkerFailed+nonzero、premature EOF、malformed stdout、signal crash 与 terminal/exit
mismatch 都形成明确失败。Acceptance verdict Fail 仍是成功 Completed Run。Worker
stderr 只进入 backend diagnostics，不作为 wire input，也不原样暴露为 HTTP error。

SSE `/runs/{run_id}/events` 使用原始 RunEventSequence decimal string 作为 `id`，不重新
编号。`Last-Event-ID=0` 表示 before-first；cursor 必须 canonical、非负且不超过当前
latest。订阅在同一 catalog condition 下读取 backlog 并等待 live append，因此
replay/live 边界不丢失、不重复。Completed/Failed Run replay 完已知事件后关闭；Failed
lifecycle 仍通过 GET Run 查询。

Python baseline 固定为 CPython 3.12.3，FastAPI/Pydantic/Uvicorn 和测试依赖采用仓库内
original wheelhouse、全量 exact versions 与 SHA-256 lock。configure/build/test 只用
`pip --no-index --require-hashes`，不访问 PyPI，也不修改系统 Python 环境。

HTTP request cancellation、SSE disconnect 与 browser reconnect 是纯 observation-plane
行为，不得终止 worker、改变 Run lifecycle 或影响 simulation result；P0 不提供用户
Cancel API。FastAPI application shutdown 则必须通过 lifespan 关闭 catalog，终止并 reap
Python gateway 拥有的所有 active child process，不得遗留 orphan worker。

WorkerCompleted 到达时仍是 provisional terminal observation。只有 stdout framing 完整结束、
child 已被 reap、exit status 为 0 且 terminal/protocol consistency 全部通过后，catalog 才能
原子发布 Completed lifecycle 和 formal Result；检查完成前 GET Result 必须保持 RunNotReady。
Acceptance verdict Fail 继续是该正常 Completed 路径中的业务结果。

P0-S4-05 至此 CLOSED：client-disconnect non-causality、SSE reconnect/replay、backend
shutdown child ownership、terminal publication atomicity、single-active Run、volatile catalog
与 control/execution plane 分离已通过 OFF/ON、Python 和真实 worker integration 测试冻结。

### 2.51 Environment / Scenario / Experiment resource catalog

P0-S4-06 建立 `Environment -> Scenario -> Experiment -> Run -> Result` 的只读资源链。
Environment 存在性和 metadata 继续由 C++ `EnvironmentAssetRepository` 权威验证；Scenario
与 Experiment 继续由 C++ application domain 及既有 Acceptance preset 权威构造。位于
worker adapter 边界的窄 C++ resource-catalog adapter 只投影 backend schema v1 document，
Python 只负责严格 HTTP DTO、immutable index 与 version resolution，不解析资产 package，
不重新定义仿真 domain。

Environment API 只暴露 logical EnvironmentAssetId、显式 format/version、provenance summary、
coordinate/axes summary、cell/no-arrival count、checksum 与 validation state；禁止泄漏 repository
root、absolute/internal package path 或 filename identity。Scenario/Experiment 发布版本不可覆盖，
P0 只提供预注册 read catalog，不提供 PUT/create/delete。所有 list 固定按 ID、version 排序，
不得依赖 filesystem enumeration 或 dict insertion order。

正式 POST `/runs` 只接受 ExperimentId/version。Backend 必须先解析
`Experiment -> Scenario -> Environment` 全链，再构造既有 StartRunCommand。Run 创建时捕获
三层 identity/version；后续 catalog 新版本不得改变既有 Run，Run 与 Result read model 均返回
captured references。schema v1 当前使用一个 definition_version，故 P0 preset 要求 Scenario 与
Experiment version 对齐；不匹配必须 InvalidReference，不得静默选择其他版本。

Resource API 延续 canonical decimal string web policy，并区分 EnvironmentNotFound、
ScenarioNotFound、ScenarioVersionNotFound、ExperimentNotFound、ExperimentVersionNotFound 与
InvalidReference。P0-S4-06 不引入 database、authentication、generic persistence、Bellhop
subprocess 或 arbitrary scenario editor。

Backend startup 必须将 C++ adapter 的完整 document 作为一个不可分割 snapshot 校验：metadata、
identity/version 唯一性以及 Scenario→Environment、Experiment→Scenario 全部引用通过后才能原子发布
deterministic index；任一坏项导致 startup fail，禁止 partial publish。P0 snapshot 在 backend process
lifetime 内不可变，不提供 hot reload；新内容需未来显式 refresh 设计或 backend restart。

WorkerCompleted 必须与 Run 创建时捕获的 RunId、ExperimentId/version、ScenarioId/version 和
Environment identity/version 完全一致。任何 mismatch 即 WorkerProtocolFailure，Run 进入 Failed，
即使 child exit 0 且 acceptance Pass 也不得发布 formal Result。

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
