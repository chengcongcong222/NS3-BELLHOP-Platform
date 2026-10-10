# 典型案例系统保留包

这是 2026-10-10 开发冻结后的精简数据集，不是新的算法版本。保存典型资源、实际 Bellhop 产物和必要的历史修订，不包含批量试验草稿、WOA 全量下载缓存或旧构建目录。

## 保留内容

| 内容 | 用途 |
| --- | --- |
| 浅水四节点与六节点预置案例 | 沿用平台内置的明确案例入口，不作为自建实验的回退 |
| 南海浅水参考环境 | 对应正式参考资产及已有校验和 |
| 收口1010 导入声速剖面环境 | 100 m 水深、1 km 距离、25 kHz；5×5×101 采样；保留原始到达文件、声线路径和建设输入 |
| 收口1010 三节点场景与双流实验 | 同源双流、正式轨迹绑定、流轮询教学算法；可在页面切换模式 A/B |
| 三节点 CSV 轨迹 | 保留原始资产身份、版本、轨迹身份和校验和 |

输入环境采用明确的理想平底假设，不冒充实测地形。模式 B 的多径场景允许出现真实误码与 CRC 失败，不预设零误码。

## 本机启动

在当前项目根目录运行：

```bash
./start-platform.sh
./start-platform.sh status
```

已配置主入口 `http://127.0.0.1:4182`，后台 `http://127.0.0.1:8012`。本机配置是 `Platform/.runtime/current-system.env`；存在时，入口使用其指定的保留程序，不会因预检失败自动另建一版系统。

`Platform/.runtime/retained-system/` 包含运行程序、Python 环境、算法安装和活动数据；前端仍使用 `Platform/frontend/dist`、当前唯一的 `node_modules` 和 `.tools`。不再依赖旧 trajectory 工作树。ns-3 系统依赖仍需保留原安装前缀。

运行记录目前仍为进程内存状态：不要随意重启。原记录已归档；恢复数据集不会把历史 Run URL 重新注入运行目录。进入案例后重新运行即可生成本版结果。

## 恢复资源

在已准备好当前平台程序的环境中，可恢复到一个不存在的新目录：

```bash
python3 Platform/scripts/restore_typical_workspace.py /path/to/new-typical-data
```

命令校验 `manifest.json` 中每个文件的 SHA256，再整体落盘；拒绝覆盖已有目录。只重定位建设侧的文件路径，正式声场和轨迹资产字节不变。

随后将服务配置的 `PLATFORM_AUTHORING_WORKSPACE` 指向生成的 `workspace`，`PLATFORM_ENVIRONMENT_REPOSITORY` 指向 `environments`。算法安装必须包含与实验引用摘要相同的 `example.flow-round-robin`；不能拿其他算法替代。配置示例见 `current-system.env.example`。

Git 保存源码、这个约 10 MiB 的典型数据集和操作证据；不保存虚拟环境、Node 解压目录、历史批量资源和二进制安装缓存。本机受测程序另有 D 盘归档，参见 [整理与冻结记录](../../docs/acceptance/development_freeze_record.md)。

新机器仅克隆 Git 后，仍需按 [构建说明](../../BUILDING.md) 准备依赖，并按 [算法安装说明](../../docs/algorithms/integration-v2/package_and_install.md) 安装本实验引用的源码包。这份资源恢复脚本不下载依赖、不替代构建，也不自动启用算法。
