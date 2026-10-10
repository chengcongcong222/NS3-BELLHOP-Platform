# 兼容范围

| 边界 | 保持与增量 |
| --- | --- |
| planning-mac-v1 | 入口、header-only 摘要、队首选包不变；v2 独立注册表 |
| planning-mac-v2 | 新显式接口，不能静默转换为 v1；冻结包 SHA 与完整参数 |
| authored-run-v1/v2 | 保留旧数值边界和旧语义；不在旧文档中新增强制 FlowId |
| authored-run-v3 | 数值 ID/计数/纳秒为十进制字符串，包含正式 FlowId；只有 RegisteredMac v2 走此路径 |
| PlanningV1 | 现有周期执行类别名称，既可承载旧算法，也可承载 v2 包；不等同于 planning-mac-v1 |
| DigitalPacket | FlowId 为可空元信息；既有通信/融合 payload 字节格式不变 |
| RxDecodeResult | 可选事后比特比较；不改变 outcome、CRC 解码或 recovered_payload；旧相等比较需考虑新增统计 |
| Channel1 | 标量/理论模型不填实际比较位数或 CRC；不虚构实测 BER |
| Channel2 | 正式解调之后记录真实比较位数、错位数和 CRC；反馈不依赖 observer 成功 |
| Trajectory/Bellhop | 沿用正式轨迹和离线资产；规划预测使用当前快照，不读取未来真轨迹，不在线运行 Bellhop |
| RunEvent/Trace | 不新增或重定义通信事件；新增文件是只读审计，不是控制输入 |
| 程序切换 | 不改变既有固定路径服务；受管服务需正常退出释放租约，再显式 activate |

没有升级普通用户服务、重启旧进程、合并来源分支或修改旧系统。
新分支实现首先用于组内审查，之后再决定部署切换。
