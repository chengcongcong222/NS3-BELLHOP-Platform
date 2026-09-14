# P0-UX-R4-03 Runtime Event Visualization

基线：`96f285dad285a04d1fae2460ef986834db5468ee`。

## 权威链路

- `Transmission` 只在发送 session 成功建立后出现；`ChannelOutcome` 仍只是发送时的信道查询结果。
- `SignalArrival` 由正式 `SIGNAL_ARRIVAL` callback 在 ReceivedSignal 成功进入 in-flight ledger 后产生，时间等于发送侧 ChannelOutcome 时刻加正式 `first_arrival_delay`。它不由 ChannelQuery 推断或冒充。
- `Reception` 继续表示 finalize/decode/disposition 后的成功、失败、旁听或中继结果。
- `Fusion` 只在真实 FusionResult 成功保存后产生，不从验收结果或前端补造。
- `NodePosition` 在已有真实 trace 时刻查询运行绑定的 TRJ-01 轨迹；没有新增采样事件、实时循环或第二套时钟。

## Studio 行为

Studio 保持旧系统的中央场景、紧凑检查器和底部 Timeline 形态。六条操作轨道分别显示发送、信道求解、实际到达、解调/接收、融合和周期提交；位置样本只更新画布，不制造操作事件。相邻正式位置样本间可以视觉插值，禁止外推。

实时跟随、暂停观察、上一事件、下一事件、按仿真时间定位及 0.5/1/2/4 倍回放都只移动浏览器观察游标。页面分别标注仿真时间与播放速度；任何控制都不会调用 Runtime、推进 ns-3 或改变 Run Result。

## 真实验证边界

`capture.mjs` 使用真实 Backend、worker 和 ns-3.47 创建一个 Channel1 正式运行与一个通过真实 Authoring execution projection 启动的 Channel2 运行。无请求拦截、mock 或硬编码运行事件。运行在本机完成很快，因此截图是同一正式事件日志的 Completed 回放证据，不人为降低 worker 速度。

Channel2 默认浅海多径运行真实出现 CRC 成功和失败；失败画面来自 `Reception(NotDecoded)`。Channel1 产生真实 Fusion；Channel2 若有效交付不足则不补造 Fusion。完整计数、run identity、精确 arrival 校验和移动节点列表见 `capture-evidence.json`。

当前仍没有独立的 DecodeStart、应用层 Delivery 或逐条融合方位观测事件；Reception 表达完整接收处理的最终结果，Fusion 只表达已保存结果的只读摘要。界面因此不拆分或推测这些阶段。

## 验证

- Frontend Vitest：39/39；production build 通过。
- Backend/C++ OFF：67/67。
- Backend/ns-3.47 ON：101/101，包含 kernel smoke、Channel1、Channel2 正常/强噪声/融合/因果门禁与 Trajectory 回归。
- 真实 Channel1/Channel2：均为 18/18 个 SignalArrival 精确满足 `arrival = channel query time + first arrival delay`；移动节点 N10/N20/N30 均产生不同仿真时刻的正式位置。
- `git diff --check`：通过。

截图：

1. `screenshots/01-tx-start.png`
2. `screenshots/02-signal-arrival.png`
3. `screenshots/03-decode-success.png`
4. `screenshots/04-fusion.png`
5. `screenshots/05-trajectory-position.png`
6. `screenshots/06-timeline-single-step.png`
7. `screenshots/07-channel2-decode-failure.png`
8. `screenshots/08-channel2-arrival.png`
