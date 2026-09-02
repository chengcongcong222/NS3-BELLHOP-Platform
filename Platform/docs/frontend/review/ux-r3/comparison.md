# P0-UX-R3 产品语义与工作流审查记录

日期：2026-09-02
基线：`095dc302bc602573cdc699aab4cdfde158f56f63`（P0-UX-R2）

## 本轮收口

- 已发布环境视图只显示正式 DTO 可证明的 coverage、坐标轴与统计。接口没有逐点 SSP、Bathymetry、TL、SNR 或路径，因此不绘制它们。
- 从正式环境创建的本地草稿只继承范围、频率和来源 metadata；SSP、Bathymetry 与传播场内容为空，并在界面标明未继承。
- 环境输入以 SSP/Bathymetry 采样点表为主，可添加、删除、按首列排序；批量文本是高级入口。保存要求两类采样点各至少两个有效点。
- Scenario 默认只有节点；只有双选时才显示测距。没有 `TopologyEdge` / `ConnectivityGraph` DTO 时不补画网络连线。
- Scenario 与 Run 共享等比例 `WorldViewTransform`：正式环境范围优先，节点超出范围时才扩展；缺少范围时明确说明按初始坐标确定视野。
- Run 画布标为“场景初始几何 · 当前通信事件”。节点检查器不再宣称运行时位置；当前正式 DTO 没有时间索引位置投影。
- 回放按 `occurred_at_ns` 定位最近事件，速度表示仿真时间比例；过长静默间隔会提示“空闲间隔已压缩”。
- Running 进度仅为已收到 `CycleCommit` / `simulation_cycle_count`；terminal 前不读取结束时间。
- Reception 统一称为接收处理事件，并区分本地送达、旁听、未解码和中继入队。
- 案例展示按 `experiment_id + version` 注册；Acceptance profile 只是标签，不再决定案例身份。无注册项显示通用科研案例说明。
- 首页的新入口改为 `/new-simulation`，明确“从案例、已发布环境范围、空白环境/场景、既有场景实验”五个起点。

## 真实 catalog 限制

当前正式 Backend catalog 只有 `Acceptance4Node` 与 `Extended6Node` 两个案例，二者 profile 不同。两个同 Acceptance profile 案例仅作为前端回归 fixture 的条件，不新增或伪造正式 Backend catalog，也不制作冒充正式资源的 “12-case-two-same-profile” 截图。

## Running 证据原则

不为截图向 worker 增加延迟。若真实 worker 在浏览器采集前已完成，应记录其实际瞬时完成行为，并以真实 Completed 的事件回放作为可复现证据。

## 自动验证

- Frontend Vitest：32 passed。
- Frontend production build：通过。
- R3 新增语义测试覆盖派生环境真实性、世界坐标变换、Reception 分类及中文状态。

## 1440 × 900 浏览器证据

- `workbench.png`：工作台与新的仿真准备入口。
- `new-simulation.png`：五种真实工作流起点。
- `workspace-environment.png`：空白本地环境草稿、采样点表和“未合成示例曲线”状态。
- `workspace-scenario.png`：场景初始几何与无默认网络连线。
- `workspace-experiment.png`：本地实验参数工作区。

历史 R1/R2 证据保留于相邻目录；本轮不重写其历史材料。
