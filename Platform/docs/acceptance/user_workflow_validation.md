# P0-ALG-02 收口操作验证

状态：安装修复已验证；页面验收待执行，不能据此冻结。执行者为 Codex 自动化，不是人工验收签字。
基线：`d72b3a8bda9eba69dd199eec0508e45f1c3af6a7`。
安装侧受测源码：`dbce6c4849fd8197dbcfac0108e312aebcedb094`。

| 编号 | 本轮实际操作与结论 |
| --- | --- |
| U01 | 未执行新版本页面验证：主入口待确认。 |
| U02 | 未执行案例 → 通道1 → 结果页面操作。 |
| U03 | 未执行文件导入、预览、环境构建发布页面操作。 |
| U04 | 未执行轨迹创建、绑定、保存重开页面操作。 |
| U05 | 未执行算法切换、修改参数、保存重开页面操作。 |
| U06 | 未执行通道2页面运行与误码/CRC核对。 |
| U07 | 未执行播放、单步、暂停、时间轴页面操作。 |
| U08 | 未执行页面重复实验与结果追溯。 |
| U09 | 未执行页面非法输入修正恢复。 |
| D01 | 通过。按现有 `new closeout_probe` 创建 v2 包，只修改包名称、时延权重默认值为 0.75、补默认值行为断言；`check closeout_probe` 实际 C++ 编译通过。无核心登记修改。 |
| D02 | 部分完成。实际 `install closeout_probe`，不启用，再 `install flow_round_robin`，真实程序目录包含两包和旧包；尚未启用并在主入口下拉框验证。B 为已有包重装，不声称两个包都是首次安装。 |
| D03 | 部分完成。候选程序重放仓库现有 `round-robin-mode-b-cycle-1.json`，返回 `Matched`；本轮尚未通过页面建立同源双流并核对发送。重放不是重跑物理仿真。 |

## 安装证据

- `python3 Platform/tools/test_protocolctl.py`：12 项通过。连续安装有/无活动版本、坏候选不回退、版本冲突、第二次编译失败、发布失败、保留源码损坏、启用占用/失败、历史切换保留另一候选、同值指针及状态分类。累积发布单元测试使用构建替身；以下两次安装才是真实 Worker 构建证据。
- 后台 `algorithm_installation_test.py`、`algorithm_catalog_test.py`：13 项通过。
- ON 构建目录的相关 CTest：6/6 通过（含上述 protocolctl 测试，不重复计数）。
- `git diff --check`：通过。
- A 构建日志：`/home/ccc/build/alg02-packages/candidate-apj1gg_6/build.log`。
- B 构建日志：`/home/ccc/build/alg02-packages/candidate-w6hph7xg/build.log`。
- 两次真实构建得到相同最终 Worker SHA：`dd21c42db0468afce60d378074be5377de7f2f1c87f0768efbeabc2bdb0174c7`。
- 安装清单：`/home/ccc/build/alg02-installations/releases/dd21c42db0468afce60d378074be5377de7f2f1c87f0768efbeabc2bdb0174c7/manifest.json`。
- `list` 实际显示源码 Discovered、唯一候选 InstalledPendingActivation、活动 Active、旧版本 Retained；没有把所有旧版本都叫待启用。

没有新端口、新服务、服务热替换或旧服务截图冒充新版本验收。
