# P0-UX-R4-01 Legacy Frontend Rebase

基线：`1ec3d51fbd05ff39a2405dde564cbd21ec7158ce`。
独立分支：`feature/frontend-legacy-rebase-r4`；未触碰主工作区及旧系统。

## 迁移映射

| 旧能力 | 新权威数据 | 本轮处理 |
|---|---|---|
| Studio 紧凑暗色布局、导航、参数侧栏 | 现有资源与 Run API | 适配布局与字体层级，不复制旧 store |
| StudioCanvas 方向曲线 | Transmission / Reception | 提取纯 `buildEventCurve` 算法，适配强类型 DTO |
| Timeline / History 时间定位 | 原始 RunEventSequence、纳秒时间 | 适配播放/暂停/定位；不迁入 fixed tick |
| Profile 趋势与选链思路 | ChannelOutcome / Reception quality | 按时延、TL、路径数、SNR 展示真实记录；不补声线 |
| 结果图表与融合历史 | Result、FusionResult、只读来源摘要 | 五类统计面板、融合轮次选择与来源检查 |
| Scene3D | 当前缺三维环境点数据；新项目未供应 Three.js | 延后，不复制虚构地形或引入新依赖 |
| 环境、场景、实验编辑 | 新资源接口/本机草稿边界 | 保留正式数据语义；场景速度标为轨迹生成输入 |

## 最小只读投影

RunResult 增加可空 visualization；既有无该字段的消息仍可解码。
新字段进入 application → worker codec → Python wire → Result API；不进入 contracts 或 Runtime。
旧版严格 decoder 不认识新字段，因此 worker/backend 必须同版本部署；没有改 StartRunCommand。

- positions：对**运行实际绑定的不可变轨迹**，在 Trace 时刻、融合完成时刻及起止时刻调用同一 Query。
  不重新生成轨迹，不用最终位置插值，不读取浏览器时钟。最多 8192 个 node/time 样本，超出显式
  positions_complete=false；前端仅展示精确匹配时刻，不外推。轨迹虚线是样本折线，不是新运动模型。
- fusion_sources：复制正式 FusionResult.observation_identities；包含 NodeId 与 observation sequence。
  不从 generated_reports 恢复方位，避免把发送端数据冒充接收端数据。
- generated_feature_count：实际 generated_reports 数量。
- waveform：复制现有 WaveformReceptionSummary 的 payload bits、bit errors 与 CRC；原 observer 仍收到原记录。
  Channel1 不产生虚构 waveform 行。误码统计只用于结果，不影响解调或网络交付。

## 真实事件与降级

运行画布显示真实 Transmission 发送方向、Reception 最终处置与 FusionResult 完成标记。
ChannelOutcome 在发送阶段记录，只显示为信道查询；**不是到达事件**。没有 SignalArrival 日志时不播放到达动画。
FusionResult 是独立正式结果，不伪装成 SSE，也不生成新的 RunEventSequence。
回放按相邻记录仿真时间差/速度计算；100ms 可读下限、2s 空闲压缩上限均只影响观察，并明确标示压缩。

当前位置投影随 Completed Result 发布，故真实轨迹运动目前用于**完成后的历史回放**。
Running 期间未发布的位置显示初始几何及说明；不能宣称本轮已实现实时位置流。
当前公开实验/worker command 没有 Channel2 配置入口；实际误码映射已打通，但不偷偷把公开案例切换成 Channel2。
没有增加新的正式案例或为截图伪造数据。

## 结果页面

运行概览 / 网络性能 / 水声链路 / 波形通信 / 探测与融合。
结果详情、结果目录和概览不展示自动验收 verdict，未改 backend 历史报告计算。
Acceptance 参考要求可与本次值并列，最终判断由用户完成。模型 BER 不标为实测硬件 BER。

仍缺正式数据：业务包总数与去重交付率、端到端产生时刻/时延、交付字节/吞吐量、活动链路、重传/碰撞、
最大时延扩展、波形样本、逐条融合方位/时间/无效原因、独立成功交付特征数、场景轨迹资源详情。
这些项明确缺失，不用配置速率冒充吞吐量，不用 Tx-Rx 推导丢包，不用旁听接收充当成功交付。

## 验证与截图

Frontend：37 tests；production build。OFF：67/67；ON/ns-3.47：101/101。
ON 包含 kernel smoke、Channel1、Channel2 正常/强噪声/融合/因果门禁、Trajectory 回归。
Python backend：64 tests（包含 C++/Python 新字段往返）。新增投影重复运行一致性、最终位置一致性、
实际 waveform observer 对照、精确时刻查询、时间定位、无伪到达和无自动 verdict 测试。

`capture.mjs` 使用真实 backend 创建 Run，经 Chrome CDP 捕获 1440×900 页面，无网络拦截、mock 或场景硬编码。
`capture-evidence.json` 保存实际 Run ID、browser 版本、来源与样本数量。截图不冒称 Running；为真实 Completed/Replay。
截图在 `screenshots/01-workbench.png` 至 `08-results-acoustic.png`。
浏览器中文字体使用本机已有 Noto CJK，仅为截图进程配置，没有写入 C 盘或修改系统字体目录。

下一步优先：将已有位置投影扩展为运行中只读观测流，然后迁移 Profile 的选链剖面；
三维组件在环境与轨迹正式数据、离线依赖齐备后再迁移。
