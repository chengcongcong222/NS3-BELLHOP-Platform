# P0-ALG-02 算法接入

第三方只编写算法主体、XML 与包内测试；平台提供真实只读输入并负责发送执行。
不是通过前端脚本模拟结果，也不引入另一套时间轴。

- [从新建到安装、运行、重放](user_guide.md)
- [输入字段与缺失语义](input_contract.md)
- [输出约束与实际执行对应](output_contract.md)
- [安装工具、版本保留和启用](package_and_install.md)
- [兼容边界](compatibility.md)
- [集成决策](DESIGN_DECISIONS.md)
- [逐项验证记录](validation_report.md)
- [当前检查点](CHECKPOINT.md)

公开 C++ 入口：`Platform/planning/include/ns3_factory/planning/sdk.hpp`。
两个独立 v2 教学包：`algorithms/examples/channel_aware`（损失/时延权重排序）和
`algorithms/user/flow_round_robin`（多文件流轮询）。均不宣称科研性能优势。
