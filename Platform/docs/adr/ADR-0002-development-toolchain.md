# ADR-0002：P0 开发工具链基线

- 状态：Accepted / Frozen for P0
- 日期：2026-08-18
- 适用范围：Platform P0 开发、构建与 CI 参考环境
- Supersedes：P0-S0-01 中暂定的 C++20/toolchain 基线
- 关联决策：ADR-0001 Runtime Foundation Decisions

## 1. 背景

P0-S0-01 的真实构建验证确认，Platform 在不启用 ns-3 时可以使用 GCC 13.3 和 C++20 完成配置、构建及 smoke test；但启用冻结的 ns-3.47 基线后，C++20 consumer 无法可靠包含 ns-3.47 core headers。

已验证事实如下：

| 配置 | 结果 |
| --- | --- |
| `PLATFORM_ENABLE_NS3=OFF` | Platform 配置成功；GCC 13.3；C++20；构建成功；`platform_build_smoke_test` 通过。 |
| `PLATFORM_ENABLE_NS3=ON` | CMake 成功发现 ns-3 3.47 和 `ns3::core`；Platform kernel smoke 仍以 `-std=c++20` 编译；ns-3.47 headers 使用 `std::stacktrace`，编译报错 `‘std::stacktrace’ has not been declared`；可执行文件未生成，ns-3 kernel CTest 为 Not Run。 |

官方 ns-3 构建基线已经将最低 C++ 标准由 C++20 提升至 C++23。基于冻结的 ns-3.47 版本和上述实际编译结果，需要冻结与其兼容的 Platform 开发工具链。

## 2. 决策

### 2.1 Canonical host 与开发环境

- Canonical host：Windows + WSL2。
- Canonical development environment：Ubuntu 24.04 LTS。
- Platform 仓库和编译目录必须位于 WSL Linux filesystem。
- `/mnt/c/...` 不作为 canonical build location。

Windows 文件系统仍可用于非 canonical 的文件交换或辅助操作，但不得以其构建表现代替 canonical 环境的验证结果。

### 2.2 编译器与语言标准

- 编译器：GCC。
- 当前已验证环境：GCC 13.3。
- 精确 GCC patch version 不作为永久架构要求，仍为 TBD。
- Platform language standard：C++23。

本 ADR supersede P0-S0-01 中暂定的 C++20 基线。升级到 C++23 不是为了扩大业务代码可使用的语言机制，而是因为冻结的 ns-3.47 基线要求 C++23 兼容环境，且真实构建已经证明 C++20 consumer 无法可靠包含 ns-3.47 core headers。

`contracts-v0.1` 尚未冻结，因此本次语言标准调整不构成对已发布 Platform API 的 compatibility break。

### 2.3 构建、脚本与版本控制

- Build system：CMake + Ninja。
- Scripting：Python 3。
- Version control：Git。

精确 CMake、Ninja 和 Python patch version 暂不冻结。

### 2.4 ns-3 基线与使用范围

- ns-3 版本固定为 ns-3.47。
- P0-S1 仅使用 ns-3 core module。
- Platform 通过 CMake `find_package` 使用导入目标 `ns3::core`。
- 接入 ns-3 不得引入其完整网络协议栈作为 P0-S1 的隐含依赖。

ns-3 的规范获取和安装流程冻结为：

```text
official ns-3.47 source release
    -> 独立构建
    -> user-local prefix
    -> Platform find_package
    -> ns3::core
```

禁止：

- 将 ns-3 vendor 到 Platform 源码中；
- 写死 `/home/ccc` 或其他个人绝对路径；
- 在 ns-3 不可用或加载失败时自动回退到其他 scheduler。

user-local prefix 的具体路径可由开发者环境配置提供，但不得成为源码中的个人路径常量。

### 2.5 CI 参考环境

P0 的 CI reference environment 冻结为：

```text
Ubuntu 24.04 + GCC + CMake + Ninja + ns-3.47
```

具体 CI provider 和工具的精确 patch version 暂不冻结。

### 2.6 C++23 使用原则

采用 C++23 作为编译基线，不代表业务 contracts 应大量使用新的或复杂的语言特性。公共 contracts 仍优先满足：

- 简单；
- 明确；
- 可移植；
- 可测试；
- 强类型；
- 少模板魔法。

除非能够解决明确问题并带来可验证价值，不得仅为了“使用 C++23”主动引入复杂语言机制。

## 3. 与 ADR-0001 的关系

本 ADR 与 ADR-0001 不冲突。

ADR-0001 冻结的 Runtime Foundation 架构规则全部保持不变，包括但不限于：

- ns-3 是唯一在线仿真时间权威；
- Platform contracts 不暴露 `ns3::Time`；
- `ns3::Time` 与 `SimTime`/`SimDuration` 的转换仅存在于 kernel/M1；
- 仅 kernel 直接操作 ns-3 Scheduler；
- 不建立第二套全局事件调度器，也不允许回退到其他 scheduler；
- 第一阶段仅使用实现离散事件内核所需的最小 ns-3 能力。

本 ADR 只 supersede 旧的 C++20/toolchain 暂定基线，并冻结与 ns-3.47 匹配的开发工具链。C++23 只解决构建和语言基线问题，不扩大 ns-3 的架构边界，也不 supersede ADR-0001 的 Runtime Foundation 架构。

## 4. 影响与后续约束

- 后续 Platform target 必须以 C++23 作为编译基线。
- P0-S1 的 ns-3 依赖必须保持为 `ns3::core` 最小集合。
- 开发与 CI 的规范验证必须以 WSL/Linux filesystem 或对应的 Ubuntu 24.04 Linux filesystem 为基础。
- 构建配置不得依赖个人绝对路径。
- 本 ADR 仅记录决策；对现有 CMake、构建文档或代码的落实应由后续获得明确授权的实施任务完成。

## 5. 仍保持 TBD

- 精确 GCC patch version；
- 精确 CMake patch version；
- 精确 Ninja patch version；
- 精确 Python patch version；
- CI provider；
- GoogleTest 获取策略；
- sanitizer 矩阵；
- Release / Debug / Default 的最终 CI 组合。

以上 TBD 不影响本 ADR 已冻结的 host、开发环境、语言标准、构建工具类别、ns-3 版本及其架构边界。
