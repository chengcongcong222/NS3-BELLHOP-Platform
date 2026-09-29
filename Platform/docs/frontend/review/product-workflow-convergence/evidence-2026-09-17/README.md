# 第一批施工证据 · 2026-09-17

范围：环境数据真实性、建设器恢复输入、环境目录、首页首版。不是整轮产品验收。

后续 WOA 无反馈问题修复、统一实验编辑首版及最新测试数量见 [第二批施工记录](./woa-and-studio.md)。下文保留第一批的原始测试数量和证据，不混同不同批次。

## 真实输入与计算

直接下载 NOAA WOA23 的4月1°温盐资料，再用原有声速换算实现读取；不是参考数组或 mock：

| 请求 | 实际网格 | 有效深度 | 海面声速 |
| --- | --- | --- | --- |
| 18°N / 110°E，4月，至200m | 17.5°N / 109.5°E | 0–175m，24点 | 1536.136 m/s |
| 18°N / 150°E，4月，至200m | 17.5°N / 149.5°E | 0–200m，25点 | 1539.787 m/s |

原始下载文件 SHA256：

- `woa23_decav_t04_01.nc`：`1c2ef79123d4cfd62f77705a7d44195a9bb0b369d5ab5bf5f871f824182f94f3`
- `woa23_decav_s04_01.nc`：`2e1c46417a1574a7254cb9cebf115acff3b502b62f5f957f0b85b7912b445af5`

来源：[NOAA WOA23](https://www.ncei.noaa.gov/access/world-ocean-atlas-2023/)。当前读取1°网格，相邻坐标可以返回同一网格；这不是缓存造假。参考来源清单位于 `environment/assets/reference_shallow_water_v1/source/source_manifest.json`，明确为110°E，而非旧UI错误标注的150°E。

实际请求 GEBCO 2020 / OpenTopoData：18°N / 110°E，向东10km，3点；返回正水深102、108、105m（0、5000、10000m）。不以相邻水深补陆地或缺失数据。

通过独立 Backend HTTP API 保存这组真实水文、地形及用户计算参数，运行本机 Bellhop，发布后重新读取输入：

- 环境：`4-environment-732fb792b77f-r1`
- 计算：`env-build-71c96200c294`
- `.arr` SHA256：`473bd5d1069256b2b7ca5127aeb7a9d6bfa78172808387a8c4641c5808dc3d57`
- 24个 SSP 点、3个地形点；恢复 payload 与提交 payload 完全相同。
- 使用独立 `.runtime/ui-review-workspace` / `.runtime/ui-review-environments`，没有改写旧环境和原运行记录。

另一个原生引擎测试改变输入声速1500→1540，输出声场 SHA 确实不同，两份输出均被正式 C++ 发布器接受。非法输入、修改后的参考标记、修订冲突均拒绝。

## 截图与自查

真实 Chromium 140，1440×900，未注入业务数据；后端8002，前端4175。缺少浏览器库的问题通过项目 `.runtime` 内解包解决；中文字体只读 Windows 字体目录，未向C盘写入文件。

- [首页](./workbench.png)：实验入口与继续编辑位置优先，计数集中。后续仍需与一体化工作台联调。
- [环境目录](./environment-library.png)：真实海域、深度、频率；内部枚举不作主信息。
- [实际建设环境](./environment-builder.png)：真实采样图及坐标、可恢复计算参数；不是白色覆盖矩形。

自查发现并修正了继承的两列 Field 布局导致逐字换行、步骤按钮挤窄和白色 footer 背景。没有把首次截图直接作为完成证据。旧 Studio/Overview/Environment 源码只读；没有迁入旧 store、API、模拟循环。

## 门禁

- Frontend：45/45，production build通过。
- Backend：98/98，包含本次新增门禁及实际 Bellhop测试。
- ON相关 CTest：23/23（含 Backend、ns-3 kernel smoke、Channel1抽象回归、Channel2正常/强噪声/融合/缺失恢复数据和轨迹）。之后 Backend新增门禁又独立全量通过。
- OFF相关 CTest：5/5（轨迹、UUV、几何、Channel2基础与特征链）。未声称重跑全量 C++。
- 一次 Backend 重跑碰到既有 gateway PID 文件先创建、尚未写完的测试竞争：读取到空字符串。未修改网关实现，也未隐藏这次失败；随后两轮完整 Backend 通过。
- Git 管理目录缺失，`git status` / `git diff --check` 均无法执行。直接检查本批源码无尾随空白/冲突标记，不能将此称为 Git diff 门禁通过。未 add、commit、push、重建或覆盖 Git 历史。

## 本机依赖与继续使用

当前 Backend venv 已安装下面的可选 WOA 读取包，未修改原21包离线基础锁。正式 CMake构建不因此联网。

| 包 | 固定版本 | 供应 |
| --- | --- | --- |
| numpy | 2.2.6 | 官方PyPI CPython3.12 Linux x86_64 wheel |
| netCDF4 | 1.7.2 | 同上 |
| cftime | 1.6.4.post1 | 同上 |

SHA固定在 `backend/ocean-requirements.lock`。三个原始wheel保留内嵌license，存于 `.runtime/ocean-wheels`；未进入第三方核心基线。NumPy为BSD许可证，netCDF4/cftime为MIT；以wheel内附完整许可证及其打包库声明为准。

首次供应：`bash Platform/scripts/setup_ocean_authoring.sh --download`（明确联网）；已有wheel时同脚本不带参数执行离线安装。全新clone若未供应这组可选wheel，在线WOA会明确不可用，不回退参考数据。离线交付归档打包这组可选工具尚未纳入本批。

Bellhop源码：[A-New-BellHope/bellhopcuda](https://github.com/A-New-BellHope/bellhopcuda)，固定commit `b396d40ba49c2f349258b9687cfae8ff8323828f`，GLM子模块 `6ad79aae3eb5bf809c30bf1168171e9e55857e45`，CPU构建 `-DBHC_ENABLE_CUDA=OFF`。保留上游LICENSE；本机二进制SHA256 `806ac6a07ae4c9ebecba552243d234d54e486230f088f517228a87a323f5f514`。构建目录 `.runtime/tools/bellhopcuda`。启动器默认发现此处的程序，也支持显式 `PLATFORM_BELLHOP_EXECUTABLE`。计算中使用 `-1`，不引入运行时动力学或第二个仿真时钟。

对原8000/4173服务未重启，避免清空其内存运行记录；4175是隔离验证服务，数据不是用户原工作区的副本。浏览器演示只覆盖本批页面，不能代替完整用户任务验收。
