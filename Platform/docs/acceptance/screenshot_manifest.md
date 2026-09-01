# Acceptance Screenshot Manifest

当前环境没有可用的系统浏览器/正式截图工具，本阶段不向产品引入大型浏览器依赖。截图必须使用同一 P0-S5-05
Golden demonstration state，并避免展示开发路径、浏览器个人信息或临时 RunId 作为基线 identity。

| ID | Route | Screen purpose | Required visible fields | Capture state |
| --- | --- | --- | --- | --- |
| SS-01 | `/` | Overview | platform resource counts、recent Run、Result link | backend ready；Golden Run completed |
| SS-02 | `/environments/reference-shallow-water-v1` | Environment Detail | AssetId、checksum、25 kHz、coverage、validation/provenance classification | catalog loaded |
| SS-03 | `/scenarios/acceptance4-scenario/versions/1` | Scenario topology | 4 nodes、3 moving sensors、fixed fusion center、environment link | immutable resource view |
| SS-04 | `/experiments/acceptance4-experiment/versions/1` | Experiment Detail | Acceptance4Node、60 bit/s、25 kHz、110 dB re 1 µPa @ 1 m、guard、seed | before Start Run |
| SS-05 | `/runs/{run_id}` | Run Monitor | Completed lifecycle、event sequence、Transmission/Signal/Reception/CycleCommit | terminal event stream complete |
| SS-06 | `/runs/{run_id}` | Event timeline | stable sequence、simulation time、event kind、identity、channel evidence | terminal event stream complete |
| SS-07 | `/results/{run_id}` | Result summary | overall、node count、rate、duration、bearing points、fusion period | Golden Result available |
| SS-08 | `/results/{run_id}` | Acceptance table | six requirement/actual/verdict/evidence/reason rows | Acceptance4Node evidence loaded |
| SS-09 | `/results/{run_id}` | Fusion result | FusionResult plot/table、6 observations、24 s period | Golden Result available |
| SS-10 | `/results/{run_id}` | Evidence download/info | environment/propagation/BER semantics、download action、source revision | evidence loaded |
| SS-11 | `/system/info` | System info | P0-S5-05、source SHA、linux-x86_64、ns-3 3.47、authorities、M1 gateway、AssetId/checksum | backend ready |

截图只用于 Level 5 UI presentation。正式字段证据仍来自 backend Result/AcceptanceEvidence。
