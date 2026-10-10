# integration_multifile

基于平台排序教学样例；不是科研协议性能结论。当前使用 planning-mac-v1。
修改 algorithm.hpp 算法主体，XML 仅开放实验参数。节点按真实 NodeId 绑定。
不得 include runtime/internal、操作 ns-3、修改正式队列或使用墙钟随机数。
新增实现可放 src/*.cpp，辅助头放 include/*.hpp；不执行包自带构建脚本。
运行 protocolctl check 后 install；参数变更必须有行为测试。
