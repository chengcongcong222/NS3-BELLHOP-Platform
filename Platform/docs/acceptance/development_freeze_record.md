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

以上表格记录首次冻结时的安装路径。该次更新前先备份共享数据并确认空闲，未覆盖旧工作树或停其他历史服务。首次启动配置为 `Platform/.runtime/closeout-main.sh`；永久证据在 [evidence/p0-alg02-closeout](evidence/p0-alg02-closeout/)。当前入口和整理后的路径以下节为准。

记录提交只补充验证证据，不改变受测程序。[用户路径记录](user_workflow_validation.md) 与 [非阻断限制](known_issues.md) 是冻结边界。后续转为验收文档、标准图表、操作手册及演示材料；开发仅保留阻断缺陷修补。

## 本机典型系统整理节点

整理基线为 `4e22bf0a4d3d82be7c3e9af1e600618e1dc0c9a2`。本次只整理启动入口、典型数据、历史运行目录与构建缓存，不修改仿真算法、前后端业务逻辑或 Runtime 事件。

当前项目：`/home/ccc/work/NS3-BELLHOP-Platform-algorithm-integration-v2`。运行根目录的 `./start-platform.sh`，打开 **http://127.0.0.1:4182**；后台仍为 8012。重复启动只报告已运行，不另开端口。

| 当前配置 | 路径或身份 |
| --- | --- |
| 启动配置 | `Platform/.runtime/current-system.env` |
| 保留的程序与数据 | `Platform/.runtime/retained-system/` |
| 作者工作区 | `retained-system/data/workspace` |
| 环境资产 | `retained-system/data/environments` |
| 算法安装 | `retained-system/algorithms`，仅当前启用版本，无待启用版本 |
| 算法开发配置 | `Platform/.runtime/protocolctl.json` 已指向上述安装；源码包未删除 |
| 前端 | 原 `dist`、当前唯一前端依赖及 Node；没有重做生产构建 |
| 后台业务源码 | `63909d3` 所含业务源码，未变化 |
| Worker 和前端 JS | SHA256 与首次冻结表格相同 |
| 运行日志 | `retained-system/service/logs` |
| 典型数据恢复包 | [release/typical-system](../../release/typical-system/README.md)，约 10 MiB，31 个原始数据文件，逐文件 SHA256 |

数据保留两份正式环境、一个三节点自建场景与双流实验及其轨迹，另有两个内置预置案例。完整到达文件、声线和输入快照一并保留；不是只留下页面截图。原工作树的源代码、Git 管理库、只读旧系统 `NS3_Factory` 和原始压缩包不动。

### 验证结果

| 检查 | 本轮结果 |
| --- | --- |
| 同一入口浏览器操作 | 首页、案例、原草稿、保存运行、模式切换、结果弹层、环境声线均实际经过页面；自动化操作，不是人工签字 |
| 四节点预置案例 | `run-6c27536ecf7a4f94b09b07f8ee103dd8`；Completed，6 次发送、6 次交付、1 次融合 |
| 自建模式 A | `run-7154b304cfaf447b89104cec28a5a78f`；Completed，8 次发送、8 次交付 |
| 同场景模式 B | `run-dc86d14391ed413a91455e4409520945`；Completed，8 次发送、0 次交付；512 个比较比特、152 个错误比特、16 次 CRC 失败（含旁听），未假成功 |
| 声线路径 | 已发布导入环境可选 5 个发射深度，显示 31 条真实声线 |
| 前端 | 锁定 Vitest 4.1.11：resources、runActions、environmentTruth 共 31 项通过 |
| 后台 | 安装生命周期、轨迹 API/C++ 逐点一致性共 5 项通过 |
| 恢复工具 | 5 项通过，含完整性、拒绝覆盖、损坏输入、路径边界 |
| C++ ON | Bellhop Runtime 与 Acceptance 场景/融合 3 项通过 |
| C++ OFF | 轨迹资产/UUV 2 项通过 |

曾误从仓库根目录调用 npm exec，使用了非锁定测试器且未载入 DOM 配置；该次运行无效，已改为从前端目录直接执行锁定版本并通过。误生成的缓存已移入隔离区，未进入源码节点。

### 归档与恢复边界

归档位置：`D:\ProjectStorage\NS3-BELLHOP-checkpoint-20261010`。

| 文件或目录 | 内容 |
| --- | --- |
| `builds-before-cleanup.tar.gz` | 清理前构建、旧安装、旧备份完整归档；SHA256 `4586e3db893cacf0f8f03940667ca112daf741bbcc2d9d6479868a707a86a249` |
| `project-history-before-cleanup.tar.gz` | 旧项目源码、用户环境/草稿/轨迹和缓存；SHA256 `e75967a3e16c08fe92eb1fa7a297a10b8d12dd95647e19a634319a84e8c9ab49` |
| `service-records/8007` 至 `8012` | 旧服务的运行目录、结果、定义及事件；8012 的 `runs-before-switch.json` 保留更新前索引 |
| `current-programs-and-data.tar.gz` | 当前已受测的运行程序、典型活动数据、Node 与前端依赖、主入口配置；排除服务 PID 和活动日志，不用于覆盖运行中的目录 |
| `quarantine/` | 可直接找回的原目录；不是永久删除。搬移映射见 `quarantine-moves.jsonl` 和 `quarantine-project-moves.jsonl` |

两个压缩归档在清理前均完成 `tar --compare`，退出码 0。批量永久删除被安全检查拦截后，改为可恢复跨盘搬移；不把“有压缩包”当作直接永久删除整个项目的理由。

历史运行目录当前为内存数据，不能自动恢复历史 Run URL。已导出旧端口的 23 次运行和原主入口的 4 次运行；本次保留系统已重新生成可查看结果。原始用户资源在 D 盘归档中，不会因为精简展示而被销毁。

本轮操作截图和正式结果见 [典型系统验证证据](evidence/typical-system-20261010/)。自动化只负责页面操作；结果与事件仍来自正式后台和 Worker。

### Windows C 盘限制

本发行版 `Ubuntu-24.04` 的虚拟磁盘在 C 盘，不是另外两套 D 盘发行版。移走文件释放的是 Linux 内部空间；当前 VHDX 未启用稀疏回收，不能把搬移体积直接算成 C 盘已回收体积。整理期间也观察到 Windows C 盘页面文件约 8.7 GiB，但缺少其起始值，不能断言 C 盘余量变化均由它造成。

离线压缩必须在虚拟磁盘不以读写方式挂载时进行，参见 [Microsoft compact vdisk](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/compact-vdisk)。本轮未擅自关闭整个 WSL、修改页面文件或在线操作 VHDX。需要另行安排短暂停机，确认当前结果已导出、IDE 文件已保存后，再回收 Windows 的实际占用。
