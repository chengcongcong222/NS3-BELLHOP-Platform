# 通道 2：特征信息与 Bellhop 波形 PHY

## 本次更新的三点说明（2026-09-06）

1. **Bellhop 信道接入**：使用 Bellhop 已离线计算的多径增益、相位和时延
   处理波形，不再手工填写演示路径。使用的是模型计算结果，不是海上实测，
   也不是在每次发包时重新运行 Bellhop。
2. **特征信息接入**：接通“特征报告 → 字节 → 比特 → 调制 → 信道 →
   解调 → 恢复字节 → 恢复特征”的流程。接收端交付解调恢复的数据，
   CRC 失败时拒收，不用发送端原始数据补回。
3. **信道之后加噪声**：先计算信道输出，再叠加接收端噪声，即
   Y = H(X) + N；BPSK/BFSK 均可组合 AWGN 白噪声或 Wenz 海洋背景噪声。

验证结果：ns-3 开启时全项目 91 项测试通过，关闭时另外验证了 3 项独立
链路测试。当前仍使用理想同步、未实现多径均衡，前端默认入口没有切换。

## 这次实现了什么

新增内部 Waveform Tx/Rx provider，并通过显式装配接入现有
`AcceptanceRunExecutor` 和 ns-3 场景。原有通道一、前端预设、公共 JSON
格式、Environment 查询策略和 ns-3 调度内核没有切换或改写。

链路为：

```text
现有特征业务产生 DetectionFeatureReportV1
  → 特征编码：15 字节负载
  → DigitalPacket / 发送队列
  → PHY 添加 4 字节 CRC32 → 152 比特
  → BPSK 或 BFSK → 一份复基带采样波形 X
  → 按收发位置查询 Bellhop 资产中的多径 → H(X)
  → 在接收端叠加 AWGN / Wenz 噪声 → Y = H(X) + N
  → 理想同步、解调、CRC 检查
  → 恢复 15 字节 → Runtime 交付 → 解码特征字段 → 现有融合业务
```

这里的特征报告来自项目现有的仿真业务，不是手写的测试 `0101`，
也不是外部真实传感器的采集结果。波形是计算机中的离散复基带采样，
没有模拟 DAC/ADC 硬件。

## Bellhop 接入方式

演示读取仓库已提供的：

`environment/assets/reference_shallow_water_v1/bellhop/reference_shallow_water_v1.arr`

使用现有导入器和 `AcousticFieldChannelProvider`。该 `.arr` 是 Bellhop
离线计算结果，不是这次手工填写的路径，也不是海上实测信道。本次没有
重新运行 Bellhop：运行时读取其计算成果，并按当前节点位置查询传播参数。
参考环境的频率为 25 kHz；水平距离覆盖 0–2500 m，深度网格覆盖 8–75 m。
不同海域、频率或超出覆盖范围的场景，需要环境模块先生成相应资产。
缺资产、频率不匹配或位置越界会报错，不会偷偷生成直达路径。

PHY 使用每条路径的绝对声压增益、相位和相对首达路径的时延，对波形进行
延迟、缩放及叠加。总传播损失不再额外乘一次；首达时延只用于事件时间，
不在波形前填入同样长度的零。

归一化器保留的 Bellhop 相位不包含实传播时延的载波相位。转换成复基带
路径增益时补入 `-2π × carrier_hz × excess_delay_seconds`，以首达路径
作为理想公共相位参考。多径相消、拖尾和码间干扰仍然保留。
这一约定对应 [BellhopCXX 的声场表达式](https://github.com/A-New-BellHope/bellhopcuda/blob/b396d40ba49c2f349258b9687cfae8ff8323828f/src/influence.hpp)。

## 加噪声的位置与参数

程序先计算 `H(X)`，再生成和叠加噪声 `N`。不是 `H(X + N)`。
单元测试逐采样核对了 AWGN、Wenz 的这一顺序。

- `none`：显式关闭噪声，便于单独观察多径。
- `awgn`：高斯白噪声；参数是声压功率谱密度，单位 dB re 1 µPa²/Hz，
  不是“直接指定接收 SNR”。
- `wenz`：Wenz-style 海洋背景噪声近似，使用风速和船舶活动参数；
  不是实测录音。零风速仍有其他噪声分量。

噪声在采样的复基带频带内生成；本演示采样率为 6000 Hz。
Channel 2 的噪声强度由 `WaveformPhyConfig.noise` 控制，公共合同中的旧
标量 `NoiseObservation` 仅保留兼容上下文，不再额外生成第二份噪声。
演示把随机种子同时显式传给实验和噪声配置；每个 Transmission/接收节点
再派生独立、可重复的噪声序列。

## 在 Ubuntu / WSL 中运行

需要 C++23、CMake 和 ns-3.47 core。使用外部构建目录，例如：

```bash
cd /home/administratorlzx/work/NS3-BELLHOP-Platform
build_dir=/home/administratorlzx/build/ns3-bellhop-channel2
cmake -S Platform -B "$build_dir" -G Ninja \
  -DPLATFORM_ENABLE_NS3=ON -DBUILD_TESTING=ON \
  -DCMAKE_PREFIX_PATH=/home/administratorlzx/.local/ns3/3.47
cmake --build "$build_dir" --target platform_application_channel2_feature_demo -j 2
```

构建成功后任选一种组合：

```bash
"$build_dir/application/platform_application_channel2_feature_demo" --modulation bpsk --noise none
"$build_dir/application/platform_application_channel2_feature_demo" --modulation bpsk --noise awgn --psd-db 0
"$build_dir/application/platform_application_channel2_feature_demo" --modulation bfsk --noise wenz --wind 0 --shipping 0.5
"$build_dir/application/platform_application_channel2_feature_demo" --modulation bpsk --noise wenz --wind 5 --seed 42
```

演示明确使用：60 bit/s、25 kHz 载频、6000 Hz 复基带采样率、
110 dB re 1 µPa at 1 m 源级、BFSK 频偏 ±30 Hz、4 kHz 声学查询带宽、
最多 1,000,000 个采样。它们是可替换的演示配置，不是公共协议默认值。
QPSK 仍预留，但选择后会明确报“不支持”，不会偷偷改用 BPSK。

由于 15 字节负载加 4 字节 CRC 在 60 bit/s 下需要约 2.533 秒，
本演示采用 6 秒 TDMA 时隙，保留 2 秒保护间隔。没有修改通道一的 4 秒预设。
4 节点场景跑两个通信周期：共 6 次发射、18 次接收尝试，其中包含旁听节点。

输出列解释：

- `paths`：此次接收实际使用的 Bellhop 路径数。
- `payload_bits / bit_errors / BER`：120 个特征负载比特的恢复情况，不含 CRC。
- `CRC_OK`：接收端通过 CRC 检查才交付，CRC 不是纠错算法。
- `HX_power_upa2 / noise_power_upa2`：信道输出功率与随后加入的噪声功率，
  在整个接收采样窗口内取平均，包含多径尾部。
- `delivered_features`：融合节点收到并交付给现有特征业务的报告数。
- `fusion_results`：达到现有融合条件后产生的结果数，收到少量报告不保证能融合。
- `CRC_frame_error_ratio`：包括旁听接收的 CRC 失败比例，不应误称为目标节点 PER。

当前参考参数下，默认 BPSK 无噪声演示的目标节点成功接收 2/6 份报告，
不足以产生融合结果。这不是承诺所有真实信道都能无误接收：
当前解调器没有多径均衡，Bellhop 多径本身就可能产生误码。
不要通过复制原始数据或归一化掉衰减来“修复”这个结果。

强噪声反例：

```bash
"$build_dir/application/platform_application_channel2_feature_demo" \
  --modulation bpsk --noise awgn --psd-db 120
```

该极大噪声值仅用于测试：应看到误码、CRC 失败和零特征交付，
而不是仍然恢复出发送端原数据。

## 数据真实性、测试与边界

每次物理发射只调制一次，多个接收节点共享同一份不可变 X。
接收判决只依赖接收波形、显式配置及理想同步信息。原始比特在判决完成后
才用于统计 BER，不能用于决定成功、修正错误或者填充接收负载。
解调后的字节通过 `RxDecodeResult.recovered_payload()` 交给 Runtime；
路由标识保留，但负载替换为接收结果。CRC 失败没有原数据回退。

```bash
cmake --build "$build_dir" -j 2
ctest --test-dir "$build_dir" --output-on-failure
```

新增测试覆盖：

- BPSK/BFSK + 无噪声/AWGN/Wenz 的实际 ns-3 特征业务；
- Bellhop 多径上的特征字段逐项恢复，以及多接收端共享 X；
- 原始真值替换不影响接收判决；实际损坏波形会使 CRC 失败；
- Runtime 本地交付、转发都使用接收字节；
- 接收端加噪顺序、载波相对时延相位、帧开销、过大配置拒绝；
- 强噪声无虚假交付，同配置重复运行结果一致。

目前仍采用理想帧起点、帧长及首达相位参考；没有前导码搜索、频偏/Doppler
估计、多径均衡、纠错编码、真实硬件 DAC/ADC，也没有多发射信号叠加的
干扰接收算法。重叠信号会明确报不支持，不能用于碰撞场景。

新增演示通过显式内部入口启用波形模式；前端/worker 默认入口仍为通道一。
波形实际 BER 在上述观察输出中统计，旧的标量 BER 验收字段保持“未评估”，
不冒充实测波形 BER。波形装配要求 `quality_mode=None`。

共享合同、Runtime 与装配层的增量修改见
[ADR-0004](../docs/adr/ADR-0004-channel2-decoded-payload.md)：
源码调用方式兼容旧 provider，但 C++ 类型布局变化，需要重新编译。
功能分支合并前，需要集成负责人审查这些共享目录修改。
