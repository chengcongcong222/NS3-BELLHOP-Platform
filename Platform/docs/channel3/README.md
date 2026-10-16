# OSDM 与实测信道回放

2026-10-16 六阶段实施与主入口验证已完成，受测身份、操作证据及限制见 [收口记录](closeout.md)。

## 三条业务线

| 模式 | 传播来源 | 接收结果 | 场景语义 |
| --- | --- | --- | --- |
| A | 已发布 Bellhop 声场 | 抽象接收模型，可附理论 BER | 正式环境和节点轨迹 |
| B | 已发布 Bellhop 声场 | BPSK / BFSK / OSDM 波形解码、CRC、逐比特比较 | 正式环境和节点轨迹 |
| C | 有版本和校验和的 UWA 时变信道录音 | 同一波形接收链，保留真实解码失败 | 单向录音链路示意，不虚构海域几何 |

OSDM 是调制解调配置，不是第四种信道。训练序列估计信道；当前 MMSE
均衡使用仿真噪声功率。MAC 按实际编码、训练、补齐与循环前缀计算发送时长，
不把标称速率当有效吞吐量。同步失败为正常未解码，不崩溃、不补零误码。

## 科学依据与边界

依据是库作者的[文件规范](https://uwa-channels.github.io/docs/file_specifications/)
和[参考实现](https://github.com/uwa-channels/python/tree/dbf373ca7cd07ac4eb1b24a191d76bea18f73560)，
不是国际标准认证。版本、许可证及校验和锁在 `Platform/adapters/uwa-reference.lock.json`。
数据来自[固定 Zenodo 记录](https://doi.org/10.5281/zenodo.21287414)。

- 保留时变复系数、phi/theta 优先关系、延迟漂移与 f_resamp；不阈值稀疏化，不逐包冻结信道。
- Blue 多路是接收阵列的阵元，不是独立网络节点；独立试次不拼成同时链路。
- 归一化系数不产生绝对声压级或传播损失；同步相对延迟不等于真实声传播时间。
- 用户填写的传播时延是显式建模假设，影响排槽与到达；不据此声称验证真实海域 MAC 性能。
- 不推断反向信道，不叠加合成轨迹多普勒，不制造实测方位观测或融合结果。
- 首版仅一个只发节点、一个只收节点，Direct、CommunicationOnly、AdaptiveTdma 或配置式 TDMA。
  第三方预测型 MAC、多个独立收发端、阵列合并、移动几何重建未开放。
- 录音导入接受官方 MAT v7.3；每个资源固定一个阵元或试次。矩阵不进入前端 DTO。
- 噪声是官方统计模型生成的固定连续实现，不是原始海试音频。alpha < 2 的有限样本功率比
  不解释为有限方差理论 SNR。噪声编号、原文件哈希、尺度、版本随运行冻结。

## 用户操作

1. 在实验工作台物理模式中选择 C，确认新建录音实验；也可打开 `/workspace/experiment?mode=C`。
   不覆盖已有 A/B 草稿。
2. 选已导入录音，核对阵元/试次、频率、带宽、时长和出处。
3. 设置录音起点、建模传播时延及相对幅度。中心频率取录音值，不另建 Bellhop 环境。
4. 选择调制、连续噪声（或明确不加噪声）、TDMA、业务周期和周期数。
5. 保存并运行；在同一个运行与结果页面查看事件、误码数 / 比较位数、CRC，按原配置重跑。
   没有成功解码不等于运行系统失败。

实验保存为普通 Scenario / Experiment 草稿，启动冻结 `ns3-factory-authored-run-v4`。
两个示意端点也绑定正式静止轨迹；不引入旧运动模型。Run / Result 包含源文件与资源哈希、
参考实现版本、片段起点、建模时延、调制参数及噪声实现。重跑校验原资源，缺失或变化时拒绝，
不回退参考浅水环境。噪声实现固定，修改实验种子不会偷偷更换选中的噪声样本。

## 管理员导入

导入是离线准备操作，运行中没有 Python 信号处理子进程。准备 Python 3.12、
numpy 2.2.6、scipy 1.15.3、h5py 3.14.0，并检出锁定的官方代码。以下从仓库根目录执行；
workspace 指现有主入口 authoring workspace，不是另开服务。原始大文件不进入 Git。

```bash
python Platform/adapters/tools/import_recorded_channel.py \
  --input /data/blue_1.mat --repository /path/to/workspace/recorded-channels \
  --id uwa-blue-1-element-0 --name 'Blue 1 · 第1阵元' \
  --element 0 --axis-kind ReceiverElement --bandwidth 5000 \
  --citation https://doi.org/10.5281/zenodo.21287414 --license CC-BY-4.0 \
  --source-sha256 11dc8eae246f44782fe1a4909af380d8f4da642f802fdcb17e083c63d0be07d1

OPENBLAS_NUM_THREADS=1 python Platform/adapters/tools/prepare_recorded_noise.py \
  --reference /path/to/pinned-uwa-python --noise /data/blue_noise.mat \
  --samples 624000 --sample-rate 52000 --elements 0 --seed 23 --scale 0.01 \
  --output /path/to/workspace/recorded-noise/blue-noise-seed23/1 \
  --id blue-noise-seed23 --name 'Blue 连续噪声 · 编号23' \
  --citation https://doi.org/10.5281/zenodo.21287414 --license CC-BY-4.0
```

噪声示例覆盖 12 秒，实验不得超时。Blue 噪声统计带宽为 4882.8125 Hz，
选择它时 UI 收窄占用带宽，不声称 5000 Hz 全带噪声已获支持。
资源不可覆盖；需新实现时使用新 ID。目录只允许可信管理员写入。
资源目录重新读取即可看到导入结果，不要求重启，不设全局可变信道单例。

## 门禁与性能预算

门禁：`official_replay_equivalence_test.py`、`recorded_noise_reference_test.py`、
`recorded_waveform_channel_test.cpp`、`recorded_channel_e2e_test.py`、`recordedExperiment.test.tsx`。
真实录音测试须显式配置环境变量，跳过不算通过。

数值门槛：float64 输出相对 L2 ≤ 1e-6，禁止事后相位、幅度、时间拟合；同一程序相同输入
逐样本重复一致。门槛是项目约定，不是作者精度保证。

部署保护预算：2000 万复系数、单次重采样输出最多 1000 万样本、有理重采样分子/分母预算。
这些不是 UWA 物理极限。目录只返回清单；运行启动校验、加载一次，数据归运行所有。
实际验证与部署状态见收口记录；未测设备/数据集不宣称已验证。
