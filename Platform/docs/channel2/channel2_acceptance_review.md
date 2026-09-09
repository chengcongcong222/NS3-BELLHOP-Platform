# P0-CH2-01 Channel 2 Acceptance Review

审查日期：2026-09-09。结论适用于第一阶段 `Y = H(X) + N`，不是完整产品化。

## 1. Git 与隔离

- 正常执行 fetch 后，实际远端 `origin/feature/channel2-waveform-modem` 为
  `d76e2072379fa016fe1b79f9515dd5ca133bad40`。
- 使用已有干净工作树 `/home/ccc/work/NS3-BELLHOP-Platform-channel2`，
  从该远端节点建立 `review/channel2-acceptance`，取消对师弟分支的 upstream 绑定。
- 原主工作树保留在 `feature/frontend-workbench-reconstruction`，其 UX/Authoring
  未提交文件没有被覆盖或切换。没有 reset、restore、clean、force、amend 或 integration merge。
- 初次验收时修复与证据尚未提交；用户接受技术结论后执行 Acceptance Freeze，
  将这 10 个文件固化到独立 review 分支，提交消息为
  `fix(channel2): close waveform acceptance causality`。
  验收结论针对 **上述来源 SHA 加本轮修复**，不是未修复远端。
  冻结提交的实际 SHA 和远端验证结果由最终交付报告记录，避免自引用提交哈希。

## 2. 实际数据因果链

数字方位特征由既有特征编码器编码为 15 字节数据，经正常包队列和发送机会进入
发送执行器（`TransmissionExecutor`）。发送身份先分配，每次物理发送只编码一次：

`D → MSB-first bits → CRC32 framing → BPSK/BFSK → X(TransmissionId)`

波形发送模块（`WaveformTxPhy`）将不可变 X 发布到运行级波形仓库。
发送执行器之后才遍历候选接收节点。每个接收节点查自己的 H，但查到同一个 X；
不是每个 sender-receiver pair 再调制一次。广播仍是一条 Transmission 对应多条 Reception。

环境链真实为：正式 `.arr` → 严格 Bellhop 解析 → 路径归一化 → 内存声场资产
→ 接收位置的声场查询。没有在验收代码里手填路径，也没有在热路径执行 Bellhop。
标量 TL/首达时延在有效网格插值，路径向量取最近空间网格；不宣称相干路径插值。

接收模块（`WaveformRxPhy`）将路径绝对声压增益、相位、额外时延转换成复基带路径，
包括相对载频时延相位。先作分数时延多径叠加 H(X)，再添加接收噪声 N。
没有再次应用 aggregate TL，没有执行 H(X+N)。保留的原始接收波形是 H(X)+N，
理想相位校正作用于解调副本。

绝对首达时延只用于 ns-3 到达事件；波形内部只处理 excess delay。
首达时延没有重复填充为前导样本。ns-3 经 kernel gateway/dispatcher 仍是唯一时钟及调度者。

当前一次接收过程使用一个准静态 H；不在长 Transmission 内更新多个时变信道块。

接收波形经过解调、字节恢复、CRC 检查，成功时剥掉 CRC 并输出 D_hat。
判决不读取原始 truth bits；原始位与恢复位的比较仅在判决后计算实际 payload BER。
路由身份来自发送记录，交付内容来自 D_hat。应用读取交付仓库中的 payload，解码方位并融合。

## 3. 发现并修复的阻塞问题

原公共交付逻辑允许恢复 payload 缺省，这是通道1必要的兼容行为。
具体波形解调器虽然正常填入恢复数据，波形装配却没有强制执行这个约束。

新反例先执行真实解调，再用测试接收模块返回“成功、无恢复数据”。
**修复前实测**：run_ok=1，6 条交付、6 条融合观测、1 个融合结果。
新测试因此失败，证明确实发生了原始 D 回退，不是静态推测。

最小修复位于装配层：波形场景自动包装恢复数据检查器
（`RecoveredPayloadRequiredRxPhy`）。成功且 payload 缺省或为空即返回
`kFailedPrecondition`，在公共接收会话和交付之前终止。运行失败也清理波形仓库。
不改 contracts、不改公共 runtime 交付兼容分支、不改通道1。

修复后反例：run_ok=0，1 次实际解调（CRC 成功），0 交付、0 观测、0 融合。
使用波形语义的调用方必须经过显式 Waveform 装配；旧直接构造方式保持抽象通道语义。

## 4. 正式资产和 N1 → N2 实验

资产：`Platform/environment/assets/reference_shallow_water_v1/bellhop/reference_shallow_water_v1.arr`。
SHA256：`b13232e6cd47c5f4e6e70e05685d85bdd138adfe3888870c30f8dcf0b35826a3`。
这是正式 ReferenceShallowWaterV1 的 Bellhop-derived 参考资产，不是海试或实测 CIR。

发送 N1=(0,0,-65)m，接收 N2=(1000,0,-8)m。
发送 payload 为十六进制 `a53c`（16 bits），额外 CRC32 为 32 bits。
BPSK，复基带采样率 6000 Hz，符号率 60/s，中心频率 25000 Hz，
源级 110 dB re 1 μPa @ 1 m，配置占用带宽 4000 Hz，理想同步。
X 为 4800 样本，Y 为 5036 样本。
通过真实 ns-3 TxStart、Arrival、Finalize、Delivery、CycleClose 运行，不是仅调用离线调制函数。

首达时延为 653760135 ns，最大额外时延 39286673 ns，9 条有效路径：

| excess delay (ns) | absolute pressure gain | phase (rad) |
|---:|---:|---:|
| 0 | 0.000810161233 | 0 |
| 340522 | 0.000801598246 | 3.14159265359 |
| 6222367 | 0.00112390448 | 2.23013735193 |
| 7269740 | 0.00104582473 | 5.26123451957 |
| 21766186 | 0.00098611787 | 4.32619280349 |
| 24324358 | 0.000976144802 | 7.22549851871 |
| 36042929 | 0.000129588007 | 3.14159265359 |
| 36043346 | 0.0000108777995 | 3.14159265359 |
| 39286673 | 0.0000802543873 | 6.28318530718 |

两次运行只改变实际 AWGN PSD，保持 seed=42；内部再按发送/接收身份确定性派生。
PSD 单位 dB re 1 μPa²/Hz，当前生成器按采样带宽积分。
0 dB 不是关闭噪声。120 dB 是强噪声压力条件，不声称典型海洋背景。

| 实验 | PSD | payload error bits | BER | CRC | 交付 | D_hat |
|---|---:|---:|---:|---|---:|---|
| 正常 | 0 | 0/16 | 0 | 成功 | 1 | a53c |
| 恶化 | 120 | 8/16 | 0.5 | 失败 | 0 | 无可交付 payload |

恶化来自接收噪声功率的真实改变，不是随机翻 bit 冒充物理信道。

## 5. 融合因果对照

独立测试明确使用静态、已覆盖的几何：三个传感节点分别位于
(1000,0,-65)、(0,1000,-65)、(-1000,0,-65)m，中心节点位于 (0,0,-8)m。
三个目的链路均为 1000m 正式资产单元；两周期共 6 次物理发送，18 次物理接收处理。
最少融合观测数为 5。没有修改默认 catalog 或把测试几何伪装成默认场景。

| 对照 | 交付/有效观测 | 融合数 | 目的节点实际 BER | 融合位置 (m) |
|---|---:|---:|---:|---|
| 正常 | 6/6 | 1 | 0/720 = 0 | (199.963525065,150.058720033) |
| 改变实际编码数据 D' | 6/6 | 1 | 0/720 = 0 | (-200.027863569,300.073945239) |
| 成功但无恢复数据 | 0/0 | 0 | 首次解调 0/120；随后被门禁拒绝 | 无 |
| 强噪声 PSD=120 | 0/0 | 0 | 370/720 = 0.513888888889 | 无 |
| 通道1缺省恢复 payload | 6/6 | 1 | 不生成波形 BER | (199.963525065,150.058720033) |

正常测试逐条断言交付字节等于实际接收解调字节及发送物理帧内容。
差分测试保留发送记录的 D，但让测试发送包装器将目标 (-200,300) 编入真实物理帧 D'，
经实际 CRC、调制、Bellhop、多径、噪声、解调后再融合。若应用使用原记录 D，
结果将仍在 (200,150) 附近，测试必然失败。这是因果测试夹具，不是正式业务数据篡改。
强噪声下六个目的帧 CRC 全失败，上层真实失去融合结果。
通道1对照使用 RateBasedTxPhy 和抽象接收模块，确认没有被波形门禁影响。

原开发者 demo 的默认几何也独立复跑：BPSK/无噪声时 6 次发送、18 次接收尝试，
只有 2 条特征交付、0 个融合结果；全接收 BER 为 0.27314815。
该现象没有被掩盖：复杂浅海多径本身即可造成波形畸变和码间干扰，
当前第一版简单接收机尚未加入多径均衡，不能承诺所有几何都成功。
新增正常对照用明确记录的可解调几何证明完整闭环，而不以默认 demo 的退出码冒充融合成功。

## 6. 噪声双来源：后续产品门禁

1. 通道2实际 N 唯一来自显式 `WaveformPhyConfig.noise`（AWGN/Wenz/NoNoise）。
2. 现有 Experiment 的标量 noise 参数**不能**唯一决定波形噪声；只改它不会改变 N。
3. 外层仍查询 `NoiseObservation`，而波形解调器使用另一份配置，二者可能矛盾。
   这不是同一噪声被物理叠加两遍，而是元数据与实际噪声可不一致。
4. `CHANNEL2_PRODUCT_NOISE_GATE=OPEN`：正式产品开放前必须引入单一经审查的
   实验噪声配置，明确模式、PSD/功率单位、积分带宽、seed 和接收身份派生规则，
   同源生成标量观测与样本噪声；冲突配置应拒绝，并在 worker/wire/前端真实投影。

该修改跨实验契约和产品边界，不在本轮强行扩展。所有本轮证据明确记录实际 NoiseProfile，
默认前端仍通道1，因此此门禁不阻塞用户定义的第一阶段物理验收。

## 7. 独立构建与测试

本次新建的独立 build 目录（不借用旧 build 的结果）：

```bash
cd /home/ccc/work/NS3-BELLHOP-Platform-channel2
cmake -S Platform -B /home/ccc/build/channel2-review-20260909-off -G Ninja \
  -DBUILD_TESTING=ON -DPLATFORM_ENABLE_NS3=OFF
cmake --build /home/ccc/build/channel2-review-20260909-off -j 4
ctest --test-dir /home/ccc/build/channel2-review-20260909-off --output-on-failure -j 4
cmake -S Platform -B /home/ccc/build/channel2-review-20260909-on -G Ninja \
  -DBUILD_TESTING=ON -DPLATFORM_ENABLE_NS3=ON \
  -DCMAKE_PREFIX_PATH=/home/ccc/.local/ns3/3.47
cmake --build /home/ccc/build/channel2-review-20260909-on -j 4
ctest --test-dir /home/ccc/build/channel2-review-20260909-on --output-on-failure -j 4
```

| 版本 | OFF | ON/ns-3.47 |
|---|---:|---:|
| 远端原提交独立基线 | 65/65 PASS | 91/91 PASS |
| 本轮修复与因果测试 | 65/65 PASS | 98/98 PASS |

保持 C++23。原波形 PHY、离线波形链、正式 Bellhop 单链测试均包含在全量结果中；
ns-3 kernel smoke、事件调度、信号生命周期、原 Acceptance4Node、原融合、Channel1
scalar PHY 与 backend 测试全部通过。未改前端，本轮不声称重跑 frontend suite。
新增 7 项测试：single、single-degraded、normal、alternate、missing、degraded、abstract。
这些测试作为 Channel2 长期回归门禁保留在 CTest 中，不是一次性验收代码。
原提交加新测试时 missing 实际失败；修复后 7/7 通过。

可单独重放证据：

```bash
ctest --test-dir /home/ccc/build/channel2-review-20260909-on \
  -R platform_channel2_review_ -V
```

## 8. 修改范围及剩余限制

新增：

- `Platform/assembly/internal/recovered_payload_required_rx_phy.hpp`：拒绝非法波形成功。
- `Platform/application/tools/channel2_acceptance_review.cpp`：真实场景与因果证据工具。
- 本报告 `Platform/docs/channel2/channel2_acceptance_review.md`。

修改：

- `Platform/assembly/internal/scenario_runtime.hpp`：波形装配自动使用检查器。
- `Platform/application/CMakeLists.txt`：注册证据工具和 7 个 ON 测试。
- `Platform/docs/adr/ADR-0004-channel2-decoded-payload.md`：记录强制约束位置。
- `Platform/docs/channel2/channel2_current_state_audit.md`、`channel2_runtime_architecture.md`、
  `channel2_parallel_plan.md`、`channel2_technical_direction.md`：纠正过时能力说明，保留历史定位。

共 10 个文件，全部位于独立 review 工作树的 Platform 下。未改原主工作树文件，
未改公共 contracts、runtime、Bellhop、调制器或噪声实现。

仍延期：长发送内时变 H block、并发源波形干扰、复杂均衡、非理想同步、JANUS、
实测 CIR/Watermark、HIL、阵列测向以及前端完整产品化。当前重叠信号明确不支持。
CRC 是内部帧完整性校验，不是认证、纠错或已冻结硬件协议；BER 是仿真判决后观察，
不冒充硬件可用 truth。首阶段通过不代表所有多径下能可靠通信。

以上没有遗留阻塞本阶段的因果问题；后续产品噪声门禁仍须单独关闭。

P0_CH2_01_ACCEPTANCE_PASS
