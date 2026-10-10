# 算法包与安装

入口 `Platform/tools/protocolctl.py`，Python 标准库即可运行，C++23/CMake/Ninja/
ns-3.47 由本机离线工具链提供。新建模板默认 planning-mac-v2；用
`new my_mac --interface planning-mac-v1` 可显式建立兼容旧接口的包。

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

install 优先继承有效的待启用版本；仅当没有待启用版本时继承已启用版本。
因此先安装 A（不启用），再安装 B，最后的候选包含 A 和 B。继承的是安装目录内
不可变包副本，不是后来修改的开发目录。候选损坏、依赖不匹配时明确失败，不回退。
在新候选目录复制保留包和新包，编译 Worker，读取真实目录确认登记，
核验后发布 releases/<worker-sha>。已有程序不覆盖；任何失败不改变 active/pending。
同 ID/version 不同编译源码拒绝发布。保留原 header-only v1 的摘要算法；新增 .cpp
纳入摘要。v2 摘要覆盖规范化 XML 和包内全部文件（含说明、测试、资源）；
发布后修改任一文件都需要新版本。`src/` 应相对引用本包头文件，或使用 include/
下包专属命名的头文件，避免不同包的 algorithm.hpp 同名串用。

新服务显式设置 `PLATFORM_ALGORITHM_INSTALL_ROOT` 使用 active 安装；后台持有共享
租约直到所有 owned worker 正常回收和服务退出。activate 使用独占锁，服务仍运行时
返回 `ActivationBlocked` 和退出码 3（未启用），两个指针均保持不变，不强杀、不热替换。
即使后台空闲，也需正常关闭服务释放租约再显式启用；
这是本版有意采用的保守部署流程。未受管服务保持原固定程序，不受安装影响。

使用 `activate --release <旧程序SHA>` 可明确选回保留版本；程序或动态依赖摘要不匹配
则失败，不以新版本冒充旧版本。依赖清单记录实际 ldd 路径和 SHA，不自动复制系统库。
共享库升级后应恢复记录的依赖再复现。全自动多版本并行服务未实现。

`list` 状态：`Discovered` 仅发现源码，`InstalledPendingActivation` 仅当前候选，
`Active` 当前已启用，`Retained` 历史保留，`Invalid` 无效并附原因。
相同 active/pending 只显示 Active。成功启用只消费与目标相同的候选；
显式切换历史版本不删除另一待启用候选。安装与启用共用指针修改锁，防止交叉覆盖。
启用以 active 原子写入为提交点；如清理同值 pending 失败，它仍按已启用处理。

## A1 安装历史证据

- integration_probe：new → check → install → activate，真实程序
  `03aa03ad4e053ef15f56fad368fcdbaa701493da3df4104973542185aa370ee1`。
- integration_multifile：外部函数必须由 src/extra_gap.cpp 提供；无需修改核心登记，
  第二次安装后真实程序 `69ef26cc0c6d44700cb3d9e6c91574df0553e476079f5eac89907f1827128edb`。
- 后台 AlgorithmCatalog 从两次实际程序读取目录；最终同时包含两个教学包与内置算法。
- InstallationLease 持有时 activate 拒绝切换，active 字节不变；释放后启用成功。
- `test_protocolctl.py` 4 项：多源编译运行、失败编译隔离、版本冲突/租约、非法路径。
- `test_compile_catalog.py` 3 项；后台算法目录/安装/网关 27 项。本阶段未跑全量回归。

以上是 A1 历史记录。后续 v2 安装及真实运行证据见 CHECKPOINT 和 verification。

## v2 重放

Worker 为 authored v2 运行导出到 `<workspace>/planning-evidence/<run_id>/`。
`index.json` 为 Available 时，cycle-N.json 才是完整的可重放周期记录。

```bash
python3 Platform/tools/protocolctl.py replay /path/to/cycle-1.json
# 保留多个程序时，显式使用捕获该记录的安装版本：
python3 Platform/tools/protocolctl.py replay /path/to/cycle-1.json --release WORKER_SHA256
```

重放执行相同 C++ 算法及计划校验器，不启动 ns-3，不重新构造运行世界，
不在 Python 重写算法。输出 Matched 表示上下文算出的计划与捕获计划一致，
不意味着重新完成了物理仿真；实际发送另看 executions 中的对应记录。
