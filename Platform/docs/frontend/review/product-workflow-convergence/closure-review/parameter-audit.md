# 实验参数语义审计

范围：仅核对当前本地实现，不把预置值当成物理定律或平台能力边界。本次按实际能力分层：普通设置为业务、物理模式、通信周期；设备、时序、随机种子、算法策略与结果参考折叠为高级设置。

| 参数 | 面向用户的意义 | 当前实现与可调边界 |
|---|---|---|
| 路由 | 数据直接送往汇聚节点，或沿有向网络经中继送达 | 已接入 DirectToFusionCenter、ShortestPath；后者为最少跳数，等跳数选择较小 NodeId。没有 Static 入口。 |
| MAC | 节点何时获得发送机会 | 仅配置式 TDMA 已接入。ALOHA、FDMA、CSMA/CA 不可作为可运行选项。 |
| 通信周期、时隙 | 一轮为每个非汇聚节点安排一个时隙 | 周期数和 `slot_duration_ns` 可进入正式定义。当前总时长为周期数 × 非汇聚节点数 × 时隙长度；不是轨迹采样数或视频帧。当前运行装配仍限 3～6 节点，非中心节点均须能发送。 |
| 保护时间 | 在时隙内给收发结束留出的时间余量 | `guard_interval_ns` 可配置，已包含在时隙长度内，不重复加到总时长。时隙须容纳最大规划包时长与保护间隔；跨周期物理事件仍明确失败。 |
| 网络刷新间隔 | 多少个通信周期更新一次网络结构及生效调度 | `network_update_interval_cycles` 可配置；默认 10 不代表每十帧向 TDMA 读一次结果。每周期仍提交状态；刷新周期计算新的结构/计划，其他周期使用已生效模板。 |
| 有效观测数阈值 | 当前融合策略至少收集多少条成功交付的有效观测，再尝试求解 | 可配置，当前 Experiment/装配策略要求 ≥5；不是节点数，也不是最小二乘的一般数学下限。同一节点不同时刻的报告可计为不同观测。周期末检查，数量和几何条件同时满足才融合；用户不必另设触发时刻。 |
| 融合耗时参考 | 从本轮最早有效观测的采样时刻，到融合结果产生的仿真时长对照值 | `maximum_fusion_period_ns` 是参考，不是定时器、CPU 耗时或自动终止期限。代码在完成求解后记录是否在参考范围内；不因超过该值丢弃结果。产品不显示自动验收 verdict。 |
| 目标坐标 | 探测业务生成模拟方位报告时采用的目标位置 | `target_x_m`、`target_y_m` 可配置；只用于生成观测及对照，不作为融合求解器输入。纯通信业务不使用该探测负载。 |
| 比特率、频率、带宽、声源级 | 发射机实际工作配置 | 后端并未固定 60 bit/s、25 kHz、4 kHz、110 dB。比特率必须正整数；其余进入 C++ 有限性/正值校验及环境覆盖检查。频率必须与离线声场匹配，修改不意味着环境自动重新计算。 |
| 随机种子 | 固定配置下复现随机噪声序列 | 非负整数，随 Run 冻结。模式 B 的 AWGN 明确消费种子，并按发送与接收身份确定独立序列；关闭噪声或不使用随机路径时，换种子不保证结果变化。 |
| 模式 A 等效噪声 | 接收频带内的等效噪声功率 | `equivalent_noise_power_db_re_1upa2` 可配置，默认 45；不是每赫兹 PSD。它用于抽象接收/质量模型，不等于逐比特实测误码。 |
| 模式 B 波形噪声 | 在接收波形中是否加入 AWGN，以及其功率谱密度 | BPSK/BFSK、None/Awgn、`waveform_noise_psd_db_re_1upa2_per_hz` 已实际接入。PSD 单位是 dB re 1 μPa²/Hz，与模式 A 参数分开。关闭额外噪声不删除多径影响。模式 C 尚未实现。 |

## 默认值不等于固定值

此前前端将设备参数、时序、网络间隔显示为只读，属于界面限制，不是后端无能力。普通 authored 路径读取用户 payload；缺项时才采用 6 s 时隙、2 s 保护、10 周期刷新等默认值。速率只要求正整数，没有“必须 60”的后端约束。应提供折叠高级编辑与单位/限制提示，而不是声称这些输入不能生效。

注意旧显式 Acceptance preset 的模式 B 适配器会把时隙设为 6 s；不能直接将旧模式 A manifest 的 4 s 显示成该次模式 B 的实际时序。authored 路径不使用这个 preset 覆盖规则。

## 核对位置

- [Backend 草稿校验](../../../../../backend/src/ns3_factory_backend/authoring.py)：`validate_payload`；检查枚举/正值，并未锁定设备数值。
- [Run 定义冻结](../../../../../backend/src/ns3_factory_backend/api.py)：`create_authoring_experiment_run`；读取 `slot_duration_ns`，不是 `tdma_slot_duration_ns`。
- [Worker 精确装配](../../../../../worker/adapter/authored_run_definition.hpp)：配置进入 `ExperimentDefinition`。
- [定义约束](../../../../../application/include/ns3_factory/application/domain.hpp)：协议、设备参数及融合策略校验。
- [时长与场景限制](../../../../../assembly/internal/acceptance_scenario_config.hpp)：节点规模、时隙、保护和非中心节点计数。
- [融合实现](../../../../../assembly/internal/acceptance_feature.hpp)：`TryFuse` 的数量检查、求解、结果耗时与参考比较；[业务入口](../../../../../assembly/internal/acceptance_feature_application.hpp)只处理已交付报告。
- [Worker 物理模式](../../../../../worker/internal/simulation_worker.hpp)：模式 B 调制、噪声与 preset 6 s 时隙适配；[波形噪声序列](../../../../../phy/internal/waveform_phy.hpp)使用显式 seed。
