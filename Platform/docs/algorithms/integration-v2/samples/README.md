# 可重放实录

两份文件均来自 `algorithm_v2_e2e_test.py` 的真实 Backend/Worker/ns-3 模式B实验，
是第二周期完整输入、输出和实际执行对应，不含用户真实业务或私密资源。

- channel-aware-mode-b-cycle-1.json：信道加权排序包。
- round-robin-mode-b-cycle-1.json：独立多文件轮询包。

构建对应精确包（版本及包 SHA 在文件中）后执行：

```sh
/path/to/platform_sim_worker --replay-planning-context /path/to/sample.json
```

正式安装的包也可用 `protocolctl.py replay`，见上级安装指南。Matched 比较计划；
不是重新运行物理仿真。执行关联查看 executions，前周期实测比较查看 context.previous。
未知字段、错误指纹或不同包均会拒绝，不能拿不同算法的结果冒充重放。
