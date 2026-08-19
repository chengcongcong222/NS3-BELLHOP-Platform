# NS3-BELLHOP P0.4 旧系统实施评审与 P0-S0/P0-S1 规划

> 评审日期：2026-08-17
> 正式基线：[NS3-BELLHOP_P0.4_软件架构与开发实施设计基线.docx](docs/NS3-BELLHOP_P0.4_软件架构与开发实施设计基线.docx)（P0.4，2026-08-16）
> 新系统目录：`Platform/`（独立新架构实现目录，当前已完成 P0-S0-03B 基础状态运行时）
> 本轮范围：只读分析旧系统并形成实施计划；除本评审文件外，不修改生产代码。

## 0. 评审结论

P0.4 已经足以启动 P0-S0、P0-S1 开发。旧系统中环境资产、Bellhop 离线流水线、基础传播/噪声/定位算法、前后端产品能力具有较高迁移价值；旧运行内核、固定 tick MAC 调度和集中式通信执行不应迁入 `Platform/`。

代码审计得到的核心结论如下：

1. 旧系统没有真正接入 ns-3。源码与构建文件中未发现 `ns3::Simulator`、ns-3 模块 include 或链接配置；`ns3_factory` 目前只是项目命名空间。
2. 真实运行内核位于 `src/main.cpp`，采用 `for (step...)` 固定步长循环，顺序处理移动、外部调度、路由、MAC、传播、噪声、接收和结果。
3. `ConsoleSimulationManager` 不是运行内核，只负责初始化检查和摘要输出；不能在它上面扩展新 Runtime。
4. 旧代码没有字面上的 `for(receiver) sender.Send()`，但它在 receiver 循环内重复进行传播、量测、噪声、投递判定和 `tx` 事件记录。由于没有 `TransmissionSession`，发送端一次性副作用没有结构性保护，迁移风险高。
5. `Platform/` 应严格按 P0.4 的 M1-M8 边界从零建设，不复制旧 `main.cpp`、旧 `SimulationManager`、旧 MAC scheduler 或任何自建全局事件队列。
6. P0-S0 应冻结 contracts；P0-S1 应以 MockPHY 完成“四节点、两周期、汇聚角色轮换、星型 TDMA、每周期一次 Commit”的最小闭环。

审计覆盖自研源码、配置、schema、测试脚本、场景、前后端和项目文档。`node_modules`、构建目录、缓存、运行结果及第三方源码只盘点边界，不作为逐文件迁移对象。

---

## A. 旧系统目录结构与模块职责

旧系统仍位于仓库根目录；`Platform/` 是新系统的独立实现区。

| 目录/文件 | 旧系统职责 | 主要问题或价值 |
|---|---|---|
| `include/ns3_factory/config`、`src/config` | 场景 JSON 解析和配置结构 | 解析工具可用；运行配置仍以 `time_step_ms` 为核心 |
| `include/ns3_factory/core`、`src/core` | `SimulationManager`、控制台实现、FactoryRegistry | manager 并未驱动仿真；FactoryRegistry 未实际形成运行装配体系 |
| `src/main.cpp` | 场景装配、运行状态、固定步循环、业务、MAC、路由、传播、结果输出 | 实质上的巨型运行编排器，必须重写 |
| `mobility` | 静态、匀速、RandomWaypoint 等移动模型 | 数学算法可迁移；`Step(dt)` 生命周期不可迁 |
| `nodes` | 节点类型/节点配置 | 能力与角色混合，需要拆成 Capability 与每周期 Role |
| `topology` | 静态边和拓扑构建 | 可作为配置拓扑输入；不能代替 M3 质量连通图 |
| `protocol` | Passive/ALOHA/CSMA/TDMA/Polling/ExternalSchedule；Static/Flooding/AODV-like/OLSR-like | 算法意图可参考；运行机制为每 tick 允许/拒绝，和 P0.4 冲突 |
| `propagation` | Simple、SSP Ray、Bellhop 查询、arrivals、NLOS/镜像源 | Bellhop/arrivals 资产代码价值高；公共边界需改成 Channel Provider |
| `noise` | Constant、Wenz、Composite | 公式可迁；量纲、带宽和 PHY 归属需重建 |
| `measurement` | 量测生成、误差和部分定位输入 | 算法可迁；随机流和数据合同需适配 |
| `hil` | UDP/串口/容器输出桥接和日志 | Transport 可留；缺少正式外部时间映射及接收注入语义 |
| `repository` | 场景/节点/环境等仓储接口草案 | 可参考命名；需按一次 ScenarioRuntime 的依赖装配重写 |
| `tools` | WOA/GEBCO/Bellhop 环境生成、执行、预处理和回归脚本 | 离线环境资产平面的主要复用来源 |
| `data` | SSP、地形、Bellhop、arrivals、环境数据库和模板 | 可迁资产；需 manifest、版本、单位、coverage 和校验 |
| `schemas`、`scenarios`、`examples` | 旧场景 schema 和回归场景 | 产品思路可留；需升级为 ProtocolBundle/EnvironmentAsset/Capability schema |
| `backend` | FastAPI；资产管理、WOA/GEBCO/Bellhop、运行进程、SSE、报告 | 大面积保留；新 Run API 不得参与仿真时间语义 |
| `frontend` | React 场景编辑、环境、实验、运行监控和回放 | 大面积保留；移除 fixed-tick 配置语义 |
| `师弟代码` | 环境 HTTP 传播、特征提取、误包模型和旧主程序副本 | 仅迁纯算法；运行时 HTTP Channel 和固定步主程序不迁 |
| `third_party` | BellhopCUDA 等工具 | 保留为离线工具依赖，不进入在线 Runtime 热路径 |

旧运行链如下：

```text
Scene JSON
  -> ConfigLoader
  -> src/main.cpp 创建 mobility/MAC/routing/propagation/noise 与若干 map
  -> for each fixed tick
       -> MobilityModel::Step(dt)
       -> 业务/外部 schedule 到期检查
       -> RoutingTable::Resolve()
       -> MacScheduler::Evaluate()/OnTransmit()
       -> for each resolved receiver
            -> TransmissionModel::Evaluate()
            -> MeasurementEngine::Generate()
            -> NoiseModel::Evaluate()
            -> 投递判断、转发入队、tx/rx trace
  -> 文件输出/报告/HIL 输出
```

这条链不具备 P0.4 要求的 PlanningCycle、WorldSnapshot、CycleWorkingState、ProtocolKnowledgeState、单次 Commit 和三级通信对象。

---

## B. 旧模块迁移分类

分类含义：`REUSE` 基本直接保留；`ADAPT` 主体保留并适配新合同；`PORT` 只迁算法核心；`REWRITE` 按 P0.4 重写；`RETIRE` 不进入新系统。

| 旧资产/模块 | 结论 | 新系统落点与说明 |
|---|---|---|
| JSON 基础解析、通用校验辅助 | REUSE | M7；不得带入 fixed-tick 默认语义 |
| `SceneConfig`、scene schema、模板 | ADAPT | M7；改为 NodeCapability、ProtocolBundle、EnvironmentAsset、run/cycle 配置 |
| `src/main.cpp` 运行内核 | REWRITE | M1/M2/M7；只把少量字段映射和输出语义作为参考 |
| `SimulationManager` / `ConsoleSimulationManager` | RETIRE | 由 M1 `IKernelGateway + CycleCoordinator` 取代 |
| `FactoryRegistry` | RETIRE | P0.4 使用隶属于单次 `ScenarioRuntime` 的非全局 `ComponentRegistry` |
| 旧 Node 类型/角色枚举 | PORT | M2/M3；拆为 `NodeCapabilityProfile`、`NodeCommittedState`、`RoleTable` |
| Mobility 数学模型 | PORT | M2 `StateProjector`/移动 provider；由查询时刻投影，不再 `Step(dt)` |
| 静态拓扑输入 | ADAPT | M3 `ConfiguredConnectivityBuilder` 或 `LogicalTopologyBuilder` 输入 |
| 旧 ALOHA/TDMA/CSMA scheduler | PORT | M4 Planner/Runtime Hook；废弃每 tick `Evaluate` 生命周期 |
| Polling、ExternalSchedule MAC | PORT/后置 | 非 P0 基线，只在未来有明确需求时迁算法 |
| FDMA | REWRITE | M4；旧系统没有完整实现 |
| Static Routing | PORT | M4 `StaticRouting` |
| BFS 最短路 | PORT | M4 `ShortestPathRouting`，输入改为 M3 `ConnectivityGraph` |
| DirectToSink | REWRITE | M4 P0 基线 |
| Flooding | PORT/后置 | 不属于当前三种基础路由验收 |
| AODV-like、OLSR-like | RETIRE | 当前不是完整协议，且不属于 P0；避免延续误导性协议名称 |
| Simple/SSP-Ray/ImageSource 传播算法 | PORT | M5 的多个 `IChannelFieldProvider` 实现 |
| WOA/Argo、温盐到 SSP | ADAPT | Environment：`IEnvironmentSource`、`ISoundSpeedModel` |
| GEBCO、地形和 BTY | ADAPT | Environment：`IBathymetrySource`、Bellhop builder |
| Bellhop ENV/BTY builder、runner、ARR parser | REUSE/ADAPT | 离线 Bellhop pipeline；统一输出新资产 manifest/payload |
| `BellhopTransmission` 查询逻辑 | PORT + REWRITE 接口 | `BellhopChannelProvider + AcousticFieldAtlas` |
| `ArrivalsDatabase` 网格读取/查询 | ADAPT | Environment repository；补 frequency profile、coverage、错误语义 |
| Wenz/Constant/Composite Noise | PORT | M5 `INoiseProvider`/PHY；先冻结单位和带宽合同 |
| 量测与定位算法 | ADAPT/PORT | Measurement Provider + Localization Algorithm；不依赖旧 Node |
| 旧随机误差/PER 采样 | PORT | 使用可复现随机流，不能依赖调用次数和进程级 RNG |
| UDP/串口/容器 Transport | ADAPT | M6；增加 ExternalDescriptor、TimeMapper 和经 M1 注入 |
| 旧 HIL bridge 的运行时间语义 | RETIRE | 不能让外部文件/设备推进系统时间 |
| Backend 资产、实验、SSE 外壳 | ADAPT | 新 Asset/Scenario/Run API；SSE 只观测 M8 输出 |
| Frontend 页面与回放 | ADAPT | UI timer 只做播放；场景表单升级到新 schema |
| SSP/地形/Bellhop/arrivals 数据 | REUSE/ADAPT | 转换、校验、版本化后进入 EnvironmentAssetRepository |
| `师弟代码` channel feature/PER 公式 | PORT | M5 内部算法候选，须独立验证 |
| `师弟代码` 运行时 HTTP 环境查询 | RETIRE | P0 在线运行只查本地离线资产；不能阻塞事件线程 |
| build/cache/result/node_modules | RETIRE | 不作为迁移输入 |

---

## C. 重点问题代码位置

### C.1 固定时间步推进

- `include/ns3_factory/config/scene_config.h:69`：`time_step_ms`。
- `schemas/scene.schema.json:33`：固定步长 schema。
- `src/config/config_loader.cpp:631-633`：解析 `simulation.time_step_ms`。
- `src/main.cpp:1326`：`dt = time_step_ms / 1000.0`。
- `src/main.cpp:1818`：`for (int step = 0; step <= total_steps; ++step)`。
- `src/main.cpp:1824`：逐步调用 Mobility `Step(dt)`。
- `师弟代码/main.cpp:1138,1691`：相同固定步推进副本。
- Backend、Frontend 和大量场景继续暴露 `time_step_ms`，例如 `backend/main.py:1313`、`frontend/src/types.ts:160`。

结论：字段可以在旧场景 converter 中识别，但不得成为新 Runtime 的调度参数。若为兼容旧输入，应明确报 deprecated，并转换为与业务无关的迁移提示，不能转换成第二套 tick。

### C.2 SimulationManager

- `include/ns3_factory/core/simulation_manager.h:7`：旧接口。
- `src/core/console_simulation_manager.cpp:12`：`Run()` 仅检查初始化并打印摘要。
- `src/main.cpp:2143`：在旧运行/输出装配后部创建 manager。

结论：`ConsoleSimulationManager` 名称与实际职责不符，整体 RETIRE。

### C.3 Node 状态更新

旧代码没有单个字面意义上的 Giant Node，但形成了“巨型编排 + 分散状态”的等价问题：

- Mobility 对象内部持有并更新位置：`src/mobility/random_waypoint.cpp:83,139`。
- `src/main.cpp:1400-1460`：业务序号、下一发送时间、relay queue、seen packet 等 map。
- `src/main.cpp:2039-2059`：中继队列取出、失败后按 `dt` 延后并重新入队。
- `src/protocol/routing_table.cpp:270-337`：AODV-like cache/discovery 状态。
- `src/protocol/mac_scheduler.cpp:58`：进程级全局信道忙状态。

P0.4 所说“旧 Node 巨型行为类”在当前代码中的准确表现是：节点相关行为被 `main.cpp` 和多个有状态模型共同隐式拥有，而不是已经存在一个可直接拆分的 Node 类。

### C.4 MAC 调度

- `include/ns3_factory/protocol/mac_scheduler.h:39`：`Evaluate + OnTransmit` 接口。
- `src/protocol/mac_scheduler.cpp`：Passive、ALOHA、CSMA、TDMA、Polling、ExternalSchedule。
- `src/protocol/mac_scheduler.cpp:58-62`：`global_channel_busy_until_seconds`。
- `src/protocol/mac_scheduler.cpp:163-165`：CSMA 使用全局忙状态。
- `src/main.cpp:1631`：每次尝试调用 `Evaluate`。
- `src/main.cpp:1655`：通过后调用一次 `OnTransmit`。

问题：

- 旧接口回答“当前 tick 允许/拒绝”，不是生成 `TxOpportunity`。
- 旧 ALOHA 带有最低 ready-node 争用/回退语义，不等同于 P0.4 Pure ALOHA。
- CSMA 全局忙状态会导致场景间污染，且不是按接收处到达信号构造的 ChannelSense。
- FDMA 缺失。

### C.5 Routing

- `include/ns3_factory/protocol/routing_table.h:35`：运行时 `Resolve()`。
- `src/protocol/routing_table.cpp:69-121`：BFS/最短跳核心。
- `src/protocol/routing_table.cpp:220`：Static。
- `src/protocol/routing_table.cpp:238`：Flooding。
- `src/protocol/routing_table.cpp:270`：AODV-like。
- `src/protocol/routing_table.cpp:348`：OLSR-like。
- `src/main.cpp:1534`：发送尝试中即时 Resolve。

结论：Static 和 BFS 核心可迁；路由必须在周期规划时基于 M3 产物形成 `RoutingPlan`，不能继续通过可写运行对象隐式决策。

### C.6 Packet Send / Receive

- `src/main.cpp:249`：`PendingPacket` 混合业务信息、调度和转发状态。
- `src/main.cpp:1493`：`attempt_transmit_packets` 总入口。
- `src/main.cpp:1534`：逐 packet 路由解析。
- `src/main.cpp:1591-1629`：按 next-hop/peer 整理待发 packet。
- `src/main.cpp:1655`：发送节点 MAC 状态更新一次。
- `src/main.cpp:1657`：进入 receiver/peer 循环。
- `src/main.cpp:1671-1679`：每个 peer 单独执行传播、量测和噪声。
- `src/main.cpp:1695-1719`：每个 peer 写一条 `tx` trace。
- `src/main.cpp:1721` 以后：构造接收事件、投递和转发。

旧系统没有 `DigitalPacket`、`TransmissionSession`、`ReceptionSession`、`ReceiverWindow` 或 `InFlightSignalLedger`。

### C.7 Bellhop 查询

- `tools/bellhop_env_builder.py`：ENV/BTY 等离线构建。
- `tools/bellhop_runner.py`：Bellhop 执行与兼容网格生成。
- `tools/bellhop_preprocess.py`：环境预处理。
- `src/propagation/bellhop_transmission.cpp:92-176`：加载配置/资产。
- `src/propagation/bellhop_transmission.cpp:316`：二维 `range + rx_depth` 插值。
- `src/propagation/bellhop_transmission.cpp:355`：传播求值。
- `src/propagation/arrivals_database.cpp:528`：arrivals 的 tx-depth/rx-depth/range 查询。

旧实现与 P0.4 的差距：

- 旧二维 grid 忽略 tx depth；arrivals DB 有三个空间/深度轴，但 frequency 主要是单资产元数据。
- 越界存在静默钳位风险。
- 无 multipath 时旧代码可合成直达路径，可能掩盖“资产缺失/无到达”。
- 新接口必须区分精确命中、标量插值、多径最近网格、越界、资产缺失和物理无到达。

### C.8 Noise

- `include/ns3_factory/noise/noise_model.h`：旧统一接口。
- `src/noise/wenz_noise_model.cpp:19-65`：Wenz 公式。
- `src/noise/noise_factory.cpp:28-72`：模型装配及硬编码 fallback。
- `src/main.cpp:1676-1681`：receiver 循环内把 `receive_power_db - aggregate_noise_db` 作为 SNR。

Wenz 注释中的输出是 `dB re 1 μPa²/Hz` 谱级，旧运行链未明确带宽积分和统一参考量。必须先冻结单位合同，再迁公式。真实重叠通信信号应由 `InFlightSignalLedger/ReceiverWindow` 形成干扰，不应由 NoiseModel 随机生成。

### C.9 HIL

- `include/ns3_factory/hil/hil_bridge_application.h:8-22`：消息只有 payload、sequence、timestamp 等基础字段。
- `src/hil/hil_container_bridge.cpp:81`：`Send()`。
- `src/hil/hil_container_bridge.cpp:96-101`：由日志构造并发送消息。
- `src/hil/hil_container_bridge.cpp:142-317`：UDP/串口 transport。
- `src/main.cpp:2016`：固定步循环中输出 HIL entry。

未发现完整的外部输入接收、外部时间到 `SimTime` 的映射、迟到策略、未来事件策略和重放一致性。Transport 可迁，时间语义必须重写。

### C.10 全局单例 Factory

- `include/ns3_factory/core/factory_registry.h:9` 定义 `FactoryRegistry`，但没有发现实际使用或全局 singleton 实例。
- 真正的全局可变状态是 `src/protocol/mac_scheduler.cpp:58` 的信道忙时间，以及 Backend 的 `_run_state`、`_run_history` 等模块变量。

新 `ComponentRegistry` 应由一次 `ScenarioRuntime` 持有，不做全局 singleton。

### C.11 与时间推进有关的 callback

- `backend/main.py:4994-5001`：subprocess 线程通过 `call_soon_threadsafe` 向 SSE 队列投递日志。
- `frontend/src/pages/StudioPage.tsx:2383-2449`：`requestAnimationFrame`、`setTimeout`、`setInterval` 驱动 UI 回放和耗时显示。
- 旧路由、传播、噪声接口普遍传递 `double simulation_time_seconds`，但没有统一时间权威。

这些 callback 可以保留为 I/O/展示机制，但不得进入仿真因果控制链。所有影响仿真结果的外部输入必须经 M6 映射后，通过 M1 安装为 ns-3 事件。

---

## D. 一对多重复发送风险专项结论

### D.1 直接检查结果

源码中没有发现对通信发送端调用的 `sender.Send()` 或 `StartTransmission()`。唯一的 `Send()` 是 HIL transport：`HilContainerBridge::Send()`。

旧主流程当前是：

```text
MacScheduler::OnTransmit()          # 一次
for peer in resolved_peers:
    TransmissionModel::Evaluate()   # 每 receiver 一次
    MeasurementEngine::Generate()   # 每 receiver 一次
    NoiseModel::Evaluate()          # 每 receiver 一次
    append tx trace                 # 每 receiver 一条，语义有误导
    create/process rx result
```

所以当前没有“MAC OnTransmit 已重复 N 次”的既成错误，但存在同等严重的结构风险：系统没有独立对象表示“一次真实发射”，也没有地方集中保证队列、Radio、编码、TxStart/TxEnd 和能耗只结算一次。

### D.2 新系统必须编码成不变量

1. `StartTransmission()` 不接受 `receiver_id`。
2. 一个进入执行的 `TxOpportunity` 恰好创建一个 `TransmissionSession`，且该数量与 receiver 数量无关。
3. `TransmissionSession` 创建后，发送端出队、Radio `IDLE→TX`、TxEmission 生成和发送能耗开始结算各一次。
4. receiver selector 的展开发生在 Transmission 创建之后，只创建 0..N 个 `ReceptionSession`。
5. 每个 Reception 独立查询 Channel，但共享同一个 Transmission/TxEmission。
6. `RxStart/RxFinalize` 禁止调用 `StartTransmission`。
7. `TxEnd` 按 TransmissionId 幂等完成一次，不按 Reception 数量触发。
8. 多对一必须先进入 `InFlightSignalLedger`，再按接收节点与重叠窗口构造 `ReceiverWindow`。

### D.3 最小自动化检查

广播场景 N2→{N1,N3,N4} 应断言：

```text
TxOpportunity        1
TransmissionSession  1
TxStart / TxEnd      1 / 1
TxEmission           1
queue consume        1
TX energy settlement 1
Radio TX transition  1 round trip
ReceptionSession     3
ChannelQuery         3
```

该测试正式纳入 P0-S1，作为 Core Closed Loop 的结构回归保护。

### D.4 Trace 迁移规则

旧代码在 receiver loop 内为每个 peer 写一条 `tx` trace，该语义不得迁移。新系统的统计和 Trace 身份必须与三级通信对象一致：

- 一个 `TransmissionSession` 发布一条 Transmission trace。
- 每个 `ReceptionSession` 分别发布一条 Reception trace。
- 广播接收节点数量只影响 Reception trace 和 ChannelQuery 数量，不影响 Transmission trace 或发送次数统计。
- Trace payload 只保存自身只读摘要与相关 ID，不嵌入完整 Packet、WorldSnapshot、ProtocolCyclePlan 或 runtime owner。

---

## E. Platform 新目录结构

不在本轮创建。实现时按 P0.4 第 17 章落地，并补充必要的构建和模块测试目录：

```text
Platform/
├─ CMakeLists.txt
├─ cmake/
├─ docs/
│  ├─ NS3-BELLHOP_P0.4_软件架构与开发实施设计基线.docx
│  └─ adr/
├─ contracts/                  # 公共数据结构与跨模块服务合同
│  ├─ include/ns3_factory/contracts/
│  └─ tests/
├─ kernel/                     # M1：ns-3 网关、事件安装、CycleCoordinator
│  ├─ include/
│  ├─ src/
│  └─ tests/
├─ runtime/                    # M2：状态、通信执行、ledger、commit
│  ├─ include/
│  ├─ src/
│  └─ tests/
├─ structure/                  # M3：角色、连通图、逻辑拓扑
├─ planning/                   # M4：路由、MAC、CycleTiming、runtime hook
├─ phy/                        # M5：Tx PHY、Channel、Rx PHY、noise/interference
│  ├─ abstract/
│  ├─ waveform/
│  └─ mock/
├─ environment/                # 离线环境资产、Atlas、repository、validator
├─ adapters/                   # M6：BIN/UDP/串口/文件/外部时间
├─ assembly/                   # M7：schema、registry、compatibility validator
├─ observability/              # M8：trace、metrics、checkpoint、archive
├─ algorithms/                 # AS-MAC/AB-MAC 等复合协议插件
├─ tests/
│  ├─ unit/
│  ├─ integration/
│  ├─ acceptance/
│  └─ fixtures/
└─ tools/                      # 旧资产 converter 和离线环境工具入口
```

目录级规则：

- `kernel` 是唯一允许直接依赖 ns-3 Scheduler API 的平台模块。
- `runtime` 不创建事件队列，只经 `IKernelGateway` 安装/取消事件。
- `structure`、`planning` 只能读取规划视图并返回计划，不能获取可写 Node/WorldState。
- `phy` 不访问 WorldStateStore；Channel Provider 不等于完整 PHY。
- `adapters` 不推进时间，只生成带目标 `SimTime` 的外部输入事件。
- `observability` 只读订阅，错误不得改变仿真控制结果。
- 旧代码通过 converter/adapter 单向进入新系统；`Platform` 不 include `../include/ns3_factory/...` 的旧运行时私有头文件。

---

## F. contracts-v0.1 头文件与依赖方向

P0.4 第 16.1 节已经给出正式建议清单。v0.1 应优先保持这些平面文件名，不为“通用性”提前拆出多层抽象。

| 头文件 | v0.1 最小内容 | 直接依赖 |
|---|---|---|
| `common.hpp` | 基础数值别名、不可变小值对象辅助 | 标准库 |
| `time.hpp` | `SimTime`、`SimDuration`；不得提供 clock/tick/advance | `common` |
| `identity.hpp` | Cycle/Node/Packet/Transmission/Reception/Action/Resource/Asset 等强类型 ID | `common` |
| `errors.hpp` | 稳定错误码、Status/Result | `common` |
| `node_capability.hpp` | 能力、双工模式、Radio capability；不含周期角色 | `identity` |
| `state.hpp` | NodeCommittedState、WorldSnapshot、连续状态 | `time, identity` |
| `working_state.hpp` | `WorkingStateView` 等跨模块只读合同；不暴露可写 owner | `state, delta` |
| `knowledge.hpp` | KnownNodeState、KnownLinkState、KnowledgeView、KnowledgeDelta、信息范围与新鲜度 | `state, identity, time` |
| `role.hpp` | ProtocolRole、RoleTable、角色 planner DTO | `identity, knowledge` |
| `connectivity.hpp` | 有向边、质量估计、ConnectivityGraph、estimator query/interface | `state, node_capability` |
| `topology.hpp` | Peer/Star/Custom LogicalTopology | `role, connectivity` |
| `traffic.hpp` | 业务流、队列选择、PacketSelector | `identity, time` |
| `routing.hpp` | RoutingPlan、next-hop/path、路由 planner DTO | `traffic, topology` |
| `mac.hpp` | MacPlan、TxOpportunity、ReceiverSelector、FrequencyAllocation、RuntimeDecision、CSMA runtime hook 合同 | `routing, time, node_capability` |
| `protocol_plan.hpp` | M3+M4 统一 ProtocolCyclePlan、IProtocolCyclePlanner、manifest refs | `role, connectivity, topology, routing, mac, cycle` |
| `event.hpp` | PlatformEvent、EventPhase、稳定排序键、EventHandle | `time, identity` |
| `cycle.hpp` | CycleTiming、阶段、终止原因、CycleContext | `time, identity` |
| `packet.hpp` | DigitalPacket；只表达信息和不可变身份 | `identity, traffic` |
| `transmission.hpp` | TransmissionSession、TxEmission 元数据、发送生命周期；通过 PacketId 引用包 | `packet, mac, state` |
| `reception.hpp` | ReceptionSession、ReceiverWindow；通过 TransmissionId 引用发送 | `transmission, channel` |
| `channel.hpp` | ChannelQuery/Response、PropagationPath、IChannelFieldProvider | `time, identity, environment` |
| `environment.hpp` | EnvironmentSnapshot、AcousticEnvironmentAsset/Atlas、repository | `identity, time` |
| `waveform.hpp` | WaveformBuffer、sample descriptor；未知格式显式 UNKNOWN | `time, errors` |
| `phy.hpp` | ITxPhy、IRxPhy、PhyResult、noise/interference 输入 | `packet, transmission, reception, channel, waveform` |
| `external.hpp` | IExternalInputAdapter、IExternalTimeMapper、IBinWaveformAdapter、descriptor | `time, waveform, event` |
| `delta.hpp` | Queue/Energy/Radio/Knowledge 等 delta 和 DeltaSet | `state, identity` |
| `trace.hpp` | TraceEvent、ITraceBus、Trace 自有的只读摘要 payload | 仅 `time, identity, errors` |

### F.1 关键依赖约束

```text
common/time/identity/errors
        ↓
node_capability + state + environment + traffic
        ↓
knowledge + role + connectivity + topology + cycle + event
        ↓
routing + mac + protocol_plan

packet
  ↓ PacketId
transmission
  ↓ TransmissionId
reception

environment → channel
packet/transmission/reception/channel/waveform → phy
time/event/waveform → external

time + identity + errors → trace 自有摘要 payload
```

禁止的头文件依赖：

- `packet.hpp` 反向 include `transmission.hpp` 或 `reception.hpp`。
- `state.hpp` include planner、PHY、Bellhop 或 HIL 类型。
- `channel.hpp` include Bellhop 文件格式或具体 repository 实现。
- `protocol_plan.hpp` include ns-3 scheduler 类型。
- `working_state.hpp` 暴露 runtime 内部可写 `CycleWorkingState` owner。
- `knowledge.hpp` 暴露 runtime 内部可写 `ProtocolKnowledgeStore` owner。
- `trace.hpp` include 完整 WorldSnapshot、ProtocolCyclePlan 等业务对象，或暴露可写控制返回值。
- contracts include 任一模块的 `internal/` 头文件。

### F.2 v0.1 需要在评审时明确的合同语义

- `SimTime`/`SimDuration` 是 Platform 自有强类型值，底层为 `int64` nanoseconds；没有 `Now/Advance/Tick` 能力，也不暴露 `ns3::Time`。
- `ns3::Time <-> SimTime` 转换仅存在于 kernel/M1；运行时唯一 `Now()` 来源是 M1/ns-3。
- 一个进入执行的 `TxOpportunity` 恰好创建一个 Transmission，receiver fan-out 不得重新进入发送逻辑。
- `DigitalPacket` 重传保持 PacketId；每次物理重传创建新 TransmissionId。
- `ReceiverWindow` 是接收 PHY 的联合输入，不是按包独立调用的便利容器。
- 所有 dB、Hz、秒、米、焦耳等字段必须在注释或强类型中明确单位和参考量。
- 外部 BIN descriptor 除 `bit_width=16` 外全部是 optional/UNKNOWN，解析前必须验证完整性。

---

## G. 第一阶段 P0-S0、P0-S1 最小提交计划

每项按一个可独立评审提交切分。提交中不得混入旧系统重构。

### G.1 P0-S0：Contracts Freeze

| 提交 | 内容 | 最小验收 |
|---|---|---|
| P0-S0-01 | `Platform` CMake 骨架、ns-3.47 最小依赖探测、模块 target 和 smoke test | 新目录独立配置/编译，不链接旧 main，不引入完整 ns-3 网络协议栈 |
| P0-S0-02 | `common/time/identity/errors` | `int64` nanoseconds 强类型时间；NodeId{0} 合法；错误码稳定 |
| P0-S0-03 | `node_capability/state/delta/working_state` | Snapshot 不可写；contracts 只暴露 WorkingStateView，owner 留在 runtime |
| P0-S0-04 | `knowledge/role/connectivity/topology` | KnownNode/Link、View/Delta 可表达；不暴露可写 KnowledgeStore |
| P0-S0-05 | `traffic/routing/mac/protocol_plan/cycle` | 能表达 Star+Direct+TDMA 的完整两周期计划 |
| P0-S0-06 | `packet/transmission/reception` | Packet/Tx/Rx 三种 ID 与生命周期不混淆 |
| P0-S0-07 | `environment/channel/waveform/phy` | Abstract/Waveform 共接口；Bellhop 只实现 Channel 边界和明确失败码 |
| P0-S0-08 | `external/trace` 和 Mock 接口 | BIN 未知字段不设默认值；Trace 只依赖 base 与自有摘要 payload |
| P0-S0-09 | contracts include/dependency tests 与 API review | 每个公开头可独立 include；无循环、internal/owner 泄漏 |

P0-S0 完成判据：各开发线可以只依赖 contracts 独立编译；接口评审通过；冻结项如需修改开始使用 ADR。

### G.2 P0-S1：Core Closed Loop

| 提交 | 内容 | 最小验收 |
|---|---|---|
| P0-S1-01 | ns-3.47 `Ns3KernelGateway` 最小接入 | `Now/Schedule/Cancel/Run/Stop` 由 ns-3 支撑；无自建 EventQueue 和完整协议栈依赖 |
| P0-S1-02 | EventPhase dispatcher 与稳定排序 | 按冻结 phase、稳定 ID 排序；动态事件只能进入当前 phase 之后的 phase |
| P0-S1-03 | `WorldStateStore` + 乐观版本 Commit | expected version 错误可检测；Snapshot 不可原地修改 |
| P0-S1-04 | runtime-owned `CycleWorkingState` + `StateProjector` | 对外只给 View；位置不随 callback 次数累加 |
| P0-S1-05 | runtime-owned `ProtocolKnowledgeStore` | 对外只给 KnowledgeView，更新通过 KnowledgeDelta |
| P0-S1-06 | `CycleCoordinator` 与 Cycle0 | t=0 直接规划并安装；与后续周期同路径；CycleClose 每周期 Commit 一次 |
| P0-S1-07 | `TransmissionManager` | accepted TxOpportunity 只生成一个 Transmission；发送端副作用一次 |
| P0-S1-08 | `InFlightSignalLedger` + `ReceptionManager` | 0..N Reception fan-out；支持 ReceiverWindow 多对一聚合 |
| P0-S1-09 | closed-cycle validator | CycleClose 前无 active Tx/Rx；剩余时间不足的动作延至下一周期 |
| P0-S1-10 | Mock TxPHY/Channel/RxPHY | 三段接口贯通；Mock 参数明确是测试夹具，不是物理默认值 |
| P0-S1-11 | 最小 M3/M4 fixture planner | Cycle1 N3 sink、Cycle2 N4 sink；输出 Star+Direct+TDMA，不实现通用算法 |
| P0-S1-12 | 四节点两周期集成场景 | 两周期运行、角色轮换、每周期一次 Commit、TDMA 顺序正确 |
| P0-S1-13 | 广播/多对一 invariant 回归 | 广播精确满足 1 opportunity/tx/start/end/emission/consume/energy、3 reception/query |
| P0-S1-14 | M8 最小 trace 和实验元数据 | 一次 Transmission 一条 Tx trace；每个 Reception 独立 trace；观测不影响控制结果 |

P0-S1 明确不包含：真实 ALOHA/FDMA/CSMA、Bellhop 资产迁移、真实波形、旧 UI/API 兼容或严格实时 HIL。这些属于 P0-S2 及后续里程碑。

---

## H. 可立即开发、占位、冲突与待确认项

### H.1 已冻结、可立即开发

- M1-M8 的职责和禁止反向依赖。
- ns-3 为唯一在线仿真时间权威。
- PlanningCycle 内事件执行、周期结束正式 Commit 一次。
- WorldSnapshot、CycleWorkingState、ProtocolKnowledgeState 分离。
- AlgorithmMemory 只预留。
- Capability 与本周期 Role 分离。
- M3 输出 RoleTable、ConnectivityGraph、LogicalTopology。
- M3 的能力检查、距离粗筛、质量判断、连通滞回流程。
- M4 输出 RoutingPlan、MacPlan、CycleTiming。
- Packet→Transmission→Reception 身份链和广播/多播不变量。
- ITxPhy→IChannelFieldProvider→IRxPhy 三边界。
- Abstract/Waveform PHY 使用相同外围合同。
- 固定 EnvironmentSnapshot、Bellhop 离线计算、运行时 Atlas 查表。
- P0 声场索引维度和标量/多径基础插值原则。
- M6 不能推进时间；ComponentRegistry 非全局 singleton。
- ns-3.47 为暂定基线，第一阶段只引入离散事件内核所需的最小能力。
- `SimTime/SimDuration` 使用 `int64` nanoseconds，contracts 不暴露 `ns3::Time`。
- 同 timestamp EventPhase 顺序、动态事件禁止回插、P0 closed-cycle、Cycle0 统一路径。
- NodeId{0} 合法；无节点必须使用 optional/显式状态。
- P0-S0 contracts 和 P0-S1 四节点两周期闭环；广播 invariant 属于 P0-S1。

### H.2 必须占位/TBD，不得猜测

- PSK、同步、解调和理想 DAC/ADC 内部细节。
- BIN signedness、endianness、sample rate、real/IQ、carrier、frame format。
- Preamble、CRC、帧检测和 codec。
- 海洋噪声场景 preset、带宽积分和单位基准。
- 严格实时 HIL、主机时间同步和迟到策略。
- AS-MAC/AB-MAC 首个复现优先级及 AlgorithmMemory schema。
- 声场轴采样密度、二进制存储实现和性能参数。
- 方向性/3D Bellhop。
- 正式 PHY 发送时长、源级、门限和能耗参数。
- M3 连通阈值、距离粗筛范围和默认滞回值。

### H.3 P0.4 文档与当前代码的冲突

1. P0.4 文档第 19.1 节称旧核心“借用了 NS3 概念和部分事件能力”，但当前构建/源码未发现实际 ns-3 接入。实施时应按“尚未接入 ns-3”估算工作量。
2. P0.4 要求无 fixed tick；旧 schema、Backend、Frontend 和大量场景仍把 `time_step_ms` 当正式字段。
3. P0.4 要求 M4 输出 TxOpportunity；旧 MAC 是运行时每 tick `Evaluate/OnTransmit`。
4. P0.4 要求质量主判和滞回；旧系统主要依赖静态边、手工范围或传播后判断，没有正式 M3 ConnectivityGraph 生命周期。
5. P0.4 要求三层通信对象；旧 `PendingPacket` 和 peer 循环没有 Transmission 身份。
6. P0.4 要求 ReceiverWindow 处理重叠信号；旧系统按 tx-rx 对独立算 SNR，不能表达真实多对一干扰。
7. P0.4 要求 Bellhop 作为 Channel Provider；旧 `BellhopTransmission` 被视为完整 TransmissionModel。
8. P0.4 要求外部时间映射后经 M1 注入；旧 HIL 主要是日志/Transport 输出。
9. P0.4 要求非全局 ComponentRegistry；旧 FactoryRegistry 虽未成为 singleton，但 MAC 存在实际进程级全局状态。

### H.4 ADR-0001 已冻结的实施决策

以下事项已经完成架构确认，不再作为开发人员可自行选择的设计点：

1. **旧系统事实**：当前旧系统没有真正接入 ns-3；`src/main.cpp` 的 fixed-step loop 才是真实运行内核。新 `Platform` 是首次建立真正的 ns-3 离散事件运行内核。旧 SimulationManager、fixed tick 和全局 MAC busy state 均不得进入新 Runtime。
2. **contracts 边界**：contracts 只保存跨模块交换数据、只读 View 和必要接口。可写 `CycleWorkingState` owner 和可写 `ProtocolKnowledgeStore` owner 都属于 runtime/internal。
3. **working/knowledge 合同**：`working_state.hpp` 主要定义 `WorkingStateView` 等只读合同；`knowledge.hpp` 定义 KnownNodeState、KnownLinkState、KnowledgeView、KnowledgeDelta 等跨模块合同。
4. **Trace 依赖**：`trace.hpp` 只依赖 SimTime、identity、error/status 和 Trace 自己定义的只读摘要 payload；不得 include 或嵌入完整 WorldSnapshot、ProtocolCyclePlan 等业务对象。
5. **Trace 身份**：一次 TransmissionSession 产生一条 Transmission trace；各 receiver 分别产生 Reception trace。旧 receiver loop 的 per-peer tx trace 不迁移，广播接收数量不影响发送次数统计。
6. **里程碑命名**：开发阶段使用 P0-S0 Contracts Freeze、P0-S1 Core Closed Loop、P0-S2 Protocol Baselines、P0-S3 PHY/Channel Integration、P0-S4 Environment/Bellhop Integration 等名称。M1-M8 仅表示架构模块。
7. **ns-3 与时间**：暂定 ns-3.47；第一阶段仅依赖离散事件内核所需最小能力。`SimTime/SimDuration` 是底层为 `int64` nanoseconds 的 Platform 强类型值，无 `Now/Advance/Tick`；contracts 不暴露 `ns3::Time`；双向转换只存在于 kernel/M1；唯一 `Now()` 来源为 M1/ns-3。
8. **同一时刻事件阶段**：顺序固定为 `SESSION_FINALIZE`（TxEnd/RxFinalize）→ `SIGNAL_ARRIVAL`（RxStart）→ `INPUT_READY`（Application/External input ready）→ `RUNTIME_DECISION`（Timer/BackoffExpire/CarrierSense/RuntimeHook）→ `TX_START` → `CYCLE_CLOSE`（Aggregate/Commit）。同 phase 内按稳定 ID 排序。
9. **动态同刻事件**：运行时在当前 timestamp 动态创建事件时，只能创建当前 phase 之后的 phase；禁止回插已执行 phase。排序不得依赖 unordered container、pointer 地址或线程完成顺序。
10. **P0 closed-cycle**：Commit 时不允许存在本周期未完成的 TransmissionSession 或 ReceptionSession。Planner/Runtime 必须保证发送、传播和接收在 CycleClose 前完成；ALOHA/CSMA 动作若剩余时间不足则延至下一周期。跨周期 active Tx/Rx 不在 P0 实现。
11. **Cycle0**：不设 bootstrap tick。`t=0` 从 WorldSnapshot S0 直接规划 ProtocolCyclePlan/CycleTiming 并安装 ns-3 事件；与后续周期使用同一代码路径。
12. **周期中新业务**：PacketReady 只入 Queue，不触发 M3/M4 重规划。后续已有 TxOpportunity 时由 PacketSelector 在 TxStart 选择；无可用 opportunity 则等待下一周期。只有未来显式 `IMacRuntimePolicy` 才能周期内生成协议动态动作。
13. **NodeId**：`NodeId{0}` 合法；任何“无节点”语义使用 optional 或显式状态，禁止使用 0 sentinel。
14. **Bellhop 失败语义**：P0 禁止越界 clamp、静默 extrapolation、无多径时合成直达路径、无频率时静默选择最近 profile。至少区分 `ASSET_NOT_FOUND`、`OUT_OF_COVERAGE`、`FREQUENCY_PROFILE_NOT_FOUND`、`NO_PHYSICAL_ARRIVAL`；只有 `NO_PHYSICAL_ARRIVAL` 是合法物理结果。
15. **旧 API 兼容**：P0-S0/P0-S1 不承担旧 Frontend/Backend API 兼容。后续通过 `LegacyScenarioConverter` 将 old schema 转为 new Scenario schema；新 contracts 不引入 fixed-tick 字段。
16. **广播验收**：N2 broadcast→{N1,N3,N4} 正式纳入 P0-S1，必须精确满足 TxOpportunity=1、TransmissionSession=1、TxStart=1、TxEnd=1、TxEmission=1、QueueConsume=1、TxEnergySettlement=1、ReceptionSession=3、ChannelQuery=3。

完整决策及后果记录在 `docs/adr/ADR-0001-runtime-foundation-decisions.md`。

### H.5 仍然存在的真正 TBD

以下事项没有被 ADR-0001 决定，继续按占位/配置/后续 ADR 管理：

1. **PHY/Waveform**：PSK、同步、解调、理想 DAC/ADC、真实发送时长、源级、门限和能耗模型。
2. **BIN/Frame**：除 16 bit 外的 signedness、endianness、sample rate、real/IQ、carrier、Preamble、CRC 和帧结构。
3. **Noise**：Wenz/其他噪声的带宽积分、统一量纲、参考量和场景 preset。
4. **HIL**：严格实时模式、主机基准时间同步、迟到/超前数据和实时调度策略；P0 仍只做 Replay/时间映射边界。
5. **复合协议**：AS-MAC/AB-MAC 首个复现优先级及各自 AlgorithmMemory schema。
6. **环境资产**：声场轴采样密度、最终二进制存储实现、压缩/分块和性能参数；方向性/3D Bellhop 后置。
7. **M3 参数**：候选距离、连接/断开阈值、质量指标权重和默认滞回配置。
8. **CycleTiming 参数**：各基础协议的默认周期长度、阶段长度和 guard time；所有值必须配置化，不能写入公共合同默认值。
9. **Kernel 调度 API 形态**：公开接口最终采用仅 `ScheduleAt`，还是同时提供命名明确的 `ScheduleAfter`；两者都必须由 M1 映射到 ns-3，不能形成第二时钟。
10. **Trace sink 工程策略**：异步缓冲、背压、丢弃/降级和落盘失败报告方式；无论选择何种策略，均不得反馈改变仿真控制结果。
11. **构建交付细节**：ns-3.47 的具体获取/固定方式、支持的编译器和 CI 平台；不得因此扩大到完整 ns-3 网络栈依赖。

---

## 结论与进入编码的门槛

下一步可以进入 P0-S0-01，但本轮按要求不创建 contracts、不接入 ns-3。H.5 中与具体公共签名直接相关的事项应在对应 P0-S0 提交评审时解决；其余 TBD 不阻塞 P0-S0/P0-S1 主干。

新系统实施期间应持续遵守双系统原则：旧系统只作为资产来源、算法参考和结果对照；`Platform/` 是唯一新实现目录，禁止通过兼容旧类把 fixed-tick、全局 MAC 状态或 tx-rx 对式发送语义带回新核心。
