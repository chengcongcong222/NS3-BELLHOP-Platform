# 浅色科研工作台：真实页面证据

日期：2026-09-29。1440×900、100% 缩放，真实 Chromium 窗口截图；使用隔离的 Backend 资源目录，未修改用户现有环境和实验。
后端为本工作树源码，C++ Worker 位于 `/home/ccc/build/ns3-bellhop-exp01-on`，Bellhop 为本机 `bellhopcxx2d`。源码基线及完整门禁见 [实施记录](../implementation.md)。

## 页面

- [工作台](./01-workbench.png) · [环境目录](./02-environments.png)
- [多径冲激响应](./03-impulse.png) · [非相干传输损失](./04-loss-field.png) · [实际声线](./05-rays.png)
- [环境输入](./06-environment-inputs.png) · [实验工作台](./07-experiment.png) · [节点内轨迹弹窗](./08-trajectory-dialog.png)
- [模式 A 看板](./09-runtime-mode-a.png) · [模式 B 看板](./09-runtime-mode-b.png) · [强噪声看板](./09-runtime-mode-b-noisy.png)
- [发送](./10-mode-b-transmission.png) · [实际到达](./10-mode-b-signalarrival.png) · [接收成功](./10-mode-b-reception.png) · [接收失败](./10-mode-b-noisy-reception.png) · [融合](./10-mode-b-fusion.png)
- [模式 A 结果](./11-results-mode-a.png) · [模式 B 结果](./11-results-mode-b.png) · [强噪声结果](./11-results-mode-b-noisy.png)
- [打印样式与灰度画布](./12-runtime-print.png) · [实际打印 PDF](./runtime-print.pdf)

数字、资源 SHA 和每个实验的完整 Result 保存在 [real-inputs-and-results.json](./real-inputs-and-results.json)。截图中的实验是脚本通过正式 Authoring API 新建后实际执行得到，不是内置 UI mock。多次检查留下的测试资源仅位于 `/tmp` 隔离 workspace。

## 可重复验证

先以独立端口启动本树 Backend 和 Vite，给后端设置真实 Worker/资源适配器/轨迹生成器/环境发布器/Bellhop 路径及独立持久化目录，再启动本机 Chromium CDP。可执行：

```bash
Platform/frontend/.tools/node-v24.20.0-linux-x64/bin/node \
  Platform/frontend/scripts/verify-research-workbench.mjs \
  http://127.0.0.1:4199 http://127.0.0.1:8019 http://127.0.0.1:9239 \
  /tmp/research-ui-evidence
```

这个脚本会在所指 Backend **创建真实测试资源和运行**，必须使用隔离目录，不应对用户日常工作目录反复执行。不会清理或删除资源。
脚本验证：真实 Bellhop 图非空、模式 A/B/强噪声三次 Completed、轨迹弹窗可进入、浅色画布、默认日志裁剪、切换筛选不改变统计、观看前后 Result 完全相同。
模式 B 强噪声没有融合截图，因为该真实实验没有融合事件。
