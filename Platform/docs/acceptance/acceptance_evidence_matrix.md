# Acceptance4Node Evidence Matrix

本矩阵只建立字段映射，不计算任何 verdict。第三方阈值的唯一机器权威是
`Platform/acceptance/acceptance4_baseline_v1.json`；运行 verdict 的唯一权威是
`AcceptanceEvidence.acceptance_report`。UI、本文和 JSON 矩阵均为只读展示。

## 第三方硬指标

| Requirement | Current actual | Verdict field | Evidence source/type | UI location | Backend/test reference | Limitation |
| --- | --- | --- | --- | --- | --- | --- |
| 3–4 nodes | 4 nodes | `acceptance_report.network_node_count` | `projection.node_count` + captured Scenario；simulation result/configuration | Result → Acceptance evidence → Network nodes | `acceptance_run_report.hpp`; `platform_assembly_acceptance_run_report_test`; `platform_ns3_acceptance_scenario_integration_test` | `Extended6Node` 是工程扩展，不属于第三方基线 |
| 60 bit/s | 60 bit/s | `acceptance_report.communication_rate` | `manifest.experiment.phy.bit_rate_bits_per_second`；captured simulation configuration | Result → Acceptance evidence → Communication rate | `acceptance_run_report.hpp`; `platform_ns3_acceptance_scenario_integration_test` | 不是硬件 modem 实测速率 |
| BER ≤ `1e-4` | max `0.0`, mean `0.0` | `acceptance_report.bit_error_rate` | `maximum_ber`、`mean_ber`、`semantics.ber_evidence_source`；**Modeled** | Result → Acceptance evidence → BER | `platform_phy_scalar_ber_rx_phy_test`; `platform_assembly_acceptance_run_report_test` | Bellhop-derived propagation + in-band noise + simplified BPSK/AWGN；高 SNR 下 `0.0` 可能达到浮点数值表示下限，不是硬件 BER |
| feature-level fusion | `AcceptanceBearingFusion`; formal `FusionResult` | `acceptance_report.feature_level_fusion` | captured Experiment + `fusion_results`；application simulation result | Result → Acceptance evidence → Feature-level fusion | `platform_assembly_acceptance_feature_test`; `platform_ns3_acceptance_fusion_integration_test` | 输入是已交付特征，不是 raw waveform/ADC |
| bearing observations ≥ 5 | 6 bearing observations | `acceptance_report.bearing_point_count` | `minimum_bearing_points` + `fusion_results[].observation_count` | Result → Acceptance evidence → Bearing points | `platform_assembly_acceptance_feature_test`; `platform_ns3_acceptance_fusion_integration_test` | 5 个方位观测不等于 5 个节点 |
| fusion period ≤ 180 s | 24 s | `acceptance_report.fusion_period` | `maximum_fusion_period_ns` + `fusion_results[].fusion_period_ns` | Result → Acceptance evidence → Fusion period | `platform_assembly_acceptance_run_report_test`; `platform_ns3_acceptance_fusion_integration_test` | 24 s 是当前 baseline-specific expected value |

## 演示参数（不是新增硬指标）

| Parameter | Frozen demonstration value | Evidence classification |
| --- | --- | --- |
| Network | 4 nodes：3 moving sensors + 1 fixed fusion center | Simulation configuration |
| Acoustic center frequency | 25 kHz | Simulation configuration |
| Source level | **110 dB re 1 µPa @ 1 m** | Simulation configuration only；hardware calibration TBD |
| TDMA guard | 2 s | Simulation configuration |
| Network refresh | every 10 cycles | Simulation configuration |
| Environment | `ReferenceShallowWaterV1` | Reference/proxy modeled environment |
| Nominal sensor speed | approximately 5 km/h | Simulation configuration |
| Initial average horizontal range | approximately 1 km | Simulation configuration |

## Evidence hierarchy

1. Level 1 — 第三方验收要求。
2. Level 2 — 正式 Release identity。
3. Level 3 — Run captured configuration。
4. Level 4 — simulation/result evidence。
5. Level 5 — UI presentation。

Level 5 不是最终权威。`AcceptanceEvidenceBundle` 和 backend formal Result 才是数据证据。
Run lifecycle `Completed` 与 Acceptance verdict `Pass`/`Fail` 是两个维度；一个正常完成的
Run 可以得到 `Fail`，这不表示进程崩溃或 release failure。

机器可读映射见 `acceptance_evidence_matrix.json`。
