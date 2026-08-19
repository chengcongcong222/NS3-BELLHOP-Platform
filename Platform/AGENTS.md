# Platform 工程约束

本文件适用于 `Platform/` 及其全部子目录。

## 1. 开始任务前

- 必须阅读本文件和任务涉及的 ADR。
- Runtime、contracts、时间、事件、通信对象相关任务必须阅读 `docs/adr/ADR-0001-runtime-foundation-decisions.md`。
- 需要架构上下文时查阅 `review.md` 和 P0.4 设计基线。
- ADR 与旧文档冲突时，采用状态为 Accepted/Frozen 的 ADR。
- 如果任务要求违反 ADR，必须停止实现并报告；禁止自行改写架构。

## 2. 仓库与迁移边界

- 仓库根目录旧系统只允许作为资产来源、算法参考和结果对照。
- `Platform/` 是新系统唯一实现目录。
- Platform Runtime 是项目首次真正基于 ns-3 离散事件内核的实现。
- 禁止迁入旧 fixed-step/fixed-tick 主循环。
- 禁止迁入旧 `SimulationManager`。
- 禁止迁入全局 MAC busy state。
- 禁止迁入 receiver-loop 驱动的发送语义。
- 禁止建立第二套全局事件队列或 scheduler。
- 禁止从 Platform include 旧运行对象或旧模块 internal 类型。

迁移旧算法必须遵循：

```text
legacy algorithm
  -> 提取纯算法核心
  -> 适配 Platform contracts
  -> 独立单元测试
```

- 允许通过 Adapter/Converter 读取旧资产。
- 禁止为了兼容旧代码而让 Platform 反向依赖 fixed-tick 状态模型。
- 禁止顺带重构旧系统；只修改任务明确授权的目录。

## 3. 架构模块与里程碑

M1-M8 只表示架构模块：

| 编号 | 目录 | 职责 |
|---|---|---|
| M1 | `kernel/` | ns-3 时间与事件边界 |
| M2 | `runtime/` | 权威状态与通信执行 |
| M3 | `structure/` | 角色、连通图、逻辑拓扑 |
| M4 | `planning/` | 路由、MAC、周期时序规划 |
| M5 | `phy/` | Tx PHY、Channel、Rx PHY |
| M6 | `adapters/` | 外部数据与时间适配 |
| M7 | `assembly/` | 场景装配、注册和兼容性校验 |
| M8 | `observability/` | Trace、Metrics、Checkpoint |

- 开发里程碑必须使用 `P0-S0`、`P0-S1`、`P0-S2` 等名称。
- 禁止用 M1/M2 等架构模块编号表示开发阶段。

### 3.1 `kernel/`

- 是唯一允许直接操作 ns-3 Scheduler 的模块。
- 必须提供 Platform 与 ns-3 的时间、事件转换边界。
- 禁止保存权威节点状态。
- 禁止计算路由、MAC 或物理传播。

### 3.2 `runtime/`

- 必须持有权威状态。
- 必须持有可写 `CycleWorkingState`。
- 必须持有可写 `ProtocolKnowledgeStore`。
- 必须负责发送、接收、状态聚合和 Commit。
- 禁止建立自己的全局 EventQueue。

### 3.3 `structure/`

- 必须负责 RoleTable、ConnectivityGraph、LogicalTopology。
- 只能读取规划 View。
- 禁止获取可写 World、Node 或 runtime owner。

### 3.4 `planning/`

- 必须负责 RoutingPlan、MacPlan、CycleTiming。
- 禁止直接调用 ns-3 Scheduler。
- 禁止直接调用 PHY。

### 3.5 `phy/`

- 必须遵守 `ITxPhy -> IChannelFieldProvider -> IRxPhy`。
- 禁止直接访问 WorldStateStore。
- Bellhop 只允许作为 `IChannelFieldProvider` 实现，不是完整 PHY。
- Abstract PHY 和 Waveform PHY 必须使用相同外围合同。

### 3.6 `environment/`

- 负责环境资产和 Bellhop 离线资产。
- 在线 Runtime 只允许查询预计算资产。

### 3.7 `adapters/`

- 负责 BIN、UDP、串口、文件和其他外部数据适配。
- 禁止推进 Platform 仿真时间。
- 外部输入必须映射为 SimTime 后经 M1 注入。

### 3.8 `assembly/`

- 负责场景装配、组件注册和启动前兼容性检查。
- `ComponentRegistry` 必须属于单个 `ScenarioRuntime`。
- 禁止全局 singleton。

### 3.9 `observability/`

- 只能观测，不得参与控制决策。
- Trace、Metrics、Checkpoint 失败不得改变仿真因果结果。

## 4. contracts 规则

`contracts/` 只允许包含：

1. 跨模块交换的数据；
2. 跨模块只读 View；
3. 必要且稳定的接口。

禁止在 contracts 中暴露：

- 可写 `CycleWorkingState` owner；
- 可写 `ProtocolKnowledgeStore` owner；
- runtime internal 容器；
- ns-3 Scheduler 类型或 `ns3::Time`；
- Bellhop 具体文件格式；
- HIL transport 实现；
- 任一模块的 `internal/` 类型。

- `working_state.hpp` 主要定义 `WorkingStateView` 等只读合同。
- `knowledge.hpp` 定义 `KnownNodeState`、`KnownLinkState`、`KnowledgeView`、`KnowledgeDelta` 等跨模块合同。
- 真正可写 owner 必须位于 `runtime/internal/`。
- contracts 的不兼容修改不得静默进行。
- 修改 contracts 必须说明 API compatibility impact。
- 触及冻结规则时，必须先新增或更新 ADR。

## 5. 时间规则

- ns-3 是在线仿真唯一时间权威。
- 当前基线暂定 ns-3.47。
- 第一阶段只允许引入离散事件内核所需的最小 ns-3 能力。
- 禁止因接入 ns-3 而引入完整网络协议栈。
- `SimTime` 和 `SimDuration` 是 Platform 强类型时间值。
- 底层必须使用 `int64` nanoseconds。
- `SimTime` 不得提供 `Now()`、`Advance()` 或 `Tick()`。
- contracts 不得暴露 `ns3::Time`。
- `ns3::Time <-> SimTime` 转换只允许存在于 `kernel/`。
- 唯一 `Now()` 来源必须是 M1/ns-3。

禁止：

- 使用 double seconds 作为事件主时间类型；
- fixed timestep；
- 自定义全局 simulation clock；
- 第二套 scheduler。

## 6. 同一时刻事件规则

同一 SimTime 必须按以下 `EventPhase` 顺序执行：

1. `SESSION_FINALIZE`：TxEnd、RxFinalize；
2. `SIGNAL_ARRIVAL`：RxStart；
3. `INPUT_READY`：Application/External input ready；
4. `RUNTIME_DECISION`：Timer、BackoffExpire、CarrierSense、RuntimeHook；
5. `TX_START`；
6. `CYCLE_CLOSE`：Aggregate、Commit。

- 同一 phase 内必须继续按稳定 ID 排序。
- 当前 timestamp 动态创建事件时，只允许创建当前 phase 之后的 phase。
- 禁止回插已经执行过的 phase。
- 禁止依赖 unordered container 遍历顺序。
- 禁止依赖 pointer 地址。
- 禁止依赖线程完成顺序。
- 禁止依赖未定义的 callback 注册顺序。

## 7. PlanningCycle 与状态

必须区分：

- `WorldSnapshot`：上一完整周期 Commit 后的正式权威状态；
- `CycleWorkingState`：当前周期的可写运行状态，owner 只属于 runtime；
- `ProtocolKnowledgeState`：协议节点实际获得的信息，不等于 WorldSnapshot；
- `AlgorithmMemory`：算法私有跨周期状态，P0 只预留。

正常周期必须遵循：

```text
WorldSnapshot S(k)
  -> Planner
  -> ProtocolCyclePlan
  -> ns-3 events
  -> CycleWorkingState
  -> Aggregate
  -> Commit once
  -> WorldSnapshot S(k+1)
```

- 一个正常 PlanningCycle 只能正式 Commit 一次。
- P0 必须采用 closed-cycle。
- `CYCLE_CLOSE` 时 active `TransmissionSession` 必须为 0。
- `CYCLE_CLOSE` 时 active `ReceptionSession` 必须为 0。
- 无法在 CycleClose 前完成的发送动作必须延迟到下一周期。
- P0 禁止跨周期保留 active Tx/Rx session。
- Cycle0 必须在 `t=0` 从 S0 走正常规划路径。
- 禁止为 Cycle0 增加 bootstrap tick 或特殊 fixed-step。

周期中新 Packet：

- PacketReady 只进入 Queue，不触发 M3/M4 全局重新规划。
- 若后续已有可用 TxOpportunity，PacketSelector 可在 TxStart 选择该 Packet。
- 若本周期没有可用 opportunity，Packet 必须等待下一周期。
- 只有未来显式 `IMacRuntimePolicy` 才允许生成周期内协议动态动作。

## 8. 节点规则

- 必须区分 `NodeCapability` 与 `ProtocolRole`。
- Capability 表示节点能做什么；Role 表示节点本周期扮演什么角色。
- `NodeId{0}` 是合法节点编号。
- 禁止使用 `NodeId{0}` 表示 invalid。
- “无节点”必须使用 `optional<NodeId>` 或明确状态类型。
- 禁止建立 Giant Node 类。
- 禁止把 Packet、Routing、MAC、Propagation、PHY、Transmission、Reception 和时间推进全部塞入 Node。
- Node 主要表达静态能力、权威状态身份和必要引用。

## 9. 通信对象与广播不变量

必须严格区分：

- `DigitalPacket`：信息；
- `TransmissionSession`：一次真实物理发送；
- `ReceptionSession`：某次 Transmission 在一个 receiver 上形成的接收过程。

- `PacketId`、`TransmissionId`、`ReceptionId` 必须是不同强类型。
- 同一 Packet 重传时 PacketId 不变。
- 每次物理发送必须产生新的 TransmissionId。
- accepted TxOpportunity 必须创建恰好一个 TransmissionSession。
- 一个 TransmissionSession 必须生成一个 TxEmission。
- 一个 TransmissionSession 允许生成 0..N 个 ReceptionSession。
- `StartTransmission()` 不得以单个 `receiver_id` 作为核心输入。
- receiver fan-out 必须发生在 Transmission 创建之后。
- RxStart/RxFinalize 禁止反向调用 StartTransmission。
- TxEnd 必须按 TransmissionId 幂等执行一次。

P0-S1 强制广播回归：

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

receiver 数量导致 TxStart、TxEnd、QueueConsume、TxEnergySettlement 或 Transmission trace 增长的实现，禁止合并。

## 10. Trace 规则

- `trace.hpp` 只允许依赖 time、identity、error/status 和 Trace 自有只读摘要 payload。
- 禁止在 TraceEvent 中嵌入完整 WorldSnapshot。
- 禁止在 TraceEvent 中嵌入完整 ProtocolCyclePlan。
- 禁止在 TraceEvent 中嵌入 runtime owner 或可写业务对象。
- 一个 TransmissionSession 必须对应一条 Transmission trace。
- 每个 ReceptionSession 必须分别对应一条 Reception trace。
- 广播 receiver 数量不得改变 Transmission trace 数量。
- Trace sink 故障不得影响仿真因果结果。
- 缓冲、背压和落盘策略仍为 TBD，禁止自行写死。

## 11. PHY 与 Channel

- 外围合同固定为 `ITxPhy -> IChannelFieldProvider -> IRxPhy`。
- Bellhop 是 Channel Provider，不是完整 PHY。
- Abstract PHY 和 Waveform PHY 必须使用相同外围合同。
- 非 PHY 任务不得自行决定 PSK、同步、解调、DAC/ADC、源级、门限、能耗或帧格式。
- 未冻结物理参数必须保持 TBD 或显式 test fixture/mock value。

## 12. Environment 与 Bellhop

必须采用离线资产链：

```text
WOA/Argo/GEBCO
  -> SSP/地形
  -> Bellhop 离线计算
  -> EnvironmentSnapshot/AcousticFieldAtlas
  -> Runtime 在线查询
```

正常 Packet/Event 热路径禁止：

- 下载 WOA/Argo；
- 在线运行 Bellhop；
- 通过 HTTP 查询实时传播模型。

P0 Bellhop 查询至少区分：

- `ASSET_NOT_FOUND`；
- `OUT_OF_COVERAGE`；
- `FREQUENCY_PROFILE_NOT_FOUND`；
- `NO_PHYSICAL_ARRIVAL`。

- 只有 `NO_PHYSICAL_ARRIVAL` 是合法物理结果。
- 禁止越界自动 clamp。
- 禁止查询失败静默 extrapolation。
- 禁止无路径时合成直达路径。
- 禁止无频率 profile 时静默选择最近频率。

## 13. BIN 与外部数据

当前只确定 sample bit width 为 16。

以下全部保持 UNKNOWN/TBD：

- signedness；
- endian；
- sample rate；
- real/IQ；
- carrier frequency；
- frame format；
- preamble；
- CRC。

- 禁止为了使代码先运行而设置隐式默认值。
- 必须使用 Descriptor + Adapter 表达未知状态。
- 外部文件或设备不得推进 Platform 仿真时间。

## 14. P0-S0/P0-S1 范围

- P0-S0 是 Contracts Freeze。
- P0-S1 是 Core Closed Loop。
- P0-S1 使用 Mock PHY、Mock Channel 和 fixture planner 验证 Runtime。

P0-S1 禁止提前实现：

- 完整 ALOHA；
- 完整 TDMA planner；
- FDMA；
- CSMA；
- Bellhop 资产迁移；
- 真实 Waveform PHY；
- AS-MAC；
- AB-MAC；
- UI/API 兼容；
- strict real-time HIL。

## 15. TBD 管理

当前 TBD 包括：

- PHY/Waveform 内部参数；
- BIN/Frame 格式；
- Noise 单位、带宽积分和 preset；
- 严格实时 HIL；
- AS-MAC/AB-MAC 优先级和 AlgorithmMemory；
- Bellhop 网格采样密度和最终存储；
- M3 质量阈值和滞回参数；
- 各协议 CycleTiming/guard time 默认值；
- Kernel 最终只提供 ScheduleAt，还是同时提供 ScheduleAfter；
- Trace sink 缓冲/背压策略；
- ns-3.47 获取方式、编译器和 CI 组合。

- 实现人员不得把 TBD 固化为不可替换默认值。
- 临时测试值必须标记为 test fixture 或 mock value。
- 临时测试值不得写入正式公共合同。

## 16. 开发与测试

每个编码任务必须：

1. 阅读本文件和相关 ADR；
2. 只修改任务明确授权的目录；
3. 不顺带重构 legacy；
4. 不扩展任务范围；
5. 修改后运行相关构建和测试；
6. 优先测试架构不变量，而不只测试“代码能运行”；
7. 汇报修改文件、关键实现、测试结果和未解决问题。

- contracts 修改必须额外说明 API compatibility impact。
- 发现需要违反 ADR 时必须立即停止并报告。
- 禁止自行修改冻结架构规则。

## 17. 编码原则

必须优先：

- 简单；
- 显式；
- 可测试；
- 确定性；
- owner 明确；
- 生命周期明确。

必须避免：

- 过度抽象；
- 为未来假设设计复杂框架；
- 全局 mutable state；
- 隐式 singleton；
- 魔法默认值；
- 使用 callback 次数表示物理时间推进；
- 使用容器遍历顺序决定仿真结果。

P0 的目标是建立正确、稳定、可扩展的底层运行语义，不是提前实现全部功能。
