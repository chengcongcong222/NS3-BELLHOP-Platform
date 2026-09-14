# P0-TRJ-01：节点轨迹驱动运行基线

基线：`178b67c066a20ab1320333cf82ce4a06c8d376d6`。
开发分支：`feature/trajectory-runtime-baseline`。不修改 Channel2 冻结分支或 UX/Authoring 工作树。

## 原实现审计

节点正式位置原来保存在 WorldSnapshot 的 NodeCommittedState.motion。
CycleWorkingState / StateProjector 根据锚点位置、速度和仿真时间差执行匀速积分；
UpdateVelocity 会建立新的运行时运动锚点。Application 的 MobilityModel 只有
ConstantVelocity，没有正式运行时随机航路点或动力学求解器。

Acceptance4Node 的节点 10 从 (1000,0,-60)m 沿 +x 移动，节点 20 从
(-950,250,-65)m 沿 +y 移动，节点 30 从 (0,-1050,-55)m 沿 -x 移动，
速度均为 5000/3600 m/s；融合节点 99 固定在 (0,0,-8)m。

网络结构只读取正式快照的位置；特征生成、发送执行器、接收噪声查询只需要
某时刻的位置。因此更换投影入口不需要把模型或轨迹数组传入 PHY、Bellhop 或规划器。

## 新运行链

`离线生成/CSV导入 → Asset → Repository → NodeBinding/Reference → BoundTrajectories`

`ns-3事件时间 → INodeTrajectory.Query(node,t) → StateProjector → 当前状态`

运行时不再执行位置的速度积分，生产 CycleWorkingState 已移除 UpdateVelocity。
它必须收到非空的只读轨迹查询对象，缺节点、缺资源或越界明确失败。
ScenarioRuntime.BindTrajectories 只能在 Ready 时调用，并检查所有节点初始位置与快照一致，
显式允许 1e-8 m 的浮点绝对误差；每周期发布事件前检查轨迹覆盖周期结束时刻。
没有第二时钟、定时线程、实时循环或在线动力学求解。

WorldSnapshot 仍只保存当前正式状态，不保存轨迹点数组。周期结束从轨迹采样并 Commit，
下一次正式网络结构刷新读取这些新位置。既有网络更新间隔没有变成连续重规划。

## 资源和查询语义

- Asset：受限字符的 ID、非零 version、明确坐标系、start/end、来源字符串、track 集合及内容 checksum。
- 坐标系 v1：局部笛卡尔坐标，米制，z 向上；不推断经纬度、海图投影或不同原点之间的变换。
- 时间：非负 int64 纳秒，使用同一个 run 的仿真时间原点，不隐式平移或循环播放。
- Track：独立字符串 identity，不必等于 NodeId；显式 NodeBinding 将节点映射到资源 ID/version/track。
- 同一资源可有多条 track，多个节点也可绑定同一 track；NodeId{0} 合法。
- 多点轨迹要求严格递增、有限坐标且首末点覆盖资源闭区间；不在 Asset factory 内偷偷排序修复。
- 单点轨迹明确表示整个资源有效闭区间内的固定位置；不是运行时运动模型特例。
- 两点间确定性线性插值，精确端点保持原坐标；内部节点的速度摘要取右侧段，最后端点取左侧段。
- 速度摘要只为兼容当前 MotionState 表达，由轨迹段导出，不再作为位置积分输入。
- 有效期外返回 OutOfRange，未绑定返回 NotFound；不 clamp、不推测、不外推。
- Repository 是第一版运行前的内存不可变资源注册表，按精确 ID/version 加载；重复注册拒绝覆盖。
  绑定持有共享只读资源，即使 Repository 析构也保持资源生命周期。
- FNV-1a64 checksum 对规范化 little-endian 字段计算；只用于内容复现/意外损坏对比，不是认证或签名。

TRJ-02 在 authoring plane 增加原子 JSON 持久化 catalog 与 HTTP 生成/导入 API。
它们生产相同的 `LocalCartesianMetersZUp` 资源；Runtime 查询接口仍不包含文件、HTTP 或生成模型。

## Acceptance4Node 迁移

正式 AcceptanceRunExecutor 在运行开始前，把初始配置交给离线匀速生成器，
按本次实验通信周期时长 × 周期数计算经过溢出检查的有效期，生成并绑定统一资源。
移动节点生成端点轨迹；静止融合节点生成固定轨迹。同一装配覆盖 Channel1 和 Channel2。
旧场景字段 ConstantVelocity/initial_velocity 暂保留为兼容的**轨迹生成输入**，
不再选择 runtime 运动模型。本轮没有重写 worker/frontend schema。

新增回归检查 t=0、4、12、24、120、144 s 时的四节点位置，
与旧公式相比误差不超过 1e-9 m；原正式场景、特征融合和 Channel2 验收仍必须通过。

Bellhop 的 ChannelQuery 仍在 TxStart 采样发送和接收节点位置，取自同一轨迹驱动
ProjectNodeState；不迭代求解传播期间的移动接收点。噪声查询按首达时间采样接收位置。
Channel2 每次接收使用一个准静态 H、无多径均衡等冻结限制保持不变。

## 外部 CSV 边界

```csv
track_id,time_s,x_m,y_m,z_m
N10,0,1000,0,-60
N10,10,1013.888,0,-60
N10,20,1027.776,0,-60
```

ImportCsv 接收文本与显式 ID、version、CoordinateSystem、source，输出标准 Asset。
调用方在正式运行前读取文件；runtime 不 include 或解析 CSV。
CSV v1 是严格的无引号五列子集，支持 LF/CRLF，最大 16 MiB；不静默忽略空行/多列。
time_s 是最多九位小数的非负十进制秒，直接转换纳秒，拒绝指数、溢出和亚纳秒精度。
坐标按米解析并拒绝 NaN/Inf/非法尾部。导入器按 track/time 排序，但重复时间拒绝。
CSV 多 track 必须共享首末时间；静止 track 可以在公共首末时间重复位置。
单行 CSV 只有该时刻的覆盖，不能暗中无限延伸。

接入步骤：ImportCsv → Repository.Register → 用 NodeBinding 构建 BoundTrajectories
→ ScenarioRuntime.BindTrajectories → RunCycles。无需修改 runtime 来识别 MATLAB、Python、UUV 或实测来源。

## 回归和复现

新增长期测试：

1. platform_trajectory_asset_test：精确点、插值、静止/共享轨迹、非法序列、越界、版本、CSV、checksum 与确定性。
2. platform_trajectory_geometry_test：Acceptance4Node 位置等价；CSV 折返轨迹驱动正式 Bellhop 查询及提交位置。
3. platform_ns3_trajectory_geometry_test（ON）：由真实 ns-3 事件触发上述几何查询并核验当前内核时间。

几何反例故意把旧 velocity 设为 999 m/s，但导入 track 的 x 在 0/5/10/15 s
依次为 1000/1150/1300/1150 m；Bellhop 实际收到后者，证明没有回退到旧速度模型。

历史底层测试使用 tests/trajectory_fixture.hpp 在测试准备阶段生成折线资源，
保留原队列、通信、Commit、错误与溢出断言。该夹具的 UpdateVelocity 只编辑测试输入资源，
不进入生产 runtime；生产类另有编译期断言禁止恢复 UpdateVelocity API。
旧“查询早于最后速度锚点必须失败”的断言改为按已生成的完整轨迹正常回溯查询；
负资源时间改为运行前拒绝。发送端时间溢出测试显式提供足够轨迹覆盖，未削弱原溢出断言。

```bash
cmake -S Platform -B /home/ccc/build/trajectory-20260911-off -G Ninja \
  -DBUILD_TESTING=ON -DPLATFORM_ENABLE_NS3=OFF
cmake --build /home/ccc/build/trajectory-20260911-off -j 4
ctest --test-dir /home/ccc/build/trajectory-20260911-off --output-on-failure
cmake -S Platform -B /home/ccc/build/trajectory-20260911-on -G Ninja \
  -DBUILD_TESTING=ON -DPLATFORM_ENABLE_NS3=ON -DCMAKE_PREFIX_PATH=/home/ccc/.local/ns3/3.47
cmake --build /home/ccc/build/trajectory-20260911-on -j 4
ctest --test-dir /home/ccc/build/trajectory-20260911-on --output-on-failure
```

## TRJ-02 生产端

轨迹建设页提供静止、匀速直线、线性航路点、UUV 5-DOF 与严格 CSV 五种入口，
生成后立即保存为带版本和 SHA-256 的正式 Asset。页面只预览 Asset 的 XY、深度-时间和
时间采样位置，不把生成配置保存到 Scenario 或带入 Runtime。

UUV 状态为 `[north,east,down,pitch,yaw,u,v,w,q,r]`。运动学使用无横滚的 NED
五自由度变换；Plant 使用有效质量（刚体与附加质量的合并参数）、科氏/向心耦合、
一次/二次阻尼、重浮力恢复、控制与可选扰动。连续方程只在离线生成器中用确定性固定步长
RK4 积分。`IUuvController` 与 `Uuv5DofPlant` 分离，第一版包括恒定直接输入和简单的
航向/深度/速度航路点控制器，不包含 FTESO/NMPC。

生成边界显式执行 `platform.x=north, platform.y=east, platform.z=-down`；Runtime
内部不做坐标猜测。姿态与体坐标速度作为可选扩展样本保存，既有 Runtime 仍只依赖
`t,x,y,z`。默认参数明确命名为 Demo preset，仅用于确定性产品演示，不代表任意真实 UUV，
也不声称来自未纳入仓库的具体论文参数组。

本轮不实现 ROS、在线动力学、网络反向控制、实时流、FTESO/NMPC 或 Channel2 新算法。

2026-09-11 独立新构建目录最终验证：OFF 67/67 PASS，ON/ns-3.47 101/101 PASS。
其中原 Channel2 冻结的 7 项门禁全部保留并通过；轨迹、内核冒烟、Channel1、Channel2
重点重跑共 14/14 PASS。C++23 与 contracts/runtime 无 ns-3/CSV 依赖保持不变。
`git diff --check` 通过。主工作区 UX/Authoring 文件未修改，未合并 integration。
