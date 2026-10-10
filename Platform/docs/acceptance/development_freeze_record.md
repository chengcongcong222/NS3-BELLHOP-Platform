# P0-ALG-02 开发冻结记录

**CURRENT_SYSTEM_DEVELOPMENT_FROZEN**（2026-10-10）。

| 项目 | 当前实证 |
| --- | --- |
| 基线 | `d72b3a8bda9eba69dd199eec0508e45f1c3af6a7` |
| 安装修复/安装侧受测源码 | `dbce6c4849fd8197dbcfac0108e312aebcedb094` |
| 最终受测源码 | `63909d331e32a7d2c9785fc92837632ade9c968c`；追加的唯一代码修复为导入环境来源显示 |
| 分支 | `feature/algorithm-integration-v2` |
| 已启用且受测 Worker SHA256 | `dd21c42db0468afce60d378074be5377de7f2f1c87f0768efbeabc2bdb0174c7` |
| 安装根目录 | `/home/ccc/build/alg02-installations` |
| 活动安装版本 | 与受测 Worker SHA 相同；旧版本保留 |
| 待启用版本 | 无；成功启用后消费了匹配指针 |
| 候选包 | 内置 adaptive-tdma、user.integration_probe、user.integration_multifile、example.flow-round-robin、user.closeout_probe；精确版本/摘要见不可变 manifest |
| 受测主入口 | `http://127.0.0.1:4182`，后台 `http://127.0.0.1:8012`；仍在运行 |
| 前端 | `index-BW1LdsaR.js` / `index-3FGxZ8H2.css`；原受测构建未重做 |
| 前端 JS SHA256 | `f32677e8050a3fbaae3eedf30fcc10165acfc82a504d70ba19a739126ae47aae` |
| 前端 CSS SHA256 | `1748d8d59b4fc69666625f65672bf609f95a43db68d2dee73abdddcc2b468e40` |
| 受测环境 | `reference-shallow-water-v1` v1，checksum `fb64e543f9042c52`；新建发布 `1010-environment-ea3bb4c15c18-r2` v1，精确输入及资产说明见证据目录 |
| 受测实验与场景 | `experiment-7c33c1817ed9`、`scenario-327eff7149f9`；每次精确冻结版本见各 run 的 definition-context.json |
| 受测轨迹 | `trajectory-205a8739-2cad-461d-b4fa-e9896b34a4bd` v1，N0/N1/N2；场景起点派生的三份资源 ID/摘要见结果 provenance |
| 执行方式 | Codex 复用现有 Chromium/CDP 完成页面操作；API/日志只读核验；CLI 安装和重放；没有人工验收签字 |

Worker manifest 的构建源码为安装修复提交 dbce6c4；最终源码只增加后台来源显示修复，C++ 和前端文件未变化。后台身份接口记录 63909d3。受测 release_id 保留 `P0-ALG-02-CLOSEOUT-CANDIDATE`，不为了改名重新构建或重启。

先备份共享数据并确认空闲，再正常更新同一入口；未覆盖旧工作树或停其他历史服务。主入口启动配置：`Platform/.runtime/closeout-main.sh`，日志/操作截图在 `.runtime/closeout-main/`；永久证据在 [evidence/p0-alg02-closeout](evidence/p0-alg02-closeout/)。

记录提交只补充验证证据，不改变受测程序。[用户路径记录](user_workflow_validation.md) 与 [非阻断限制](known_issues.md) 是冻结边界。后续转为验收文档、标准图表、操作手册及演示材料；开发仅保留阻断缺陷修补。
