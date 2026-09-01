# Final Acceptance Presentation Source — P0-S5-05

本文件是后续 PPT 的唯一事实/数字源；不在本阶段生成 PPTX。构建 handoff 时将
`@SOURCE_REVISION@`、`@ARCHIVE_SHA256@` 物化为最终 candidate/artifact identity。

## 1. 平台总体定位

NS3-BELLHOP Platform 是以 **ns-3 discrete-event simulation kernel** 为唯一仿真时钟和事件
调度权威的水声网络数字孪生平台。P0-S5-05 是 final acceptance-aligned release candidate。

建议截图：SS-01 Overview。

## 2. 系统架构

Frontend → FastAPI → C++ Worker → RunService → ScenarioRuntime → M1/Ns3KernelGateway →
`ns3::Simulator`。Environment Asset → Channel Provider；M3/M4 → Plans；M5 → PHY；M8 →
read-only Trace。Environment 不属于 M6。

建议图：`acceptance_architecture.md` Mermaid source。

## 3. ns-3 仿真内核

- engine/version：ns-3 3.47。
- time authority：`ns3::Simulator`。
- event scheduling authority：`ns3::Simulator`。
- Platform scheduling gateway：`M1 / Ns3KernelGateway`。
- kernel/dispatcher/signal lifecycle/multirun reset/Acceptance ns-3 ON tests 证明实际运行机制。

建议截图：SS-11 System info。

## 4. Bellhop/环境资产链

WOA23 + GEBCO 2020 → normalized environment → offline Bellhop → arrival file → parser/normalizer →
immutable AcousticFieldAsset → runtime query。`ReferenceShallowWaterV1` 是 Reference/proxy modeled
environment，传播是 Bellhop-derived propagation；现场 Run 不执行 Bellhop。

AssetId `reference-shallow-water-v1`；FNV1A64 `fb64e543f9042c52`；25 kHz；0–2500 m；
650 cells = 625 Signal + 25 NoArrival。

建议截图：SS-02 Environment Detail。

## 5. Acceptance4Node 场景

3 个移动探测节点 + 1 个固定融合中心，60 bit/s，feature-level bearing fusion。演示配置：
25 kHz、110 dB re 1 µPa @ 1 m（simulation configuration）、2 s guard、10-cycle refresh、
约 5 km/h、约 1 km。

建议截图：SS-03、SS-04。

## 6. 运行过程

真实 Backend → Worker → ScenarioRuntime → ns3::Simulator。Run lifecycle、typed Trace、Result 和
AcceptanceEvidence 分离；SSE/M8 只读且不改变因果结果。

建议截图：SS-05、SS-06。

## 7. 六项验收指标

| Metric | Requirement | Golden actual | Verdict |
| --- | --- | --- | --- |
| Nodes | 3–4 | 4 | Pass |
| Communication rate | 60 bit/s | 60 bit/s | Pass |
| BER | ≤ `1e-4` | max/mean `0.0`, Modeled | Pass |
| Fusion | feature-level required | AcceptanceBearingFusion | Pass |
| Bearing observations | ≥ 5 | 6 | Pass |
| Fusion period | ≤ 180 s | 24 s | Pass |

Modeled BER 不是硬件实测；`0.0` 是数值模型结果，高 SNR 下可能达到浮点数值表示下限。

建议截图：SS-07、SS-08。

## 8. Fusion Result

Golden Run 形成 6 个 bearing observations，formal FusionResult period 为 24 s；方位点不是节点数。

建议截图：SS-09。

## 9. 确定性与可重复性

Golden identity：P0-S5-05 + Experiment/Scenario v1 + Reference asset/checksum + seed/config，不使用
RunId。三次 Release Run 的 Result/Fusion/Trace/Acceptance normalized hashes 必须分别一致；最终值
记录在 handoff `final_release_record.md`。

## 10. 离线交付

Archive `ns3-bellhop-platform-p0-s5-05-linux-x86_64.tar.gz`，source revision
`@SOURCE_REVISION@`，SHA-256 `@ARCHIVE_SHA256@`。在提供规定 ns-3.47 runtime prefix 后，Release
可全程离线 prepare/start/Run/Result/Evidence/stop；不表示裸 Linux 无依赖运行。

## 11. 当前边界/TBD

站点实测水文、硬件源级标定、硬件/实测 BER、HIL、waveform-level end-to-end hardware chain、
Windows native、durable storage、authentication 均未完成或承诺。
