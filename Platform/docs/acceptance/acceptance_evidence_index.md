# Acceptance Evidence Index

| Question | Primary evidence | Supporting evidence |
| --- | --- | --- |
| 哪个软件版本？ | release `MANIFEST.json`: `release_id`, `source_revision`, `build_target` | `/system/info`; archive `.sha256` |
| Archive 是否完整？ | archive sidecar SHA-256 `318c26b421be211b981a349394971c6b28e3fbe6730c4ee8ca44218c5df0955c` | release `SHA256SUMS`; `release_inventory.json` |
| 验收阈值是什么？ | `Platform/acceptance/acceptance4_baseline_v1.json` | `acceptance_evidence_matrix.json` |
| Run 实际用了什么？ | `AcceptanceEvidence.manifest` | Run detail、Experiment/Scenario/Environment resource APIs |
| 六项 verdict 从哪里来？ | `AcceptanceEvidence.acceptance_report` | formal Result `acceptance_report`; `acceptance_run_report.hpp` tests |
| 环境是什么？ | `manifest.environment` AssetId/checksum | `golden_metadata.json`, `validation_report.json`, `source/PROVENANCE.md` |
| Bellhop 在哪里？ | `semantics.propagation_evidence=Bellhop-derived` | Reference asset `.arr` → parser/normalizer → canonical asset tests |
| BER 性质是什么？ | `semantics.ber_evidence_source=Modeled` 和 `ber_interpretation` | scalar BER PHY test、Acceptance report test |
| ns-3 是否真实执行？ | `/system/info` authorities + Worker ELF dependency | kernel gateway、kernel/dispatcher/signal lifecycle/Acceptance ns-3 ON tests |
| 结果是否可重复？ | normalized hashes in `acceptance_evidence_matrix.json` | three Golden Run comparison and two-build archive SHA equality |
| 演示是否完整走通？ | `acceptance_dry_run_record.md` | 14-step script and operator error rehearsal |
| 如何下载证据？ | `/runs/{run_id}/acceptance-evidence` | `/runs/{run_id}/acceptance-evidence.txt` and Result download action |

## Repository evidence targets

- [`acceptance4_baseline_v1.json`](../../acceptance/acceptance4_baseline_v1.json)
- [`acceptance_run_report.hpp`](../../assembly/internal/acceptance_run_report.hpp)
- [`evidence.py`](../../backend/src/ns3_factory_backend/evidence.py)
- [`system_info.py`](../../backend/src/ns3_factory_backend/system_info.py)
- [`ns3_kernel_gateway.hpp`](../../kernel/internal/ns3_kernel_gateway.hpp)
- [`golden_metadata.json`](../../environment/assets/reference_shallow_water_v1/golden_metadata.json)
- [`validation_report.json`](../../environment/assets/reference_shallow_water_v1/validation_report.json)
- [`PROVENANCE.md`](../../environment/assets/reference_shallow_water_v1/source/PROVENANCE.md)
- [`pages.tsx`](../../frontend/src/features/pages.tsx)

## Golden normalized identities

- Result: `ba69baecf2b03dc3778ace39e9cdfbc0acbbfa93bf71f966e59c8604a6e27e59`
- Fusion: `7708e79ee2a9e9b821f347bb8fdf693c0e6573f3f3300acfd2c1a16917a25fd6`
- Trace ordering: `6968f4c31bf652761870484e85b1c5345e6c4708f4eef437834a5b3f1048e671`
- Acceptance report: `4a1b889db48a9fafcced8fb27b9c82e99803279e723e08511fd9d7c5ed1cb598`

Artifact publication 属于后续交付动作；本索引不声称 GitHub Release 已发布。
