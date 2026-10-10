# v2 输入字段

| 分组 | 字段/来源 | 空值和校验 |
| --- | --- | --- |
| 上下文 | run、cycle、snapshot、observed_at、valid_from/until、摘要 | 边界时刻一致；RunId 不影响随机序列 |
| 节点 | 快照 NodeId、位置、速度、收发/双工 | ID 排序；0 合法；不推测剩余能量 |
| 结构 | 有向边、角色、原生成周期/版本/时刻 | 复用来源不重写 |
| 路由 | 正式 RoutingPlan、版本、下一跳 | 无路由显式空，不直达回退 |
| 需求 | queue_owner + FlowId、原始源/最终目的、队列/本地新增数、最大包时长 | 不预分配未来 PacketId，不预测本周期中继成功 |
| 链路 | tx/rx、频率/带宽、位置、环境 ID/版本/摘要、方法版本 | 全能力相容有向对；超预算报错 |
| 预测量 | loss_db、first_delay_ns、path_count、max_excess_delay_ns | NoArrival 无数值；scalar-only 无多径明细；越界是错误 |
| 反馈 | 前周期、时窗、目标与旁听分列、发送/到达/CRC/交付/比特计数 | NoHistory/NoSamples；实际比特与理论 BER 不混加 |
| 参数 | 已解析完整有效配置、命名确定性随机流 | 不以 RunId、墙钟、地址播种 |

所有时间/计数/数值 ID 导出为规范十进制字符串；浮点拒绝 NaN/Inf。
知识范围 global-simulation；不是无线节点通过报文获得的知识。
上下文只读借用仅一次调用有效，禁止保存指针跨周期。

`PlanningContext` 持有值副本，入口以 const 引用读取；`world.committed_at()` 为 T_k，
有效时段为 `[T_k, T_k + duration)`。`structure_origin` 独立于重绑定后的 structure。
`previous == null` 对应导出 NoHistory；反馈行不存在对应该链路 NoSamples。
实际反馈由 Runtime 在完整提交后发布，不从 Trace、SSE 或结果缓存反向读取。

边界 JSON 是 `platform.planning-context.v2`，指纹覆盖规范字段；读取时重新校验并拒绝
未知字段、非规范整数、未来反馈、来源不匹配和指纹不一致。包源码仍只使用 C++ 类型。
