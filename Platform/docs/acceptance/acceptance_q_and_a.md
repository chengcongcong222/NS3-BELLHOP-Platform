# Acceptance Q&A

**为什么说是基于 ns-3？** 真实 Worker 通过 M1/Ns3KernelGateway 调用
`ns3::Simulator` 的 Now、Schedule、Run、Stop、Destroy；kernel、dispatcher、signal lifecycle、
sequential-run reset 和 Acceptance4Node ns-3 ON tests 验证实际执行机制。

**这些模块为什么不直接写进 ns-3 源码？** Platform 保持 ns-3 为离散事件内核权威，领域模块通过
稳定合同与 gateway 组合，避免维护 ns-3 fork，并保持职责与测试边界。

**Bellhop 是实时运行的吗？** 不是。Bellhop 在资产构建阶段离线执行；现场 Run 只查询不可变
AcousticFieldAsset。

**BER=0 是什么意思？** 它是当前 simplified BPSK/AWGN 模型的 double 数值结果，高 SNR 下可能达到
浮点数值表示下限，不表示绝对无误码。

**BER 是实测的吗？** 不是，当前 evidence type 是 Modeled；硬件/实测 BER 是 TBD。

**60 bit/s 在哪里体现？** captured Experiment PHY 的
`bit_rate_bits_per_second` 为 60，backend AcceptanceReport 给出对应 verdict，Result 页面同时展示。

**为什么 5 个方位点不是 5 个节点？** observation identity 是 sender + observation sequence；同一 sensor
可在不同周期贡献不同 observation。要求是至少 5 条观测，不是至少 5 个节点。

**为什么只有 4 个通信节点？** 第三方规模要求是 3–4 nodes；Acceptance4Node 使用 3 个移动 sensor 和
1 个固定 fusion center。

**Reference 环境是真实试验场吗？** 不是。它是由公开 WOA23/GEBCO 2020 构建的
Reference/proxy modeled environment。

**110 dB 是实测源级吗？** 不是。它是 **110 dB re 1 µPa @ 1 m** 的 simulation configuration；
hardware calibration 是 TBD。

**无到达和解码失败有什么区别？** NoArrival 表示没有物理 arrival，因此没有 Reception；NotDecoded
表示 arrival 已进入 Rx processing，但未解码。

**结果为什么可以重复？** 正式 release、资源版本、环境 checksum、seed/config 和事件 stable ordering
被冻结；三次 Golden Run 的 normalized Result/Fusion/Trace/Acceptance hashes 一致。

**如果 Acceptance Fail，是不是软件运行失败？** 不是。Run lifecycle `Completed` 与 Acceptance verdict
是两个维度；Fail fixture 验证正常完成的 Run 可以输出业务验收 Fail。

**为什么 ns-3 是外部 prerequisite？** 当前包选择显式 external-prefix 供应以避免不透明地复制 runtime
libraries；preflight 验证 3.47，Worker 仍实际链接并运行它，运行时不下载 ns-3。
