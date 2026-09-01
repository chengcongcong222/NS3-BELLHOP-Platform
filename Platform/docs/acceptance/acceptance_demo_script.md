# P0-S5-04 Acceptance Demonstration Script

目标时长 5–10 分钟。操作对象是正式 P0-S5-05 Linux x86_64 archive。该交付包在满足
ns-3.47 运行前提后，可在离线环境完成准备、启动、仿真、结果查看与验收证据导出。

## 1. Verify archive SHA-256

- **Operator action:** `sha256sum -c ns3-bellhop-platform-p0-s5-05-linux-x86_64.tar.gz.sha256`。
- **Expected screen:** archive `OK`。
- **What to say:** 正式 identity 是 release ID、source revision、archive filename 和 checksum；artifact publication 是后续交付动作，当前不声称已发布 GitHub Release。
- **Evidence to point at:** sidecar checksum 与 `MANIFEST.json`。
- **Possible expert question:** “包来自哪里？” **Approved answer:** 当前验收使用经校验的 canonical artifact；对外发布位置由后续 artifact publication 确定。

## 2. Prepare and preflight

- **Operator action:** 解压，设置 `PLATFORM_NS3_PREFIX=/path/to/ns3-3.47`，执行 `./release.sh prepare`。
- **Expected screen:** `RELEASE_INTEGRITY_OK`、`RELEASE_PREFLIGHT_OK`。
- **What to say:** Python wheelhouse 已锁定并离线供应；ns-3.47 是明确 external prerequisite。
- **Evidence to point at:** `README.md`、`MANIFEST.json`、preflight output。
- **Possible expert question:** “会联网安装吗？” **Approved answer:** 不会；prepare 使用 bundled wheels 和 `--no-index`，也不会下载 ns-3。

## 3. Start

- **Operator action:** `./release.sh start`。
- **Expected screen:** `RELEASE_READY http://127.0.0.1:4173`。
- **What to say:** 同一 launcher 管理 backend/frontend 生命周期。
- **Evidence to point at:** ready output；必要时执行 `./release.sh status`。
- **Possible expert question:** “端口冲突怎么办？” **Approved answer:** preflight 明确拒绝 8000/4173 冲突；释放端口后重新执行，不绕过检查。

## 4. Open Overview

- **Operator action:** 打开 `http://127.0.0.1:4173/`。
- **Expected screen:** Overview resource/run summary。
- **What to say:** UI 是只读操作与展示层，不是 verdict 权威。
- **Evidence to point at:** Overview cards and recent resources。
- **Possible expert question:** “页面数据谁提供？” **Approved answer:** FastAPI 返回正式资源和 Run DTO；UI 不重算验收结论。

## 5. Inspect Environment

- **Operator action:** Environment → `ReferenceShallowWaterV1`。
- **Expected screen:** AssetId、checksum、25 kHz、coverage、validation/provenance。
- **What to say:** WOA23/GEBCO 2020 构建的 Reference/proxy modeled environment；声场是 Bellhop-derived。
- **Evidence to point at:** environment detail and validation fields。
- **Possible expert question:** “Bellhop 正在实时执行吗？” **Approved answer:** 否；运行时只查询预计算 immutable AcousticFieldAsset。

## 6. Inspect Scenario

- **Operator action:** Scenario → `acceptance4-scenario` v1。
- **Expected screen:** 3 moving sensors + 1 fixed fusion center。
- **What to say:** 4 节点处于第三方 3–4 nodes 范围。
- **Evidence to point at:** topology and node capability fields。
- **Possible expert question:** “为何不是 5 个节点？” **Approved answer:** 方位阈值是至少 5 条 observation，不是至少 5 个节点。

## 7. Inspect Experiment

- **Operator action:** Experiment → `acceptance4-experiment` v1。
- **Expected screen:** 60 bit/s、25 kHz、110 dB re 1 µPa @ 1 m、2 s guard、seed/config。
- **What to say:** 60 bit/s 是硬指标；其余列为演示配置。110 dB 仅是 simulation configuration。
- **Evidence to point at:** PHY / Execution and Protocol panels。
- **Possible expert question:** “110 dB 实测过吗？” **Approved answer:** 没有；hardware calibration 是 TBD。

## 8. Start Run

- **Operator action:** 选择 **Run this experiment**。
- **Expected screen:** 新 RunId，lifecycle 从 Created/Running 推进。
- **What to say:** RunId 每次不同；Golden identity 使用 release/resource/seed identities，不使用 RunId。
- **Evidence to point at:** captured Experiment、Scenario、Environment identities。
- **Possible expert question:** “能否并发多 Run？” **Approved answer:** 当前 P0 是 single-active-run；多 Run scheduler 不在本阶段。

## 9. Observe Run Monitor

- **Operator action:** 查看 Monitor 和 Event timeline。
- **Expected screen:** Transmission、Channel Signal、Reception、CycleCommit 按 sequence 出现。
- **What to say:** ns3::Simulator 是时钟与事件调度权威，M1/Ns3KernelGateway 是 Platform gateway；SSE 只读。
- **Evidence to point at:** event sequence、simulation timestamp、signal lifecycle。
- **Possible expert question:** “为什么说基于 ns-3？” **Approved answer:** Worker 的真实执行链调用 ns3::Simulator 的 Now/Schedule/Run/Stop/Destroy，并由 kernel/integration tests 验证。

## 10. Open Result

- **Operator action:** lifecycle `Completed` 后打开 Result。
- **Expected screen:** formal projection、FusionResult 和 overall。
- **What to say:** Completed 表示系统执行完成；Acceptance Pass/Fail 是独立结果维度。
- **Evidence to point at:** lifecycle、formal Result、AcceptanceReport。
- **Possible expert question:** “Fail 是否代表软件失败？” **Approved answer:** 否；正常 Completed Run 可以得到 Acceptance Fail。

## 11. Review Acceptance table

- **Operator action:** 逐行查看 6 项硬指标。
- **Expected screen:** Requirement、Actual、Verdict、Evidence/source、Reason 完整显示。
- **What to say:** 当前 Golden baseline 为 4 nodes、60 bit/s、Modeled BER、feature-level fusion、6 observations、24 s period。
- **Evidence to point at:** backend AcceptanceReport fields。
- **Possible expert question:** “BER 0.0 是绝对无误码吗？” **Approved answer:** 不是；它是仿真模型数值，在高 SNR 下可能达到浮点数值表示下限。

## 12. Download Evidence

- **Operator action:** 下载 Acceptance Evidence text；需要时访问 JSON endpoint。
- **Expected screen:** evidence file 含 release、source SHA、resource identities、result/verdict。
- **What to say:** Evidence 是 immutable captured snapshot，不从 UI 数字重新计算。
- **Evidence to point at:** `/runs/{run_id}/acceptance-evidence` 与 `.txt`。
- **Possible expert question:** “如何追溯环境？” **Approved answer:** Evidence 内含 AssetId/checksum，并可关联 release manifest 与 provenance。

## 13. Show system/ns-3 info

- **Operator action:** 访问 `http://127.0.0.1:8000/system/info`。
- **Expected screen:** P0-S5-05、source SHA、linux-x86_64、ns-3 3.47、两个 `ns3::Simulator` authority、M1 gateway、Reference identity。
- **What to say:** external prerequisite 不等于未使用 ns-3；实际 Worker 链接并执行 ns-3.47。
- **Evidence to point at:** system/info and `binary_dependencies.json`。
- **Possible expert question:** “为何不修改 ns-3 源码？” **Approved answer:** Platform 通过稳定 gateway 组合领域模块，保留 ns-3 内核权威并避免维护 fork。

## 14. Stop

- **Operator action:** `./release.sh stop`，随后 `./release.sh status` 应显示 not running。
- **Expected screen:** `RELEASE_STOPPED`；8000/4173 不再监听。
- **What to say:** launcher 等待受控进程退出并验证端口释放。
- **Evidence to point at:** stop/status output。
- **Possible expert question:** “是否安装系统服务？” **Approved answer:** 否；P0 是 extract/prepare/start 模式，不写 systemd 或系统目录。

## Recovery card

- 未设置 `PLATFORM_NS3_PREFIX`：设置指向 ns-3.47 prefix 后重跑 prepare/preflight。
- 端口占用：停止占用 8000/4173 的进程，或按正式环境变量选择空闲端口，再重跑 preflight。
- Asset checksum 不一致：停止使用该解压目录，从已验证 archive 重新解压；不得重算 checksum 绕过。
- Frontend 已打开但 backend 未 ready：不要开始 Run；执行 `./release.sh status`，必要时 stop 后重新 start，等待 `RELEASE_READY` 再刷新。
