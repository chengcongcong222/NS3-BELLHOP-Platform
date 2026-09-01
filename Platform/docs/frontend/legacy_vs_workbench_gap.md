# P0-UX-R1 旧版与仿真工作台能力差异矩阵

日期：2026-09-01
范围：旧仓库只读审计；新仓库 `feature/frontend-workbench-reconstruction`。

## 审计方法

- 全量扫描旧版 `frontend/src`、`backend`、API 路由、样式与运行脚本；识别 15,000 余行前端实现及约 50 个资源、构建、运行和归档接口。
- 在隔离端口实际启动旧版 FastAPI 与 Vite：首页、Environment、Studio、Templates、Assets、Settings 均可由真实 HTTP 访问。旧版 Linux 前端依赖在临时目录重装，未修改旧仓库。
- 实际启动 P0-S5-05 新版前后端并读取环境、场景、实验、运行和结果。新版正式资源与 Run/Result 边界完整，但编辑和产品工作流明显薄弱。
- 旧版完整仿真未执行：它要求 Windows Bellhop 可执行文件并会向旧仓库结果目录写入；交互和运行行为改由源码、API 与已有资产交叉恢复。
- WSL 内没有 Linux 浏览器，但可从 WSL 调用 Windows Microsoft Edge。已对工作台、案例详情、环境、场景编辑器、运行、结果及 390 px 响应式布局进行真实渲染审查；临时浏览器 profile 与截图仅保存在 `/home/ccc/build/`，未进入仓库。

## 产品判断

旧版最有价值的不是 DTO 或 fixed-tick 实现，而是“工作室”模型：二维画布、属性侧栏、深度剖面、环境建设、模板派生、运行趋势和归档。新版最有价值的是不可变正式资源、真实 Run 生命周期、RunEventProjection、大整数安全、后端验收权威以及 ns-3 因果边界。P0-UX-R1 保留两者的优点：以用户工作任务组织信息架构，以正式 API 作为运行权威，以本机 Draft 工作区恢复安全编辑能力。

## 差异矩阵

| 能力 | 旧版能力 | P0-S5-05 新版现状 | 决定 | P0-UX-R1 状态 |
|---|---|---|---|---|
| 首页 | 快速入口、场景拓扑、运行入口，但信息拥挤 | 主要显示五类资源计数 | 重构 | 工作台首屏说明平台、快速开始、推荐案例、最近运行和工作区 |
| 案例 | 以模板/场景隐式组织 | 无产品层案例概念 | 新建/改进 | Case 动态组合正式 Environment + Scenario + Experiment；含卡片、详情、直接运行与派生实验 |
| 环境建设 | 环境 Builder、SSP、地形、Bellhop/Grid 操作 | 无编辑 | 恢复并重构 | Environment Draft：基本信息、覆盖、频率、SSP、地形、离线 Bellhop 边界、校验与本机保存 |
| 环境管理 | 环境库 CRUD、导入、选择 | 只读资产目录 | 改进 | 已发布资产目录 + 本机草稿管理；不覆盖不可变正式资源 |
| 场景建设 | Studio Canvas 添加、拖动、编辑节点 | 只读 SVG | 恢复并重构 | 可编辑 N 节点二维画布、添加/删除/选择/拖动/精确输入/环境绑定 |
| 场景维护 | 保存场景、派生、模板应用 | 版本只读 | 改进 | 本机 Draft 持久化；正式发布 API 缺失时不伪造 publish |
| 节点编辑 | 巨型 PropertyInspector 覆盖大量字段 | 只读节点表 | 重构 | 聚焦位置、深度、收发能力、融合中心和运动速度的属性侧栏 |
| 节点运动 | 参数编辑和浏览器播放 | 仅 Scenario 初始速度 | 保留配置、淘汰假播放 | 草稿可配速度；运行页不按 wall clock 推演运动 |
| 网络拓扑 | StudioCanvas 链路、选择和高亮 | 静态初始拓扑 | 改进 | 编辑器显示几何关系；运行页由正式 Transmission/Channel/Reception 高亮活动链路 |
| 实验配置 | 模板与 Studio 内多处分散配置 | 只读实验 DTO | 重构 | 中文分区：基础、场景、网络、通信/物理层、仿真设置；本机保存 |
| 协议配置 | 多种路由/MAC 表单，部分超出新 Runtime | 只读正式 mode | 谨慎恢复 | 只提供当前正式支持的 DirectToFusionCenter / TDMA，不制造未实现选项 |
| PHY 配置 | 速率、频率、带宽、声源级等 | 只读 | 恢复 | Draft 可编辑正式已有字段；BER 来源继续明确标为模型证据 |
| 运行启动 | Studio 内启动并播放 | Experiment 详情可创建真实 Run | 改进 | 案例和正式实验均能启动真实 Run；草稿不提供假运行 |
| 运行控制 | Start、前端播放/倍速等混合 | 只有 Start | 淘汰伪控制 | 后端无 Pause/Resume/Step/Stop，因此不展示这些按钮 |
| 运行拓扑 | 画布按压缩 wall clock 播放 | 无动态拓扑 | 重构 | Scenario 正式几何 + RunEventProjection 活动链路；不生成位置 |
| 通信过程 | 事件回放、通信历史 | Trace 表格 | 改进 | 发送、Signal、NoArrival、Reception 用用户语言映射到拓扑和时间线 |
| 运行统计 | 多个统计卡和历史数据 | Trace-kind 计数 | 重构 | 按网络/信道/通信质量组织，只聚合正式事件字段 |
| 事件查看 | 原始/播放混合 | 工程字段表格 | 改进 | 默认中文事件叙述；sequence、ID 和 raw 语义折叠到技术详情 |
| 趋势统计 | MiniTrend 等轻量趋势 | 无趋势 | 恢复 | SVG 累计发送/接收、BER、传播时延趋势；无数据时明确为空 |
| 结果分析 | 历史、比较、导出与报告 | Acceptance-first | 重构 | 通用摘要、网络、信道、节点/融合、Trace；验收成为条件附加模块 |
| 融合结果 | 目标/融合展示 | 正式坐标图和表 | 保留并改进 | 保留正式 FusionResult 坐标和周期/观测表，无数据不补画 |
| 声学结果 | 剖面、声线、地形 | 仅 Environment axis 表 | 部分恢复 | 正式资产显示数据支持的覆盖/Signal/NoArrival；Draft 显示输入 SSP/地形 |
| 环境剖面 | ProfileView 联动地形、声线和节点 | 无 | 重构 | Environment profile + 场景深度条；逐点 TL/声线因 DTO 缺失不伪造 |
| 历史运行 | 历史、归档、比较、报告 | 正式 Run/Result catalog | 保留核心 | 运行/结果目录保留正式顺序；跨运行比较仍缺正式 projection |
| 案例管理 | 模板、派生和配置绑定 | 无 | 重构 | 正式资源自动组成 Case；可派生 Experiment Draft；案例发布维护后端仍缺 |
| 导入/导出 | JSON/MD、环境文件、报表 | 验收 evidence 下载 | 部分恢复 | 保留正式 evidence；Draft 为 localStorage 结构，文件导入/导出待正式 schema |
| 3D 水下视图 | Three.js 节点、水体、地形、声线 | 无 | 暂缓 | 旧实现有空间认知价值，但依赖较重且新 DTO 无地形/声线；优先高质量 2D+剖面 |
| 错误与空状态 | 分散且技术化 | 统一 API Error | 改进 | 统一错误、真实空状态和能力边界提示，避免演示数据占位 |
| 高级信息 | 与普通字段混排 | 大量 ID/checksum 直接显示 | 改进 | ID、version、checksum、schema、调度权威进入技术详情或系统信息 |

## 最终信息架构

1. **工作台**：快速开始、案例、最近运行、草稿与次级资源统计。
2. **案例中心**：以科研任务理解正式资源组合，支持直接运行和派生实验。
3. **环境建设**：已发布声学环境与 Environment Draft 建设流程。
4. **场景设计**：已发布场景与二维 N 节点 Draft 设计器。
5. **实验配置**：已发布实验与分步 Draft 参数工作区。
6. **仿真运行**：运行历史、生命周期、动态通信拓扑、统计、趋势和事件。
7. **结果分析**：通用网络/信道/融合结果；条件式验收模块。
8. **资源管理**：已发布版本和本机草稿的边界与维护入口。
9. **系统信息**：引擎、时间/调度权威、构建与接口版本。

## 明确不迁移的技术债

- 旧 fixed-step/fixed-tick 主循环、浏览器 wall-clock 仿真播放和浏览器 verdict。
- 巨型 StudioPage/PropertyInspector、组件内直接 fetch、全局混合 dataset。
- Windows filesystem path 作为 identity、在线 Runtime 调用 Bellhop、未受新 Runtime 支持的协议选项。
- 为展示效果伪造 NoArrival、节点位置、传播损失、SNR、BER、暂停或回放控制。

## 尚需后续正式能力

- 团队级 Draft CRUD / clone / validate / publish API 与不可变版本发布流程。
- Environment DTO 的实际 SSP、bathymetry、TL/NoArrival 网格 projection，以及离线 Bellhop 作业状态。
- Runtime 正式节点位置 projection，跨运行比较与通用导出 schema。
- 若数据与离线依赖成熟，再评估 3D 地形/声线路径视图。
