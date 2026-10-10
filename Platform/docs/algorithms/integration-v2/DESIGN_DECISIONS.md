# P0-ALG-02：集成决策

基线：ebd7b1965e011ddaa2fb671305275f97eebec65d。
来源分支核验：与 origin/feature/product-workflow-convergence 一致。
独立实施：feature/algorithm-integration-v2；不切换或修改当前预览服务。

| 问题 | 核对结果与采用决策 |
| --- | --- |
| 接入定位 | 本轮仍是集中式周期规划，不扩成完整事件协议 SDK。 |
| 默认值 | 沿用 XML 单点默认值，代码接收已解析强类型配置；不重复维护代码默认。 |
| 节点 | 使用真实 NodeId 和周期快照；提供查找/连续索引辅助，不开放未来轨迹。 |
| 流身份 | 可空 DigitalPacket 元信息；不改变 4-byte 通信/15-byte 特征 payload。 |
| 选包 | v1 维持队首；v2 流内最早包 + 按 ID 条件删除，空流不替换。 |
| 路由 | 当前 composite 先 routing 后 MAC，但 MAC 尚读不到路由；v2 明确传递冻结路由。 |
| 声场 | 从 AcousticFieldChannelProvider 提取共用采样，不用假 ID 调用 Query。 |
| 反馈 | 现有 waveform observer 属只读展示；必须新增正式结果摘要，不能反向当控制输入。 |
| 结构来源 | 复用结构重绑定不能伪称本周期新计算，独立保存原生成时间/版本。 |
| 安装 | 先支持 v1 候选安装，多文件包受控编译；随后接入 v2。 |
| 启用 | 不替换活动 Worker 文件；不可变 release + 明确启用，活动 Run 留待启用。 |
| 工程预算 | 作为部署保护明示记录，超限报错，不当科研参数或静默裁剪。 |
| 环境版本 | 当前 C++ catalog 使用不可变修订限定 ID，没有独立数字资源版本列；v2 预测同时携带该 ID、内容版本 `fnv1a64:<payload checksum>` 和摘要。不能用 asset_format_version 冒充资源版本。 |
| 输入指纹 | 固定长度数值小端、字符串长度前缀的 FNV-1a64；只用于确定性审计，不作为安全签名。包与 Worker 身份继续使用 SHA-256。 |
| 随机流 | 固定 SplitMix64，按种子、算法 ID/版本、实例、周期和用途分隔；不含 RunId。 |
| 重放 | JSON 在 adapter；同一 C++ 算法/校验器，不在 Python 复制算法。 |

施工次序：A0 合同 → A1 安装 → A2 上下文/预测/反馈 → A3 流执行 →
A4 产品与示例 → A5 干净完整回归。每阶段单独记录真实结果。

接口差异必须显式拒绝；不把缺失 v2 字段降级成 v1。MATLAB、反应式 CSMA、
动态选举、并发空间复用、本轮跨周期算法私有状态均不实现。
