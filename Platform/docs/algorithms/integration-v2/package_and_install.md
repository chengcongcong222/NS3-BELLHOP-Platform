# 算法包与安装（A1 可用部分）

入口 `Platform/tools/protocolctl.py`，Python 标准库即可运行，C++23/CMake/Ninja/
ns-3.47 由本机离线工具链提供。当前模板为 planning-mac-v1；v2 尚不能宣称可用。

```bash
python3 Platform/tools/protocolctl.py configure \
  --build-root /path/to/algorithm-builds \
  --install-root /path/to/algorithm-installations \
  --ns3-prefix /path/to/ns3-3.47
python3 Platform/tools/protocolctl.py new my_mac
python3 Platform/tools/protocolctl.py check my_mac
python3 Platform/tools/protocolctl.py install my_mac
python3 Platform/tools/protocolctl.py list
python3 Platform/tools/protocolctl.py activate
```

包位于 `Platform/algorithms/user/my_mac`。XML、algorithm.hpp、README.md、tests/*.cpp
必需；src/*.cpp 和 include/*.hpp 可选。检查执行真实 C++ 编译、入口签名断言及包测试。
不执行包 CMake/Makefile，不下载依赖。原生代码仍属于受信任代码，不是安全沙箱。

install 在新候选目录复制当前已启用包和新包，编译 Worker，读取真实目录确认登记，
核验后发布 releases/<worker-sha>。已有程序不覆盖；构建失败保留日志且 active 不变。
同 ID/version 不同编译源码拒绝发布。保留原 header-only v1 的摘要算法；新增 .cpp
纳入摘要。XML v2 的完整资源摘要尚待 A4，不应把该兼容策略误称为 v2 已完成。

新服务显式设置 `PLATFORM_ALGORITHM_INSTALL_ROOT` 使用 active 安装；后台持有共享
租约直到所有 owned worker 正常回收和服务退出。activate 使用独占锁，服务仍运行时
显示待启用，不强杀、不热替换。即使后台空闲，也需正常关闭服务释放租约再显式启用；
这是本版有意采用的保守部署流程。未受管服务保持原固定程序，不受安装影响。

使用 `activate --release <旧程序SHA>` 可明确选回保留版本；程序或动态依赖摘要不匹配
则失败，不以新版本冒充旧版本。依赖清单记录实际 ldd 路径和 SHA，不自动复制系统库。
共享库升级后应恢复记录的依赖再复现。全自动多版本并行服务未实现。

## 本阶段证据

- integration_probe：new → check → install → activate，真实程序
  `03aa03ad4e053ef15f56fad368fcdbaa701493da3df4104973542185aa370ee1`。
- integration_multifile：外部函数必须由 src/extra_gap.cpp 提供；无需修改核心登记，
  第二次安装后真实程序 `69ef26cc0c6d44700cb3d9e6c91574df0553e476079f5eac89907f1827128edb`。
- 后台 AlgorithmCatalog 从两次实际程序读取目录；最终同时包含两个教学包与内置算法。
- InstallationLease 持有时 activate 拒绝切换，active 字节不变；释放后启用成功。
- `test_protocolctl.py` 4 项：多源编译运行、失败编译隔离、版本冲突/租约、非法路径。
- `test_compile_catalog.py` 3 项；后台算法目录/安装/网关 27 项。本阶段未跑全量回归。

这些是安装链证据，不是 v2 信道感知、流执行或物理 E2E 的验收。
