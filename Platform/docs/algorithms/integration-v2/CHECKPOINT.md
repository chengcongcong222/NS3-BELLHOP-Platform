# P0-ALG-02 checkpoint

- 基线：ebd7b1965e011ddaa2fb671305275f97eebec65d。
- 分支：feature/algorithm-integration-v2，独立工作树。
- A0：完成源码边界核对与 ADR-0010、输入/输出、兼容决策；无运行行为修改。
- A1：已实现统一工具、候选构建、多文件包、独立安装与租约启用。真实两个 v1
  教学包编译进入 Worker，后台目录验证通过。详见 package_and_install.md。
- A1 历史门禁：XML 3 项、工具 4 项、后台目录/安装/网关 27 项通过。
- 独立安装根的最后程序：69ef26cc0c6d44700cb3d9e6c91574df0553e476079f5eac89907f1827128edb。
- A2/A3 核心基础已实现：共享无通信身份采样、值拥有只读上下文、原结构来源、
  流内最早包选择及条件删除、跨跳 FlowId、仅成功提交后发布的正式周期反馈。
  ns-3 集成夹具验证三周期逐跳转发；Trace 故障不改变后续正式反馈。
- 已增加 v2 XML 独立注册表及信道排序教学包，权重改变确实改变流顺序；
  v1 包摘要/入口保持不变。命名随机流不使用 RunId 或墙钟。
- A2/A3 已补齐规范 JSON 导出、同 C++ 重放、逐 grant/flow/packet/transmission
  对应。诊断导出和正式周期反馈分离，诊断失败不改变运行；预算失败明确标记。
- A4 已补齐 authored-run-v3 冻结、前后端稳定流身份和通用 XML 参数；旧接口保持。
  第二包 flow_round_robin 为独立多文件 v2 包，真实 CLI 安装并完成 A/B 运行与重放。
- A5：全新 OFF 构建完整 CTest 75/75；全新 ON 构建完整 CTest 112/112。
  ON 后台子门禁 328 passed / 5 skipped；这5个依赖条件跳过项已通过另外提供真实
  Bellhop/科学依赖的57项环境水文测试，以及两个算法的显式 A/B E2E 补跑覆盖。
  OFF 后台 323 passed / 10 skipped，其中运行类门禁由 ON 执行，不算 OFF 通过。
- 全量前端31文件268项通过；生产构建成功（原有单块>500kB体积警告保留）。
  Starlette HTTP_422 常量弃用警告保留，无功能失败。`git diff --check` 通过。
- 最终干净 ON 的两个算法各跑 A/B，两周期各8条安排，全部 Completed；每周期计划
  与实际发送逐项一致，全部输入经同 C++ 重放 Matched。样本及运行ID见 validation_report。
- 本轮额外发现并修复：ns-3夹具误注册到OFF、CMake Python变量作用域、
  新事后误码统计引起旧整体相等测试不适用；保留并加强了解调真值隔离断言。
- 现有主工作树和服务不动；不使用旧测试次数作为本轮通过证据。
- 最终实现提交 `292b3dbe6f776b6e45c3842b4fcb11ac10e91cab` 已从干净源码安装，
  release `57a26dc3f6152846c2a45c0a9e236b1a322e38f95e7b09c2ab92318c374b8fcf`；
  manifest.source_dirty=false，正式CLI重放提交样本 Matched。
- 下一步：远端发布授权及联合审查；尚未部署到普通用户服务。
- UI通用参数适配有自动化测试；本轮未新增浏览器截图，用户易用性仍需联合复核。
- 阶段普通 push 被权限审核拦截，已向用户请求确认新分支的代码/文档发布；本地继续。
