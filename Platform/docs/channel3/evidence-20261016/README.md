# 浏览器与数值证据

主入口 `http://127.0.0.1:4182`，2026-10-16；Chromium 140 无头浏览器自动化，1600×1000。页面操作后以只读 API 核对，不是人工签字。

| 图片 | 内容 |
| --- | --- |
| [09-final-c-editor](09-final-c-editor.png) | C 录音、固定连续噪声、OSDM 与精简布局；重开保存值 |
| [10-c-coverage-rejection](10-c-coverage-rejection.png) | 超出录音时长的输入被拒绝，可就地修改 |
| [11-final-c-runtime](11-final-c-runtime.png) | C 链路示意、非标定功率比、BER、CRC、真实日志 |
| [12-final-c-result](12-final-c-result.png) | C 结果、出处与实验假设 |
| [13-final-c-waveform](13-final-c-waveform.png) | 19/64 错误比特、CRC 0/2；不伪装成功交付 |
| [14-final-b-osdm-editor](14-final-b-osdm-editor.png) | B 普通实验保存重开后的 OSDM 参数 |
| [15-final-b-runtime](15-final-b-runtime.png) | B OSDM 目标交付与波形统计 |
| [16-final-a-runtime](16-final-a-runtime.png) | 原 A 四节点案例保持运行与融合 |

`final-main-runs.json` 为最终部署的完整冻结配置、事件与结果；`pre-ui-polish-runs.json` 为 UI 收口前的同程序结果。C 的 repeat 与 UI 更新前后、B/A 更新前后的 trace 哈希一致。

`ctest-on.log` / `ctest-off.log` 为最终回归原始输出；`numerical-checks.json` 保存锁定官方对照数值。参见 [六阶段收口](../closeout.md) 的范围、版本与限制。
