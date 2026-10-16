# OSDM / 官方 UWA 录音集成：六阶段收口

日期：2026-10-16。执行方式：源码测试 + Chromium 140 无头浏览器自动化；不是人工验收签字。
范围：模式 B 的训练式 OSDM，以及模式 C 的单向、双端点官方录音回放。未扩展阵列合并或新网络协议。

## 六阶段结果

| 阶段 | 落实内容 | 验证结果 |
| --- | --- | --- |
| 1 科学与接口基线 | ADR-0011；固定 UWA 代码、数据、依赖和许可证；分清阵元、试次、归一化幅度和建模时延 | 锁定文件 `adapters/uwa-reference.lock.json`；不把录音相对延迟当真实传播距离 |
| 2 OSDM 正式接入 | B 模式增加调制选项及 6 项分块/同步参数；规划使用含训练、CRC、补齐、CP 的实际时长；同步失败正常返回未解码 | 通用 authored worker、无噪声/AWGN/Wenz、强噪声及原算法回归通过；页面保存重开和运行通过 |
| 3 官方等价回放 | 原生 C++ 时变卷积、相位、延迟漂移、非整数时钟重采样；官方离线连续噪声，运行时按时间切片 | 5 组合成数值分支 + 真实 Blue 第 1 阵元；相对 L2 最大 2.50e-13，小于项目门槛 1e-6；无拟合对齐 |
| 4 资源与执行链 | 不可变录音/噪声资源、精确版本、正式静止轨迹、authored v4、run-owned provider；原通用 ns-3 执行链 | 无噪声/连续噪声两组真实 C E2E；重复一致；版本、覆盖、损坏反例拒绝；不访问 Bellhop 作为 fallback |
| 5 前端与操作 | 同一工作台切换 C；录音与噪声选择、OSDM 配置、链路示意、同一运行/结果页；原实验重开 | 页面实际保存、运行、回放、查看结果、重跑、改错；275 项前端测试通过；截图逐张检查 |
| 6 回归与原位部署 | ON/OFF、原案例/算法包、性能检查；保全数据，先验证旧包可恢复，再更新原入口 | ON 124/124，OFF 82/82；所有原算法包保留；仅 4182/8012 主入口，保持运行 |

这里的“官方等价”限于锁定库作者参考实现及已测数据/分支，不是国际标准认证，不能外推为所有海域、所有调制配置均可成功解码。

## 版本与部署身份

- 基线：`7468e59df88348c304a0eb3f03ecc0be67d58b0b`。
- 普通合入队友分支 `feature/channel12-comparison-experiment` 的 `34ca091242e85a0a5cafffc071358c6c93309be6`，包含 C 原始功能；未覆盖当前通用执行及 ALG-02 接口。
- 核心/后台受测源码：`23b27dcbf42cc6748fe6170810db72e7c9c36b44`。
- 最终前端受测源码：`1155584cce6eed1aa43daa5b7523c9cf82ab8268`。相对核心提交只改前端、相关测试和源数据锁定清单；没有重新生成未经测试的仿真程序。
- 安装程序 SHA256：`19f4932c0f4e59e8c4d253b619a732bcb757b18962647727d3edee3f694bcb13`。manifest 记录源提交 23b27dc、clean source、依赖校验和及全部 5 个算法。
- 前端：`index-DGyfKJdY.js`，SHA256 `652b9d647deed3c1fa033604c7514b5211288f8f0246bcb1909ba524a30f4dcc`；CSS `index-D76JwGSo.css`，SHA256 `670e657fbb525d0b982c1a5fbc91de12cc3ebed1b0a5e20630890c1256459249`。
- 主入口：`http://127.0.0.1:4182`；API：`http://127.0.0.1:8012`；原脚本 `./start-platform.sh`。
- 本机配置：`Platform/.runtime/current-system.env`。后台固定于 `channel3-system-23b27dc/Platform/backend/src`，前端固定于 `channel3-frontend-DGyfKJdY`；用户数据继续使用原 `retained-system/data`。
- 发布标识：`OSDM-UWA-INTEGRATION-20261016-UI1155584`。`/system/info` 的 source_revision 是实际后台源码，frontend_release 是实际前端产物，算法程序身份另由安装 manifest 固定。

保留的算法：`platform.adaptive-tdma`、`user.integration_multifile`、`user.integration_probe`、`example.flow-round-robin`、`user.closeout_probe`。没有用只含内置算法的测试 worker 替换用户累积安装版本。

## 真实页面操作与结果

浏览器统一为 1600×1000。通过页面输入/下拉/按钮执行，API 只读用于核对和归档；没有用直接 POST 启动实验代替页面验收。

| 用户路径 | 实际执行与观察 | 证据 |
| --- | --- | --- |
| C 新建/保存 | 选择 Blue 第 1 阵元、固定连续噪声、OSDM；生成正式示意端点轨迹和场景，保存后有修订反馈 | `09-final-c-editor.png`；此前保存记录见 pre-ui-polish-runs.json |
| C 非法输入与恢复 | 录音起点 51 s、实验 10 s；明确报片段不足，不创建替代运行；改回 0 s 后可运行 | `10-c-coverage-rejection.png` |
| C 运行和结果 | 2 次发送、2 次接收、64 比较位、19 错误位、CRC 0/2、交付 0；Run 完成，解码失败如实保留 | `11-final-c-runtime.png`、`12-final-c-result.png`、`13-final-c-waveform.png` |
| C 编辑与重跑 | “修改本实验”返回原 C 草稿并保留连续噪声/参数；按原配置重跑，完整 trace 相同 | final-main-runs.json 中 e6f01f… 与 71747d…；事件 SHA256 均为 `d1ee667cf56b6c482402e5a164c5c0c3b086551d79288b1d40286734be1e5a2f` |
| B OSDM | 复用正式三节点场景；从页面配置 0→2 通信流、OSDM、4000 bit/s、无额外噪声；保存、重开、再运行 | `14-final-b-osdm-editor.png`、`15-final-b-runtime.png`；2 Tx、4 Rx、目标交付 2、128 bit 中错误 0、CRC 4/4（含旁听） |
| A 原案例 | 案例中心选择浅水四节点，运行默认实验 | `16-final-a-runtime.png`；6 Tx、18 Rx、6 目标交付、1 次融合；未改 preset |
| 播放与筛选 | C 的暂停/单步、实时跟随、结果抽屉、重跑仍使用正式事件；展示控制不重新计算 PHY | 同一 Run 的导出 trace/结果与固定配置重跑一致；C 不再显示虚构坐标和融合数值 |

收口中修复了页面实操发现的遗漏：录音 ChannelOutcome 漏计导致“结果证据不完整”、A/B 场景列表混入 C 场景、C 场景缺失时错误退回几何画布的展示路径。OSDM 高级项默认折叠、来源卡片压缩；最终 C 编辑首屏可完整操作。

最终主入口运行：

- C：`run-e6f01f1689444d2fbc243a7f3c0b07f9`；重跑 `run-71747d5aa07c43358d56107cd9075e6a`。
- B：`run-f65b88ea37894e5e86445a0c73b103dc`。
- A：`run-2301c2bc851c4c4c8bd14871089f54d6`。

以上运行的配置、资源、事件、结果全部归档在 [final-main-runs.json](evidence-20261016/final-main-runs.json)。截图入口见 [证据目录](evidence-20261016/README.md)。

## 测试与性能

- ON：124/124；OFF：82/82。分别见 `ctest-on.log` / `ctest-off.log`。
- 后台套件：342 passed、7 skipped。需外部真实录音的 opt-in 用例另行显式运行，不将 skip 当通过。
- 已安装的实际程序：`recorded_channel_e2e_test.py` 无噪声/连续噪声 2/2，通过真实 ns-3，精确重跑；缺版本/超时/损坏数据及噪声明确拒绝。
- OSDM 后台专项 11 项通过；前端 32 文件、275 测试通过；生产构建通过；`git diff --check` 通过。
- 官方对照：6 组 replay，真实 Blue 4308 样本，relative L2 `2.498432583387894e-13`；alpha=2 / 1.7 的连续噪声确定性及阵元顺序映射通过。详细值见 `numerical-checks.json`。
- 已安装程序的两个 C E2E（含各自重跑及反例）墙钟 3.39 s；`/usr/bin/time -v` 最大 RSS 62,648 KiB。这是本机小场景测量，不是所有规模的性能承诺。
- 正式导入资源：录音约 17 MiB、12 s 连续噪声约 4.8 MiB。目录只返回清单：本机查询录音 1,096 bytes / 1.40 ms、噪声 965 bytes / 1.17 ms；矩阵不进浏览器、日志和 DTO。
- Vite 仍提示单个 JS chunk >500 kB（gzip 170.81 kB）；本轮浏览器操作无阻断，未为此扩大为打包架构重构。

## 数据与回滚

更新前原主入口无运行记录、无活跃仿真。资源、安装状态、配置备份于 `Platform/.runtime/channel3-backup-20261016`，原运行程序、Bellhop、数据未删。先以旧固定后台和重建后哈希一致的旧前端在同一 4182/8012 验证首页和 ready，再切换新程序；没有第二套平台服务。

回滚须先导出需要保留的运行结果、确认没有活跃任务，再正常停止。禁止边运行边改 active 程序。

```bash
./start-platform.sh stop
python3 Platform/tools/protocolctl.py activate --release dd21c42db0468afce60d378074be5377de7f2f1c87f0768efbeabc2bdb0174c7
```

随后将 `current-system.env` 恢复为备份中的原配置，同时增加两个固定路径变量：

```text
PLATFORM_BACKEND_SOURCE_ROOT=<仓库>/Platform/.runtime/channel3-backup-20261016/Platform/backend/src
PLATFORM_FRONTEND_DIST_ROOT=<仓库>/Platform/.runtime/channel3-backup-20261016/Platform/frontend/dist
```

再执行 `./start-platform.sh`。不要把旧前端指向当前 checkout 的 dist。正常代码回滚无需覆盖用户数据；旧界面不支持新 C 草稿，不能用旧界面修改 C 资源。

## 明确保留的边界

1. C 首版是单向双端点录音回放，不是任意拓扑海域孪生：不推断反向链路、阵元组网、测量坐标或轨迹多普勒。多节点、双向、多个同时测量链路要有真实兼容数据再扩展。
2. C 仅 Direct + CommunicationOnly + 内置 AdaptiveTdma/配置式 TDMA；第三方 planning-mac-v2 的链路预测能力未在 C 开放。A/B 原算法入口保留。
3. 实测数值验收覆盖 Blue 第 1 阵元及合成分支，不等于库中其他海试全部验收。原始 MAT 大文件和本机安装程序不提交 Git；来源/哈希/导入命令可追溯。
4. 归一化幅度、建模传播时延、有限样本功率比明确标注。C 噪声来自官方统计模型的连续实现，不是原始录音背景音；选择固定噪声资源后，实验 seed 不另抽噪声。
5. 当前服务既有运行目录仍在内存中，重启后不自动恢复可浏览历史 Run。草稿、资源、冻结 definition 保留；本轮完整结果已导出 JSON。这是现有持久化边界，不把备份说成已实现历史目录恢复。
6. C 示例 CRC 未通过是该波形参数/真实信道下的结果，不是系统假成功；接收器能力提升不属于本次集成验收，后续应单独设计接收性能实验。

结论：六阶段在上述有证据的支持范围内完成；后续文档提交不改变已受测运行程序。
