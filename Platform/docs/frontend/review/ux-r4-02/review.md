# P0-UX-R4-02 Legacy Authoring Recovery

基线：`2af09e15d82237d00846ab5b56c0025ef6a1410f`。

## 迁移映射

| 旧系统能力 | 新系统数据权威 | 处理 |
|---|---|---|
| EnvironmentPage 的资源库、分步输入、Profile 和 Bellhop 面板 | Environment DTO、服务器 Authoring Workspace、离线参考 arrivals | 适配迁移；不迁旧 mock 数据 |
| StudioCanvas 的三栏布局、节点选择、拖拽、平面/深度视图 | Scenario DTO、服务器场景草稿、WorldViewTransform | 适配迁移；运动语义改为轨迹绑定 |
| ExperimentTemplates 的紧凑分组参数 | Formal Experiment preset + StartRunCommand | 适配迁移；未接入参数只读 |
| 旧前端仿真循环与旧 store | ns-3、Run API、worker | 淘汰 |

## 当前真实边界

- Environment、Scenario、Experiment 草稿由后端文件工作区保存，采用原子替换和 revision 乐观并发控制，不再以浏览器 localStorage 作为 authoring 权威。
- Environment 可导入 SSP/地形、使用仓库内参考数据、编辑底质和 Bellhop 参数并执行参考 arrivals 的离线构建验证。构建产物可以在 Authoring Workspace 内不可变固定，但当前不会自动进入进程启动时捕获的 Runtime resource catalog；界面明确显示这一限制。
- Scenario 恢复中央大画布、节点列表、节点属性、拖拽、双选测距和平面/深度视图。保存的是服务端草稿。正式 Trajectory catalog 尚未公开，因此只显示 `GeneratedBeforeRun` 边界，不伪造轨迹资源 ID，也不把 ConstantVelocity 作为产品入口。
- Experiment 可选择 Channel1 快速抽象物理层或 Channel2 波形物理层。Channel2 的 BPSK/BFSK、NoNoise/AWGN 和噪声 PSD 经 Authoring Run API 写入 StartRunCommand，由 worker 在组合根选择完整 PHY execution composition。
- Routing、MAC、速率、中心频率、带宽、声源级和保护间隔仍由正式 Acceptance preset 冻结，页面只读展示；未接入字段不会假装生效。仿真周期和 seed 是当前可执行参数。

## 验证

- Frontend Vitest：37/37。
- Frontend production build：通过。
- Backend/C++ OFF：67/67。
- Backend/ns-3.47 ON：101/101。
- 真实 worker Channel2 命令：完成，结果含 18 条真实 waveform BER/CRC 记录；默认浅海多径下存在 CRC 成功与失败，未包装成全链路成功。
- `git diff --check`：通过。

截图位于 `screenshots/`。截图只使用真实 Backend catalog 和服务器草稿；没有请求拦截或 mock 数据。
