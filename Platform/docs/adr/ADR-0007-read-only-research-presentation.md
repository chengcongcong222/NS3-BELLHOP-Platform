# ADR-0007：科研展示的只读数据补齐

- 状态：Accepted，本轮用户批准的三批施工
- 日期：2026-09-29

既有 ReceptionTrace 增加可缺省的 waveform 摘要：已比较的 payload bit 数、
实际 bit errors、CRC、加噪前信号功率和实际生成的噪声功率。Application 的
observation adapter 在同次 Transmission/receiver 的接收完成记录上附加
已完成的 PHY observer 结果，不改变解调、交付、Trace 数量、顺序或时间。
零噪声保持零功率，不伪造有限 SNR；历史无摘要表示未记录，不表示零误码。

C++ aggregate 新字段默认为空；wire v1 在 Reception 上接受原有字段集合或
加 waveform 的字段集合，其他未知字段仍拒绝。无摘要时 encoder 保持旧形状。
新摘要的生产者与严格解码器必须一同升级；旧解码器不会被宣称支持新字段。
Frontend 的日志、动画与时间轴只共享显示筛选，统计与位置基于完整已观看前缀。

Environment 图表读取实际离线 Bellhop 到达产物。多径冲激幅度为压力增益；
从到达幅度平方求和得到的 TL 明确标注为非相干多径能量损失，不冒充相干
声场或连续场求解。声线只读取真实 .ray；缺少产物不拟造。离线辅助绘图失败
不把损坏图形发布为成功，也不改变在线 Runtime 的声场查询。

模式 A 继续既有 BPSK/AWGN 理论 BER 观测；模式 B 使用真实波形误码统计。
本轮不新增算法、物理判决或第二套时钟，不改变 ADR-0001 的因果边界。
