# ADR-0005：轨迹驱动运行输入

- 状态：P0-TRJ-01 用户授权实施，替代 ADR-0001 2.42 中运行时速度积分的位置策略。
- 不改变 ns-3 唯一时间权威、周期 Commit、发送时收发几何采样和 Channel2 准静态 H。

正式运行只查询不可变轨迹绑定；轨迹资源和点数组不进入 WorldSnapshot。
资源使用显式 ID/version、track identity、来源、局部笛卡尔米制坐标（z 向上）、
纳秒时间和有效闭区间。多点轨迹严格递增，线性插值；单点轨迹表示整个显式有效期内固定位置。
越界查询明确失败，不 clamp、不延拓。快照继续保存当前位置及轨迹段导出的速度摘要，
速度不再是 runtime 位置积分输入。所有节点必须绑定，包括静止节点。

新增只读位置查询 contract；runtime 只依赖该接口，不 include CSV、repository 或生成器。
场景装配运行前完成资源加载、节点绑定和初始位置一致性检查。匀速生成属于离线工具，
Acceptance preset 的旧速度字段只作为生成工具输入；外部 CSV 经严格解析后进入同一资源。
旧 internal UpdateVelocity 能力移除；网络事件不得反向修改已绑定轨迹。

CSV v1：track_id,time_s,x_m,y_m,z_m；调用方必须指定坐标系、来源及资源身份。
秒采用十进制定点解析到纳秒，不经浮点时间截断；拒绝重复时间及非法值，按 track/time 排序。
资源注册后 immutable，按精确 ID/version 加载；checksum 仅用于确定性内容比对，不作安全认证。

API 影响：CycleWorkingState 创建及 ScenarioRuntime 装配需提供轨迹查询对象。
WorldSnapshot、通信 DTO、ChannelQuery 与 ns-3 事件 schema 不变。
持久化 catalog、前端轨迹建设、坐标变换、实时流、动力学与网络控制留后续阶段。
