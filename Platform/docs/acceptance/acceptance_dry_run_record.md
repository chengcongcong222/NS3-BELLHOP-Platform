# P0-S5-04 Acceptance Dry-run Record

## Identity and method

- Release: `P0-S5-03`
- Source revision: `a44a2b3a4cd88c37445c800193114c5f00841ded`
- Archive SHA-256: `318c26b421be211b981a349394971c6b28e3fbe6730c4ee8ca44218c5df0955c`
- Procedure: `acceptance_demo_script.md`, steps 1–14
- Workspace: a fresh extraction unrelated to the source tree; workspace path is not product identity
- Dependency mode: bundled Python wheels with `PIP_NO_INDEX=1`; ns-3.47 supplied through the documented external prefix
- RunId: intentionally excluded from Golden identity

## Step result

| Step group | Result | Observed automated time |
| --- | --- | ---: |
| 1 archive checksum | PASS | 7 ms |
| 2 fresh extraction | PASS | 53 ms |
| 3 offline prepare/preflight | PASS | 4,793 ms |
| 4 start/readiness | PASS | 855 ms |
| 5–13 public UI routes/resources, Run, Result, Evidence, system info | PASS | 189 ms |
| 14 stop and no-listener check | PASS | 325 ms |
| **Total automated dry-run** | **PASS** | **6,222 ms** |

人工讲解和页面停留时间不计入上述计时；现场脚本按 5–10 分钟安排。整个流程未读取源码、未手工
修改 release 文件，也未使用 mock。真实 Run 达到 `Completed`，Acceptance verdict 为 `Pass`，输出
4 nodes、6 transmissions、18 Signal、18 Reception、6 local deliveries。

## Ambiguity/wait audit

- prepare 的主要等待是创建 release-local Python environment，输出能明确显示 bundled wheelhouse。
- start 在 backend 与 frontend 都 ready 后才输出 `RELEASE_READY`。
- Run 在本机很快完成；现场仍应等待 lifecycle `Completed` 后再进入 Result。
- 未发现需要阅读源码才能继续的步骤。

## Operator error rehearsal

| Error | Stable observation | Approved recovery |
| --- | --- | --- |
| Missing `PLATFORM_NS3_PREFIX` | `RELEASE_PREFLIGHT_NS3_PREFIX_REQUIRED` | 设置到 ns-3.47 prefix，重新 prepare/preflight |
| Port 8000 occupied | `RELEASE_PREFLIGHT_PORT_IN_USE` | 释放端口或使用正式端口变量，重新 preflight；不绕过 |
| Reference asset byte changed | `RELEASE_INTEGRITY_FAILED` | 丢弃该解压目录，从已验证 archive 重新解压；不重算 checksum |
| Frontend opened before backend ready | launcher status 为 `RELEASE_NOT_RUNNING`，frontend connection refused | 执行 start，等待 `RELEASE_READY` 后刷新 |
