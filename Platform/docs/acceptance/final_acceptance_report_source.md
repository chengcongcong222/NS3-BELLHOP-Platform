# Final Acceptance Report Source — P0-S5-05

## 系统做了什么

平台把版本化 Scenario/Experiment、Reference acoustic environment 和 application workload 组合为
可重复的 ns-3 离散事件仿真 Run，输出 typed Trace、formal Result、FusionResult 和
AcceptanceEvidence。P0-S5-05 对齐最终现场验收表达和交接材料。

## 如何实现

- FastAPI 管理单 active Run 的操作/API 边界；C++ Worker 隔离 simulation process。
- RunService/ScenarioRuntime 组合 M3/M4 plans、M5 PHY、Environment Channel Provider 和 M8 Trace。
- `ns3::Simulator` 是唯一 simulation clock 和 event scheduler；M1/Ns3KernelGateway 是唯一
  Platform scheduling access boundary，不是另一套 scheduler。
- Reference environment 在开发资产链中离线经 Bellhop/parser/normalizer 生成，运行时只查询
  immutable AcousticFieldAsset。
- AcceptanceRunReport 产生六项 verdict；Frontend、Evidence matrix、文档均只引用，不重算。

## 如何验证

验证层包括 C++23 OFF/ON CTest、kernel smoke、event dispatcher、signal lifecycle、sequential Run
time reset、Reference/Bellhop parser/normalizer/repository tests、Backend Python、Frontend tests/build、
release relocation/offline/source-independent、integrity negative gates、three-run determinism 和 14-step
operator dry-run。最终数量、hash 和制品 identity 由 handoff `final_release_record.md` 给出。

## 实际结果

Acceptance4Node Golden baseline：4 nodes、60 bit/s、6 transmissions、18 Signal、18 Reception、
6 local deliveries、6 bearing observations、24 s fusion period、Modeled BER、overall Pass。六项要求是
3–4 nodes、60 bit/s、BER ≤ `1e-4`、feature-level fusion、bearing observations ≥ 5、fusion period ≤ 180 s。

## 证据性质

- Requirement authority：`acceptance4_baseline_v1.json`。
- Release identity：P0-S5-05 / `@SOURCE_REVISION@` / Linux x86_64。
- Actual：captured Result/AcceptanceEvidence。
- Verdict：Backend AcceptanceRunReport。
- Environment：Reference/proxy modeled environment。
- Propagation：Bellhop-derived propagation。
- BER：Modeled；不是 hardware measurement，数值 `0.0` 可能是浮点表示下限。
- UI：展示层，不是最终数据权威。

## Release 区分

P0-S5-03 是首个 canonical offline runtime release；P0-S5-05 是 final acceptance-aligned release
candidate/baseline，现场默认使用 P0-S5-05。历史 S5-03 identity 不覆盖、不重写。

## 限制

站点实测水文、hardware source-level calibration、hardware/measured BER、HIL、完整 waveform-level
hardware chain、Windows native、durable storage、authentication 为 TBD。当前外部前提是明确的
ns-3.47 Linux x86_64 runtime prefix。
