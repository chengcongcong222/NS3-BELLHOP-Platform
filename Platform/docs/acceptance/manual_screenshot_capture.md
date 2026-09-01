# Manual Screenshot Capture — P0-S5-05

自动能力审计未发现系统 Chromium/Chrome/Firefox 或既有项目截图工具。不得使用 mock、fixture、
静态 HTML 或生成图片冒充正式运行截图；因此本阶段保留真实人工捕获流程，不向生产 frontend
引入 Playwright/Puppeteer。

## Preconditions

1. 使用已验证的 P0-S5-05 canonical archive，并核对相邻 SHA-256 sidecar。
2. `prepare`、`start` 完成且输出 `RELEASE_READY`。
3. 执行真实 `Acceptance4Node`，等待 lifecycle `Completed`、Result/Evidence 可用。
4. 浏览器窗口固定尺寸，隐藏书签/账户信息；不得修改页面数字、verdict 或补画事件。

## Capture order and filenames

| Order | Filename | Route/state | Required visible content |
| ---: | --- | --- | --- |
| 1 | `01-overview.png` | `/`, Golden Run completed | resource counts、latest Run/Result |
| 2 | `02-environment-detail.png` | `/environments/reference-shallow-water-v1` | AssetId/checksum、25 kHz、coverage、provenance |
| 3 | `03-scenario-topology.png` | `/scenarios/acceptance4-scenario/versions/1` | 4 nodes、3 moving sensors、fixed fusion center |
| 4 | `04-experiment-detail.png` | `/experiments/acceptance4-experiment/versions/1` | 60 bit/s、25 kHz、110 dB re 1 µPa @ 1 m、guard、seed |
| 5 | `05-run-monitor.png` | `/runs/{run_id}`, terminal | Completed、Transmission/Signal/Reception/CycleCommit |
| 6 | `06-event-timeline.png` | same Run | sequence、simulation time、event kind、identity |
| 7 | `07-result-summary.png` | `/results/{run_id}` | overall、4 nodes、60 bit/s、bearing、period |
| 8 | `08-acceptance-table.png` | Result Acceptance table | Requirement `3–4 nodes`、Actual `4 nodes` and all six rows |
| 9 | `09-fusion-result.png` | Result Fusion section | 6 observations、24 s、formal FusionResult |
| 10 | `10-evidence-info.png` | Result evidence section | Modeled BER wording、Reference/Bellhop semantics、download |
| 11 | `11-system-info.png` | `/system/info` | P0-S5-05、source SHA、ns-3 authorities、M1 gateway、asset identity |

截图后逐张对照 `screenshot_manifest.md`。实际图片只能来自同一真实 Release/Run，并作为 handoff
包的可选 `screenshots/` 内容；缺少可靠捕获时保持目录缺席并明确状态，不伪造。
