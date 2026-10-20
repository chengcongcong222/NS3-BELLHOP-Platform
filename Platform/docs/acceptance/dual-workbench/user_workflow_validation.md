# 实际用户操作验证

执行者：Codex 驱动 Chromium CDP 自动化，真实渲染页面、真实 API、真实 ns-3 Worker；不是人工签字，也不是 mock-only 前端测试。输入夹具提前准备，结果均由运行产生。入口 4197/8097，未操作主服务 4182/8012。

下表的 suite 名对应 `evidence-final/<suite>.json` 和 `screenshots/<suite>-*.png`。后台与数值断言另见科学审计，不用接口成功替代页面操作。

| 编号 | 结果 | 实际操作和证据 |
|---|---|---|
| U01 | PASS | `legacy-final`、`proxy-release`、`paired-coverage`：同一工作台的几何/实录一级入口，旧 mode=C URL 与既有草稿正常打开 |
| U02 | PASS | `legacy-final`：取消切换、保留缓冲区往返、保存后切换；名称及资源保持 |
| U03 | PASS | `paired-coverage`：已有环境/轨迹节点切为跟随场景起点，鼠标拖动；输入越界位置后零 Run 准入，修正后可运行 |
| U04 | PASS | `paired-bearing`、`paired-coverage`：一次“保存并运行”建立 1 组 2 成员和独立定义；新几何实验默认 A+B，打开既有草稿不暗改启动方式 |
| U05 | PASS | `paired-background`：B 仍 Queued 时离开到 about:blank；再打开组页，两成员完成。测试 wrapper 只延迟进程以暴露排队状态，仿真由真实 Worker 执行 |
| U06 | PASS | `paired-background`、`paired-coverage`：捕获页面重跑请求键并并发重发两次，组数不增加、返回同组；后台并发测试另验首次创建 |
| U07 | PASS | `paired-fault`：明确注入 B 进程退出；真实 A 完成并可阅读，B 无结果，不伪造零值。不是制造声学误码来冒充进程故障 |
| U08 | PASS | `paired-bearing`、`paired-coverage`：默认对照，切 B 结果、对照、原配置重跑；冻结定义与成员 RunID 可导出 |
| U09 | PASS | 同上：A 波形 BER/CRC 不适用，B 显示实际错误/比较位数和 CRC；缺判决不填零 |
| U10 | PASS | `paired-coverage`：N0 X 从 11 m 拖至 22 m，再次 A/B 共同快照改变，通信业务清单哈希不变；实际查询使用新位置 |
| U11 | PASS | `archive-final`：正常重启同一测试 backend 后打开完成组、部分失败组、C 组，明确历史事件不可回放；页面下载与真实导出一致 |
| U12 | PASS | `legacy-final`：C1 保存双端点 v4 草稿，重新打开、改名、运行；未转换成 v5。旧 v4 数值另做完整 trace 校验 |
| U13 | PASS | `proxy-release`：显示“实录样本驱动、非同一海试”，画布为逻辑布局，没有公里地理刻度 |
| U14 | PASS | `proxy-release`：添加第三个发送节点，形成三条到接收端的有向物理边；点击边/节点编辑并拖动布局 |
| U15 | PASS | `proxy-pool-two`：选择已导入、校验的 Blue 两阵元池，显示具体文件、版本、起点样本、时延；保存并实际运行 |
| U16 | PASS | `proxy-release`、`proxy-pool-two`：单边覆盖保留，其余继承全局；重开和导出验证唯一对应方向 |
| U17 | PASS | `proxy-release`：全局同 CIR 必须确认人为链路假设；假设进入运行与结果来源 |
| U18 | PASS | `proxy-release`：逐边模式缺映射时明确提示、输入保留；修正后可运行。频带/覆盖/噪声拒绝另由真实预检测试覆盖 |
| U19 | PASS | `proxy-release`：只拖逻辑布局后保存、再运行，场景版本、physics SHA、完整事件均不变 |
| U20 | PASS | `proxy-pool-two`：两个 Tx 错峰、各自 CIR 的真实运行，接收 mixture count=1；噪声按 Rx 显式绑定 |
| U21 | PASS | `proxy-release`：实际两个到达窗口重叠，Reception 产生 signal_count=2，后续解调依据混合波形；数值门另验 |
| U22 | PASS | `proxy-release`：3 个 Tx 分别 BPSK/BFSK/OSDM，共同时钟配置真实运行；不兼容配置由正式预检拒绝 |
| U23 | PASS | `proxy-release`、`archive-final`：Tx 和 Rx 联合筛选有向接收证据；单元测试另验两 Rx 和无判决 BER 不可用 |
| U24 | PASS | `proxy-release`、`proxy-pool-two`：保存重开、另存独立实验副本、原配置重跑；副本 payload 相同，原组不被覆盖 |
| U25 | PASS | 保存编辑器、真实运行页、A/B 对照页的五种指定视口截图；无页面级横溢。窄屏纵向滚动为明确保留行为 |

有向物理边由收发能力完整展开，不能“删掉业务流”来假装声学干扰不存在；关闭某方向业务仍保留物理边，缺 CIR 则拒绝。这是对全 fan-out 合同的实现，不是自动推断反向实测信道。

## 后台与归档 R01–R12

| 编号 | 结果及证据 |
|---|---|
| R01 | PASS：共同冻结比较；浏览器几何越界零 Run；冻结方位清单篡改拒绝 |
| R02 | PASS：并发相同键同组、冲突键拒绝；实际页面请求重发零新增组 |
| R03 | PASS：页面离开不影响队列；重启 Interrupted 单测，真实重启归档页面验证 |
| R04 | PASS：A/B/C 原始组导出保留完整定义、hash 和结果；不从当前草稿替换 |
| R05 | PASS：真实 A + 显式 B 进程失败，A 保留；重跑生成新组/新 ID，不覆盖原失败记录 |
| R06 | PASS：旧单 Run 路由、旧 C1 浏览器及旧 A/B/C 实际 trace 回归 |
| R07 | PASS：v1–v4 合同测试；v5 资源 fail-closed；旧 C1 未转换 |
| R08 | PASS：实际运行后破坏临时资源再重跑被拒；定义/资源 checksum 校验，未知版本不回退 |
| R09 | PASS（范围限定）：既有算法 XML、安装与 SDK 回归通过；v5 未支持注册算法有硬拒绝。可选已安装 v2 包 E2E 未启用，原因见 known issues |
| R10 | PASS：真实连续执行/重跑/重启后运行，单 Worker、异常释放、队列继续；记录子进程峰 RSS 及 backend 测试进程运行前后 RSS |
| R11 | PASS：真实 Reception 的小型 mixture/来源摘要；无完整矩阵；接收判决与真值隔离回归 |
| R12 | PASS：页面下载实际归档，重启前后成员保留；32 MiB 归档预算及原子写入/损坏拒绝/写失败恢复测试 |

所有自动化的失败重试保留在 `evidence-final/negative-*`。不能将这些记录描述为第二位人员的独立人工签字。
