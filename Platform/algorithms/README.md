# 规划型 MAC 算法接入指南

第三方算法在服务器上编译为 Worker 的一部分。XML 自动生成工作台参数界面；普通用户不需要修改源码或 XML。当前接口为 `planning-mac-v1`，适用于集中式、无跨周期私有状态、单信道串行规划，不适用于反应式 CSMA 或未经建模的并发 CDMA。

## 最小包

参考 `examples/ordered_mac/`。它是接入教学样例，不是默认协议或性能优化算法，默认不安装。

```text
my_mac/
  algorithm.xml
  algorithm.hpp
  helper.hpp       可选，本包其他头文件
```

`algorithm.hpp` 导出 XML 中的 `entry` 指定函数。只包含公开的 `ns3_factory/planning/mac_algorithm.hpp` 和 contracts；不直接访问 Runtime、旧系统 store、ns-3 scheduler 或 Bellhop。函数输入是只读快照、PlanningInput、周期长度和参数，返回 `Result<vector<TxOpportunity>>`。

每条发送机会只指定发送节点和可发送时刻，不虚构接收成功。路由、业务目的节点和物理接收仍由既有链路处理。平台核验输出节点、时间和实际物理占用；算法失败直接失败，不回退默认计划。场景前提检查放在主体函数开头，并给出具体错误，例如“该算法至少需要两个可发送节点”。

## XML 参数

```xml
<algorithm id="example.my-mac" version="1.0.0" interface="planning-mac-v1"
           label="我的调度算法" entry="my_mac::Build">
  <description>适用场景和工作方式，控制在一两句话。</description>
  <parameter id="gap_ns" label="额外间隔" type="duration_ns" unit="s"
             default="0" min="0" max="60000000000" advanced="true"
             help="在物理占用之外预留的时间。"/>
</algorithm>
```

支持 `integer`、`number`、`duration_ns`、`boolean`、`enum`。所有保存值都是字符串，整数不能带前导零，布尔值只能为 `true`／`false`。数值字段必须声明上下界；默认值必须在范围内。枚举用 `<option value="用户可读选项"/>`，不执行表达式。纳秒字段用整数保存、界面以秒编辑，单位须为 `s`。

第一版最多 32 个参数、32 个枚举项，XML 最多 64 KiB。这些是配置复杂度／输入保护限额，不是协议物理极限。禁止 DTD、实体、脚本、XML 注释和未知标签。第一版仅支持基础／高级分组，不支持条件表达式或任意网页模板。

ID 和版本共同标识包。同 ID 可注册不同版本，但主体需要不同 C++ 命名空间／入口，防止符号重定义。同 ID、同版本重复安装在构建时失败。修改默认值、参数含义或实现时升级包版本；即使忘记升级，源码摘要变化也会阻止旧配置被默默替换。

## 编译安装

在已有可用的 ON 构建目录中指定额外包的绝对路径，多个目录用分号分隔。以下路径只是示例，保留自己的 ns-3 构建设置：

```bash
cmake -S Platform -B /path/to/build-on \
  -DPLATFORM_MAC_PLUGIN_DIRS=/absolute/path/to/my_mac
cmake --build /path/to/build-on --target platform_sim_worker -j 2
/path/to/build-on/worker/platform_sim_worker --algorithm-catalog
```

重新启动新版本后端服务，打开工作台“网络 → MAC”。`GET /algorithms` 返回实际 Worker 目录，页面自动出现算法及私有字段。安装失败不能靠修改前端列表绕过。不要直接改动正在使用的 Worker；先在独立服务验证，再切换访问入口。

编译器只对包内头文件做源码摘要；外部依赖仍由管理员构建系统管理。完整 Worker 摘要用于冻结和重跑校验，因此外部依赖或核心实现变化也不会被当成完全相同的实验程序。当前不自动保存多版本 Worker，历史版本缺失时明确拒绝按原配置重跑。

## 测试与发布

```bash
python3 Platform/algorithms/test_compile_catalog.py
ctest --test-dir /path/to/build-on \
  -R 'platform_(planning_adaptive_planning|algorithm_xml_catalog)_test' --output-on-failure
```

研究包至少提供：合法／非法配置、适用／不适用场景、相同输入重复结果、空队列、积压队列、溢出、无可发节点和超过周期边界测试。正式发布还需运行真实资源绑定的模式 A/B 实验，检查计划与发送事件对应关系，而不是只检查页面能显示参数。

仓库的 `backend/tests/product_chain_e2e_test.py` 在实际安装 `examples/ordered_mac` 且设置 `PLATFORM_EXPECT_EXTERNAL_MAC=1` 时，额外验证它的“降序”参数确实改变发送顺序。其余环境变量与现有产品链 E2E 相同，见测试文件。

算法主体是受信任原生代码，接口只读不等于安全沙箱。管理员须审核代码；不允许网页上传后直接执行。复杂有状态、局部信息协议和角色选举需先扩展正式执行合同，不应把隐式状态塞入全局变量。
