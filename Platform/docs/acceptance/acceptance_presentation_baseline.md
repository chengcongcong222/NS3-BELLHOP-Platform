# P0-S5-05 Acceptance Presentation Baseline

后续 PPT、项目报告和验收说明统一引用本页，不分别创造数字或扩大证据范围。

## Platform and release

- Platform：以 **ns-3 discrete-event simulation kernel** 为唯一 simulation clock/event scheduling authority 的水声网络数字孪生平台。
- Release：`P0-S5-05`，source revision `@SOURCE_REVISION@`，Linux x86_64。
- Archive：`ns3-bellhop-platform-p0-s5-05-linux-x86_64.tar.gz`，SHA-256
  `@ARCHIVE_SHA256@`。
- 正式措辞：P0-S5-05 交付包在满足 ns-3.47 运行前提后，可在离线环境完成准备、启动、
  仿真、结果查看与验收证据导出。

## Architecture statement

Frontend → FastAPI → C++ Worker → RunService → ScenarioRuntime →
M1/Ns3KernelGateway → `ns3::Simulator`。Environment Asset 旁路进入 Channel Provider；
M3/M4 生成 plans；M5 提供 PHY；M8 输出 read-only Trace。Environment 不是 M6，M6 是当前
非主链的 External/HIL adapter boundary。

## Environment and propagation statement

`ReferenceShallowWaterV1` 是公开 WOA23 + GEBCO 2020 构建的
**Reference/proxy modeled environment**。正式 AssetId 为 `reference-shallow-water-v1`，
checksum 为 `fb64e543f9042c52`。传播为 **Bellhop-derived propagation**；Bellhop 只在离线
资产构建阶段运行，现场 Run 查询 immutable AcousticFieldAsset。

## Third-party requirements

| Metric | Requirement | Golden actual/verdict |
| --- | --- | --- |
| Network nodes | 3–4 | 4 / Pass |
| Communication rate | 60 bit/s | 60 bit/s / Pass |
| BER | ≤ `1e-4` | max/mean `0.0`, Modeled / Pass |
| Fusion | feature-level | AcceptanceBearingFusion / Pass |
| Bearing observations | ≥ 5 | 6 / Pass |
| Fusion period | ≤ 180 s | 24 s / Pass |

仿真模型 BER 使用 Bellhop-derived propagation、in-band noise 和 simplified BPSK/AWGN receive
model。`0.0` 可能表示达到浮点数值表示下限，不是硬件实测结论。

## Demonstration parameters

4 nodes、25 kHz、**110 dB re 1 µPa @ 1 m** simulation configuration、2 s TDMA guard、
10-cycle network refresh、约 5 km/h、约 1 km、ReferenceShallowWaterV1。这些参数不扩展
第三方硬指标。

## Golden demonstration run

Identity：P0-S5-05 + `acceptance4-experiment` v1 + `acceptance4-scenario` v1 +
Reference asset/checksum + deterministic seed/config；RunId 不属于 Golden identity。

Baseline-specific output：4 nodes、6 transmissions、18 Signal、18 Reception、6 local deliveries、
6 bearing observations、24 s fusion period、Modeled BER、overall Pass。normalized hashes 见
`acceptance_evidence_matrix.json`。

## Evidence authority

验收阈值 → release identity → captured configuration → simulation/result → UI presentation，按五级
证据层次追溯。UI 不重算 verdict；Backend `AcceptanceReport` 是 verdict authority。Run
`Completed` 与 Acceptance `Pass`/`Fail` 是独立维度。

## Explicit TBD

站点实测水文、硬件源级标定、硬件/实测 BER、HIL、waveform-level end-to-end hardware chain、
Windows native、durable storage、authentication 均未完成或承诺。
