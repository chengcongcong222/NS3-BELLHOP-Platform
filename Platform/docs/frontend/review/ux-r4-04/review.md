# P0-UX-R4-04 Legacy Visual & Interaction Convergence

基线：`c0aa81501a4a3fbb6204852c2d4080473eff99d5`。

## 旧系统参考 → 当前页面

| 旧系统成熟形态 | 当前适配 |
|---|---|
| 52 px 图标导航、紧凑顶栏和深海蓝工作区 | 复用相同的窄轨、模块上下文、状态栏比例与颜色层级；保留新系统路由和系统信息。 |
| Studio 左节点、中画布、右检查器 | Runtime 恢复三栏；中央正式轨迹画布占主导，节点/事件检查器保持联动。Scenario 继续使用同一工具式布局。 |
| History 高密度工具条和轨道 | 六类正式事件按轨道显示，新增纯观察筛选；暂停、单步、定位和倍速仍只移动浏览器游标。 |
| 结果图表和链路表并列 | 保留五类科学指标，将概览压成指标带，其余采用双栏分析区、趋势图和逐链路表。 |
| Environment 剖面优先、Builder 分步 | 收紧步骤栏和分组，扩大真实 SSP/地形预览；保存仍走正式 authoring API。 |
| 紧凑实验参数区 | 缩窄步骤栏，Channel2 只在选中后展开真实调制/噪声配置。 |

旧组件没有直接复制：旧 `Shell`、`StudioPage`、`StudioCanvas`、`ProfileView` 的尺寸体系、三栏结构、工具条层级和深海配色被适配到当前 `AppShell`、正式资源 Builder 与 `StudioRunPage`。没有迁入旧 store、旧 API、旧仿真循环或 mock 数据。

## 数据边界

本轮没有新增 Backend、Runtime、Trace 或 DTO。Runtime 画布继续只消费 R4-03 的正式 `NodePosition`、`Transmission`、`SignalArrival`、`Reception`、`Fusion` 与 `CycleCommit`；`ChannelOutcome` 不冒充实际到达。时间轴筛选和播放控制均为观察行为，不写入 Run，也不建立第二套仿真时钟。

截图由真实 Edge、Backend、worker 与 ns-3.47 生成，没有请求拦截、mock 或硬编码运行事件。具体 Run 与浏览器版本记录在 `capture-evidence.json`。

## 验证

- Frontend Vitest：39/39；production build 通过。
- Backend Python、ns-3 kernel、Trajectory、Channel1 抽象回归、Channel2 正常/强噪声 smoke：10/10。
- `git diff --check`：通过。

## 截图

1. `screenshots/01-workbench.png`
2. `screenshots/02-environment-builder.png`
3. `screenshots/03-scenario-studio.png`
4. `screenshots/04-experiment-channel2.png`
5. `screenshots/05-runtime-tx-start.png`
6. `screenshots/06-runtime-signal-arrival.png`
7. `screenshots/07-runtime-fusion.png`
8. `screenshots/08-results-overview.png`
