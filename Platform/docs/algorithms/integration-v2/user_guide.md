# 服务端算法接入操作指南

本版针对集中式、周期式 MAC 规划。算法读取当前正式快照，返回串行的流级发送安排。
无需接触 Runtime 节点对象、队列、ns-3 调度器或 Bellhop 文件。

## 1. 建包与替换算法核心

先按 [安装指南](package_and_install.md) 配置本机工具链。运行：

```sh
python3 Platform/tools/protocolctl.py new my_mac
```

编辑 `Platform/algorithms/user/my_mac/algorithm.hpp` 和 `algorithm.xml`。
模板包含可运行的信道排序教学算法及行为测试；不是空函数或 mock。
多文件示例见 `algorithms/user/flow_round_robin`。

唯一入口签名：

```cpp
auto Plan(const ns3_factory::planning::PlanningContext& context,
          const ns3_factory::planning::Parameters& parameters)
    -> ns3_factory::contracts::Result<ns3_factory::planning::FlowTransmissionPlan>;
```

公开入口 `ns3_factory/planning/sdk.hpp`。参数已按 XML 校验，仍建议在包内 Bind 为
自己的强类型 Configuration；不要再给 C++ 配另一套默认值。
只将实验敏感性参数写入 XML；结构字段与计算结果不应伪装成可调参数。

## 2. 把自己的节点/矩阵接上平台

- 节点：`context.data().world.nodes()`，用 `world.FindNode(NodeId{...})` 查找。
  矩阵实现可用 `planning::NodeIndex(context, id)` 得到当前快照的连续下标；
  NodeId=0 合法，NodeId=99 不代表数组第 99 项。映射不能跨快照缓存。
- 待发业务：`context.data().demands`，键是队列所属节点 + FlowId。
  `source_node_id` 是原始源，不能当作中继当前队列节点。
  `queued_packets + ready_packets` 只含已排队和确定的周期入口数据，不包含未来中继成功。
- 路由：`flow.next_hop` 已由本周期冻结路由确定。无路由不安排；MAC 不能改写下一跳。
- 信道：`context.FindLink(owner, next_hop)`，量取自正式离线资产的共用采样器。
  Available / NoArrival / Unavailable 不得互相替代。缺路径明细不等于多径为零。
- 历史：`context.data().previous` 只有上一完整提交周期。首周期 NoHistory 合法；
  没有实际比特比较就没有实测 BER。目标接收与物理旁听分列。
- 随机性：用 `NamedRandomStream::Create(context, "purpose")`；不要自行取墙钟或 RunId。

算法产生 `ProposedFlowGrant{flow.key, offset, guard}` 数组，调用
`FlowTransmissionPlan::Create(context, grants)`。校验器计算真实编码时长 + 全环境传播
尾部 + 余量，拒绝重叠、越界、超额或未知流。每条安排至多发一个该流真实包；
空流记未使用，不会发送另一个流的队首包。

## 3. 编译安装与界面选择

```sh
python3 Platform/tools/protocolctl.py check my_mac
python3 Platform/tools/protocolctl.py install my_mac
python3 Platform/tools/protocolctl.py list
python3 Platform/tools/protocolctl.py activate
```

check 必须通过真实编译和包内行为测试。install 编译新 Worker 并验证其真实目录；
放入源码目录不代表算法已可选。受管后台未退出时 activate 只提示待启用，不中断运行。
后台以 `PLATFORM_ALGORITHM_INSTALL_ROOT` 指向安装根启动后，在实验工作台选择已安装
算法即可，XML 参数自动显示，不需要为算法新增前端 if 分支。

通信流是应用的待发需求，而非算法的发送计划；MAC 选择这些需求的发送时机/顺序。
本接口不能自行创建业务、改路由或实例化节点。需要此类能力的方案不应强行当作 MAC 包。

保存运行时冻结算法 ID、版本、包 SHA、Worker SHA、参数（含默认值）、FlowId。
v2 算法使用 `ns3-factory-authored-run-v3` 文件；界面执行类别仍是 PlanningV1，
表示现有“周期规划”执行链，不是包接口降级。旧算法仍使用原 v1/v2 定义。

## 4. 对照结果与排错

从服务器 workspace 的 planning-evidence/run-id 取 index 和 cycle 文件，按安装指南
重放。plan 与 executions 可逐项对照 grant/flow/PacketId/TransmissionId/下一跳/时刻。
Run definition 同时保存完整环境、轨迹、场景和实验版本，包安装 manifest 保存依赖摘要。

| 情况 | 含义与操作 |
| --- | --- |
| AlgorithmUnavailable | 未安装精确包/程序版本；不能用同名新包悄悄替换旧运行 |
| AlgorithmConfigurationInvalid | 参数缺失、未知或越界；按 XML 修正 |
| PlanningInputUnavailable | 快照、路由、声场范围或预算不满足；修正资源/配置 |
| PlanningPlanInvalid | 输出流、时刻、需求额度或安全间隔非法；修正包内算法 |
| UnusedSelectedFlowEmpty | 该安排执行时流为空；不会跨流补发 |
| NoArrival | 资产中的真实无到达，不等于程序加载失败 |
| evidence Unavailable | 只读导出失败/超预算；不能把残缺记录当完整证据，仿真因果不受影响 |

## 当前限制

这是受信任本地 C++ 扩展，不是运行陌生代码的安全沙箱。集中式全局快照不等于分布式
节点通过通信获知的信息。仅串行周期计划，不实现反应式 CSMA、动态选举、空间复用、
跨周期私有状态、HIL 或 MATLAB。链路/流/安排预算分别为4096/4096/100000，
为部署保护而非算法参数；超限报错。导出单周期8 MiB、整次运行64 MiB，失败明确标记。
不自动复制系统共享库；复现旧程序需保留其 manifest 所记录的动态库版本。
