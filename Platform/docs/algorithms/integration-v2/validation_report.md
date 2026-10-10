# P0-ALG-02 行为门禁

基线 `ebd7b1965e011ddaa2fb671305275f97eebec65d`；独立工作树、独立构建及安装根。
此表索引具体断言，不以用例数量代替行为验证。最终执行计数见 CHECKPOINT。

| 门禁 | 源码/测试与断言 |
| --- | --- |
| T01 旧算法 | adaptive_planning_test、XML 测试、authored_planning_validation_test 保留 v1 排序、摘要和旧定义 |
| T02 节点编号 | flow_planning_test 检查 0/10/20/99 的 NodeIndex；真实 E2E 同样使用这些 ID |
| T03 重排 | flow_planning_test 规范指纹；channel_aware/tests/ordering.cpp、flow_round_robin/tests/round_robin.cpp 检查顺序不变 |
| T04 一致时刻 | PlanningContext::Create 拒绝未来链路时刻；flow_runtime_test 三周期保留原结构来源 |
| T05 轨迹 | algorithm_v2_e2e_test 检查第二周期正式位置改变；planning_context_assembler_test 检查几何改变预测 |
| T06 环境 | planning_context_assembler_test 切换真实 provider/ID/摘要，损失随资产改变；无全局链路缓存 |
| T07 共用采样 | acoustic_field_channel_provider_test 每次 Query 对照 Sample；assembler 直接比对损失和时延 |
| T08 无副作用 | assembler 不接收 ID 分配器/噪声流/Trace；重复预测前后同一次正式 Query 结果相等 |
| T09 无到达 | assembler 测试 NoArrival 正常返回、越界明确失败；provider 混合覆盖测试 |
| T10 标量 | assembler/codec 测试缺失路径时 path_count/excess 为 null，不补零 |
| T11 首周期 | flow_planning_test、真实 E2E 首周期 previous=null；不填 BER=0 |
| T12 因果截断 | Context 校验历史完整提交时间/版本；flow_runtime_test 只有前周期反馈；入口无轨迹接口 |
| T13 观测隔离 | flow_runtime_test 禁用及故障 Trace 的正式反馈完全相同；控制链不读取 SSE |
| T14 统计口径 | flow_runtime_test 目标/旁听分列；A/B E2E 实际比特和 CRC 的差异检查 |
| T15 贯穿身份 | flow_runtime_test 三跳 owner 改变但原 source/FlowId 不变；队列清空且真实交付 |
| T16 同源双流 | flow_planning_test 与 flow_runtime_test 故意优先第二流，不发送第一流队首 |
| T17 条件删除 | flow_planning_test 错 PacketId 拒绝删除，正确删除仅移除所选流；runtime 发布失败留队 |
| T18 空流 | plan_bound_tx_runtime_test 真实执行记录 UnusedSelectedFlowEmpty，无 PacketId/TransmissionId/事件发布，其他流保留 |
| T19 冻结路由 | Context next_hop 不符拒绝；实际 E2E plan/execution 下一跳逐项一致 |
| T20 无未来信用 | flow_runtime_test 每周期仅当前队列所属节点有 demand，中继下一周期才获得需求；预测不分配 PacketId |
| T21 非法计划 | flow_planning_test 未知流/超额/错时/输入指纹改变拒绝；v3 loader 错包/错接口拒绝 |
| T22 安全占用 | 流计划边界测试严格 airtime+全局tail+guard；实际发送也核验尾部不越过 reservation_end |
| T23 上限溢出 | flow_planning_test int64/uint64 溢出、link/grant 预算拒绝；codec 非规范整数拒绝 |
| T24 参数闭环 | flowAlgorithmV2.test.ts、planning_v3_test、C++ v3 loader 与真实 E2E，XML 默认值冻结后供同 C++ 读取 |
| T25 参数行为 | channel_aware/tests/ordering.cpp 权重0/1改变优先流；第二包配额1/2也改变计划 |
| T26 安装发现 | protocolctl check/install + 实际 --algorithm-catalog；源码目录发现不冒充安装 |
| T27 原子发布 | test_protocolctl、algorithm_installation_test：编译失败、冲突版本、受管服务租约不改变 active |
| T28 多文件 | 第二包 src/algorithm.cpp 必须真实参与链接；整个包文件纳入 v2 摘要 |
| T29 独立第二包 | flow_round_robin 经 CLI 安装、真实 A/B E2E，无后台/前端/Runtime 算法专用分支 |
| T30 重放 | codec 同 C++ 对照；两个包各 A/B 两周期导出经实际 Worker 重放 Matched；CLI replay 安装版本成功 |
| T31 随机隔离 | NamedRandomStream 同种子换 RunId 一致，不同 purpose 不同；两个包无私有跨 Run 状态 |
| T32 安全 | XML DTD/实体/未知能力、路径逃逸、symlink/自定义构建脚本、未知参数明确拒绝 |
| T33 模式A | algorithm_v2_e2e_test 真实 API → 发布引用环境/CSV轨迹/场景/实验 → Worker/ns-3 → Completed → 每周期4个实际流安排 |
| T34 模式B | 同一链路真实波形运行，正式历史反馈有比较位数及CRC；不得用理论BER替代 |
| T35 融合因果 | waveform_phy_test 真值隔离与物理损坏 CRC；Channel2 feature/review/degraded 和原融合集成回归 |
| T36 原平台 | 干净 OFF/ON 完整 CTest（内含后台）；全量前端测试及生产构建；额外 Bellhop/水文门禁 |

## 真实运行证据（阶段）

信道排序包：模式A `run-eb80ab536b304e8d8a848c1474739d79`，模式B
`run-777edc4bc32546818f9006b614641424`。独立安装的轮询包：模式A
`run-36afa9e7befd4ff787fb4ed058fd92c0`，模式B `run-321f4db7da5f4a80a6840c9f080f8e17`。
每次两周期共8条实际安排，所有周期输入、计划、执行对应及同C++重放均通过。
完整导出保留于本工作树 `Platform/.runtime/alg02-evidence/`（不提交大日志）。
这些环境通过正式 reference-bellhop-arrivals 导入后重新发布为独立 ID；不是宣称
本测试重新求解了 Bellhop。另有真实 Bellhop 环境门禁单独记录。

轮询包安装 release：`c18e56c4ed099c4b76c70efa6d783e3c2f621a6c8d5fd63f100fffea4323079a`。
安装日志 `/home/ccc/build/alg02-packages/candidate-jmjm__tj/build.log`；保留库
`/home/ccc/build/alg02-installations`。最初该包相对头引用失败，修正包内路径后成功，
失败没有替换活动程序。最终干净构建结果另见 CHECKPOINT。

## 最终干净构建复验

ON Worker SHA256：`3abe91ebf220223902e3d38871b656f22247cca46a371797c1d4a09935f3023d`。

| 包 | 模式A | 模式B |
| --- | --- | --- |
| channel-aware | run-3eb721809ea44ea39d6c0fbaa5cf40be | run-c70c0adfa545400c8d560ecac03ea8cb |
| flow-round-robin | run-c9b719628be04a1aa50a8507381ae49b | run-9881dc945b2b470bb9304456b0181f9d |

四次均 Completed，每次8条安排，全部周期重放 Matched。`samples/` 保存两个模式B
第二周期的完整实录，包含已提交的实际比特比较反馈和发送对应；不是手写或mock数据。
它们用相应包编译出的 Worker `--replay-planning-context <file>` 可重放。
完整工作目录还保留各首周期及汇总；大二进制和测试日志不提交。

干净构建目录 `/home/ccc/build/ns3-bellhop-alg02-clean-off`、
`/home/ccc/build/ns3-bellhop-alg02-clean-on` 的 `Testing/Temporary/LastTest.log`
保存完整门禁输出。环境额外门禁首次漏传 publisher 环境变量失败，补全后57/57通过；
未把这一环境错误记为产品通过，也未修改断言规避。
