# 流轮询教学包

独立多文件 `planning-mac-v2` 示例。入口头文件、`src/algorithm.cpp`、XML 参数和
行为测试全部属于本包，安装不需要修改后台、前端或 Runtime。
`src/` 使用相对路径引用本包入口头，避免多包同名 `algorithm.hpp` 串包。

按 `(queue_owner_node_id, FlowId)` 顺序，每轮安排 `packets_per_turn` 个已知包。
只安排冻结路由下一跳存在且预测可达的流；安全占用仍使用真实编码包时长加全环境
传播尾部。单周期内轮询，无跨周期轮询游标，不宣称长期公平性或科研性能优势。
同源同目的的不同 FlowId 仍独立调度；首周期不需要历史反馈。

```sh
python3 Platform/tools/protocolctl.py check flow_round_robin
python3 Platform/tools/protocolctl.py install flow_round_robin
python3 Platform/tools/protocolctl.py activate
```

先按集成指南配置工具链；受管后台持有租约时不会切换其程序。
XML 是默认参数权威，测试证明配额 1/2 产生不同流序列，并检查重排、RunId 和非法参数。
