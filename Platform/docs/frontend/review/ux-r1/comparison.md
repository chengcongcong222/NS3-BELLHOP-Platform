# P0-UX-R1 实机视觉对照

采集日期：2026-09-01
窗口：Microsoft Edge，1440 × 900
系统：旧版 `NS3_Factory`（只读源码与既有数据，端口 4174/8001）；新版 R1（commit `2c708101eda3f99bee75b9960e0beed8cc84dc3e`，端口 4173/8000）。

截图均来自实际运行页面。旧版没有 Environment detail/Profile、Run 阶段快照和 Result drill-down 的独立路由，相应编号保留最接近的真实 Environment、Studio、Monitor 或 Results 页面；没有改数据或伪造运行状态。

| 画面 | 旧版做对了什么 | R1 做对了什么 | R1 明显弱项与下一步 |
|---|---|---|---|
| `01-home.png` | 高密度入口和软件化深色外壳，当前工程上下文明确。 | 首屏解释平台、工作流、案例和最近运行，普通用户更容易开始。 | R1 顶部横向导航占宽、首页仍偏门户卡片；保留任务入口，收紧为桌面工作台 shell。 |
| `02-environment-list.png` | 环境库、引用关系、构建入口集中，维护动作接近真实工作流。 | Published 资源边界、格式、覆盖和校验状态准确。 | R1 是资源卡片目录，缺少环境建设上下文；改为紧凑资源栏 + 空间预览。 |
| `03-environment-detail.png` | 同一环境页可以继续进入构建参数，操作成本低。 | SSP、Bathymetry、覆盖、来源信息分层准确。 | 详情视觉仍像报告；用海面/海底/深度/覆盖联动形成声学剖面工作区。 |
| `04-environment-profile.png` | Studio 的纵剖面、声线、节点和仿真控制处于连续空间。 | R1 不显示正式数据中不存在的射线、TL 或 SNR。 | R1 Profile 与场景分离；增加选中深度/链路语境，正式路径存在时才显示。 |
| `05-scenario-workspace.png` | 中央画布占据绝对主空间，顶部工具、右侧属性、底部状态密度成熟。 | R1 已有 N 节点、拖动、精确坐标、深度和能力编辑。 | R1 画布只是页面中的 panel，缺左工具栏、距离状态和环境边界；改为三栏一状态条。 |
| `06-node-selected.png` | 选中对象与右侧属性天然同屏，工作空间不中断。 | R1 节点选择会同步属性，并保持强类型字符串 ID。 | R1 只能单选、链路不可选；增加双节点/链路选择、距离与剖面同步。 |
| `07-experiment-configuration.png` | Studio 顶栏能快速切到场景控制，参数密度较高。 | R1 按场景、网络、通信、执行分组，并有即时校验。 | R1 纵向表单滚动较长；改为紧凑分区、固定目录和摘要侧栏。 |
| `08-run-monitor-start.png` | 运行入口、时间轴、速度和画布位于同一工作台。 | R1 lifecycle、simulation time 和事件完全来自正式 Backend。 | 旧版此快照只有等待启动日志；R1 完成态信息更真，但没有事件回放。实现 Completed Run 的 Trace 回放。 |
| `09-run-monitor-active-link.png` | 旧版有统一 Monitor 空间，但该真实快照没有可用活动链路，只显示等待日志。 | R1 能从正式有序事件恢复 N30→N20 链路。 | R1 只突出最后边，且状态语义弱；增加发送/传播/接收/NoArrival 状态、时间轴和选择联动。 |
| `10-run-monitor-statistics.png` | 背景可见高密度画布和右侧属性区。 | R1 有正式发送、Signal、NoArrival、Reception、周期与事件计数。 | 统计平铺为同级 KPI 卡片；按运行、通信质量、网络/融合分层。 |
| `11-result-overview.png` | 历史结果、比较和导出入口集中。 | R1 结果目录独立于运行目录，使用正式 Result availability。 | R1 目录信息密度偏低；增加案例/场景语境和关键结果摘要。 |
| `12-result-detail.png` | 旧版单页包含结果图表和历史分析入口。 | R1 通用网络/信道/融合先于 Acceptance，verdict 不在前端重算。 | R1 仍是 panel 集合；改为结果导航 + 总览画布 + 分层分析与证据钻取。 |

## 总体判断

旧版“差点劲”的反例不在单个图表，而在连续 Studio：工具、画布、属性、时间和状态共享一个视野。R1 的产品结构、正式数据边界和中文任务语言明显更好，但页面组合仍偏网站/卡片。R2 应保留 R1 的 Case workflow 与正式事件语义，把 Environment、Scenario、Run、Result 收紧为高密度桌面工程工作区。
