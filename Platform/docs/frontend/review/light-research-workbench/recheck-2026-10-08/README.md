# 12 条意见复核：真实浏览器证据

日期：2026-10-08。本工作树源码、重新构建的 ns-3 Worker、真实 Bellhop 和 Backend API；没有拦截请求或替换运行数据。浏览器为本机 Linux Chromium 的无界面渲染，中文字体只读使用本机字体，缓存位于 `/tmp`。本目录不覆盖 2026-09-29 的历史证据。

## 实际结果

三次运行均 Completed，仿真时间 36 s、2 个周期、6 次发送。下表误码及校验仅统计预期接收节点的 6 帧，不混入旁听结果。

| 实验 | 目标交付 | 融合 | 比较比特 | 误码 | 校验通过 / 失败 |
|---|---:|---:|---:|---:|---:|
| 模式 A | 6 | 1 轮、6 个有效观测 | 不适用 | 不适用 | 不适用 |
| 模式 B、低噪声 | 6 | 1 轮、6 个有效观测 | 720 | 0 | 6 / 0 |
| 模式 B、强噪声 | 0 | 0 | 720 | 368 | 0 / 6 |

零误码只表示这 720 个已比较比特未出错，不作为理论误码上限或自动验收结论。所有输入、资源身份、校验和及完整结果见 [real-inputs-and-results.json](./real-inputs-and-results.json)。观看、筛选前后 Result 完全一致。

## 页面与打印

共 38 张 PNG、一个打印 PDF。1440×900、1920×1080、2553×1264 均使用 100% 缩放；12 项屏幕布局检查无页面横向溢出，打印检查未发现所检验面板的内部裁剪。量化检查见 [layout-checks.json](./layout-checks.json)。打印样式微调后，另以同一真实运行补拍打印截图和 PDF，没有为此重跑或修改仿真。

- [冲激响应](./03-impulse.png)、[距离—深度损失](./04-loss-field.png)、[真实声线](./05-rays.png)。
- [单接收深度距离曲线](./15-1440-single-depth.png)：另建真实 Bellhop 环境验证；无到达点不连线、不记为零损失。图注保留打印时可读的采样位置和单位。
- [1440 运行看板](./16-1440-runtime.png)、[1920 运行看板](./16-1920-runtime.png)、[2553 运行看板](./16-2553-runtime.png)。理论 BER 保持一行，不拆断指数。
- `10-mode-*-*.png`：真实发送、到达、接收及融合事件。强噪声运行没有融合事件，因此不生成融合截图。
- [打印预览](./12-runtime-print.png)、[完整分页 PDF](./runtime-print.pdf)：画布灰度、标签可辨，长日志进入后续页面，不截为一个固定高度窗口。

## 复核方式

隔离数据：`/tmp/platform-research-recheck.tEM1Dt`。Backend / Frontend / CDP 端口分别为 8121 / 4321 / 9341；本次创建的服务已停止，未修改日常工作区资源或停止其他服务。Worker、catalog adapter 和发布器来自 `/home/ccc/build/ns3-bellhop-exp01-on`；Bellhop 来自本树 `Platform/.runtime/tools/bellhopcuda/bin/bellhopcxx2d`。

在同样的隔离服务下，实际使用的主命令为：

```bash
PATH="$PWD/Platform/frontend/.tools/node-v24.20.0-linux-x64/bin:$PATH" \
VITE_API_BASE_URL=http://127.0.0.1:8121 \
  npm --prefix Platform/frontend run build -- \
  --outDir /tmp/platform-research-recheck.tEM1Dt/frontend-dist
Platform/frontend/.tools/node-v24.20.0-linux-x64/bin/node \
  Platform/frontend/scripts/verify-research-workbench.mjs \
  http://127.0.0.1:4321 http://127.0.0.1:8121 http://127.0.0.1:9341 \
  Platform/docs/frontend/review/light-research-workbench/recheck-2026-10-08
```

运行脚本会通过正式接口创建资源，必须指向隔离 Backend。声学专项前端 8/8、后端 15/15（含实际辅助声线计算）通过；production build 通过；`git diff --check` 通过。前端构建仍有单 bundle 大于 500 kB 的非阻断提醒。

## 边界

- 这些是实际运行完成后的事件观看证据，没有人为延时伪装正在运行状态。
- TL 是到达幅度平方求和的非相干损失，不是相干 `.shd` 连续声场；声线仅展示产物实际保存的单一发射深度。没有产物时不合成声线。
- 本轮补了单深度显示、零能量损坏产物拒绝、采样来源标注；尚未为大网格加入跨请求解析缓存或分块传输。
- 本组真实实验是多节点向一个指定业务接收点发送，不作为多接收点业务、动态角色选举或波形重叠干扰的完成证据。
