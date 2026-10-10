# v2 输出字段

算法提出 demand_key、start_offset_ns、extra_guard_ns；平台构造 grant_id、
冻结路由引用、reservation_end、cycle/version/context_hash。

每条 grant 最多一个真实包。安全占用按最大编码时长 + 全局尾部上界 + 余量，
使用 checked integer arithmetic；不接受重叠、重复、逆序、越界、未知流或
超过确定性需求预算的安排。空需求的空计划合法。

Runtime 查指定流最早包，核验冻结下一跳，建立一次 Transmission，再登记事件，
最后删除相同 PacketId。无包记 UnusedSelectedFlowEmpty，不发送其他流。
执行对照关联 grant/flow/packet/transmission、计划与实际时间、目标和未用原因。
物理 NoArrival/CRC 失败不是非法计划，不恢复发送端原始数据。

v1 对象不带流约束，仍按原 FIFO 执行；v2 约束不能被未知 loader 忽略。

只读导出记录 Executed / UnusedSelectedFlowEmpty / Rejected；成功发送关联真实
PacketId 和 TransmissionId。`platform.planning-replay.v2` 同时携带完整输入、包 SHA、
计划及执行对照。重放比较计划，不重演物理接收，也不把执行记录当作下一周期控制输入。
