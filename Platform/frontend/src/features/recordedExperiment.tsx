import { useEffect, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Link, useNavigate, useSearchParams } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import type { AuthoringDraftDto, RecordedChannelConfig, RecordedChannelDto } from "../api/types";
import { ErrorState, LoadingState } from "../components/common";
import { UuvNodeGlyph } from "../components/UuvNodeGlyph";
import "./recordedExperiment.css";

export function RecordedSource({ source }: { source: RecordedChannelDto | RecordedChannelConfig }) {
  const m = source.manifest;
  return <section className="recorded-source" aria-label="录音来源">
    <h3>{m.name} · v{m.version}</h3>
    <dl><dt>工作频率 / 可用带宽</dt><dd>{m.fc / 1000} / {m.usable_bandwidth_hz / 1000} kHz</dd>
      <dt>可用时长</dt><dd>{m.duration_s.toFixed(3)} s</dd>
      <dt>选取对象</dt><dd>{m.axis_kind === "ReceiverElement" ? "接收阵元" : "独立试次"} {Number(m.element) + 1}</dd>
      <dt>幅度与坐标</dt><dd>归一化幅度 · 测量几何未导入</dd></dl>
    <a href={/^https:\/\//.test(m.citation) ? m.citation : undefined} target="_blank" rel="noreferrer">数据来源 · {m.license}</a>
    <details><summary>版本依据</summary><dl><dt>资源</dt><dd>{m.asset_id}</dd><dt>原文件 SHA256</dt><dd>{m.source_sha256}</dd><dt>清单 SHA256</dt><dd>{source.manifest_sha256}</dd><dt>参考实现</dt><dd>{m.reference_revision}</dd></dl></details>
  </section>;
}

export function RecordedLinkScene({ config, phase, failed = false }: {
  config?: RecordedChannelConfig; phase?: string; failed?: boolean;
}) {
  const tx = config?.transmitter_node_id ?? "0", rx = config?.receiver_node_id ?? "1";
  return <div className="recorded-link-scene">
    <header><strong>录音链路示意</strong><span>单向传播 · 非测量位置图</span></header>
    <svg viewBox="0 0 700 270" role="img" aria-label="录音链路画布">
      <defs><marker id="recorded-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0 0L10 5L0 10Z" fill="context-stroke" /></marker></defs>
      <rect x="20" y="30" width="660" height="210" rx="12" fill="#f2f7fb" />
      <path d="M190 125H505" stroke={failed ? "#b42318" : phase ? "#087d82" : "#8ba3b6"} strokeWidth="3" markerEnd="url(#recorded-arrow)" />
      <g transform="translate(145 125)"><UuvNodeGlyph scale={1.6} tone={phase === "Transmission" ? "tx" : "standard"} /></g>
      <g transform="translate(555 125)"><UuvNodeGlyph scale={1.6} tone={failed ? "failure" : phase === "Reception" ? "success" : phase === "SignalArrival" ? "arrival" : "standard"} /></g>
      <text x="145" y="182" textAnchor="middle">N{tx} · 发送端</text><text x="555" y="182" textAnchor="middle">N{rx} · 接收端</text>
      <text x="350" y="95" textAnchor="middle">{phase === "Transmission" ? "发送中" : phase === "SignalArrival" ? "信号到达" : phase === "Reception" ? failed ? "解码失败" : "接收完成" : "时变录音信道"}</text>
    </svg>
    <p>录音已包含测量时的多径与运动影响；不叠加自由轨迹、反向链路或 Bellhop 传播。</p>
    {config && <div className="recorded-assumptions"><span>录音起点 {(Number(config.start_sample) / config.manifest.fs_delay).toFixed(3)} s</span><span>建模传播时延 {Number(config.modeled_reference_delay_ns) / 1e9} s</span><span>相对幅度 ×{config.relative_amplitude}</span><span>噪声 {config.noise.mode === "None" ? "未额外加入" : "固定连续噪声"}</span></div>}
  </div>;
}

const initial = {
  name: "录音链路实验", asset: "", start: 0, delay: 2, amplitude: 1, noise: "",
  rate: "6000", bandwidth: 5000, cycles: "2", period: 5, seed: "23", packets: 1,
  mac: "AdaptiveTdma", margin: 0.1, slot: 5, guard: 0.1, modulation: "Osdm",
  osdm: { block_length: 256, vector_length: 16, cyclic_prefix: 128, constellation: "Qpsk", minimum_sync_correlation: 0.05, maximum_sync_search: 256 },
};

/** Same resource/definition/run chain; the recording-specific editor prevents
 * accidental reuse of geometric Bellhop assumptions, not an alternate engine. */
export function RecordedExperimentPage() {
  const [params] = useSearchParams(), navigate = useNavigate(), client = useQueryClient();
  const draftId = params.get("draft");
  const catalog = useQuery({ queryKey: ["recorded-channels"], queryFn: apiClient.recordedChannels });
  const noises = useQuery({ queryKey: ["recorded-noise"], queryFn: apiClient.recordedNoise });
  const drafts = useQuery({ ...queries.drafts(), enabled: !!draftId });
  const [value, setValue] = useState(initial);
  const [saved, setSaved] = useState<AuthoringDraftDto | null>(null);
  const [savedScene, setSavedScene] = useState<AuthoringDraftDto | null>(null);
  const trajectory = useRef<string | null>(null);
  const loaded = useRef<string | null>(null);
  const [savedValue, setSavedValue] = useState("");
  const set = <K extends keyof typeof initial>(key: K, val: typeof initial[K]) => setValue(current => ({ ...current, [key]: val }));
  const existing = drafts.data?.find(d => d.draft_id === draftId && d.kind === "experiment");
  useEffect(() => {
    if (!existing || loaded.current === existing.draft_id) return;
    const p = existing.payload;
    if (p.physical_layer !== "Channel3Recorded") return;
    const s = drafts.data?.find(d => d.draft_id === p.scenario_id && d.kind === "scenario");
    if (!s) return;
    const c = p.recorded_channel as { asset_id: string; start_time_s: number; modeled_delay_s: number; relative_amplitude: number; noise: { mode: string; asset_id?: string } };
    const next = { ...initial, name: existing.name, asset: c.asset_id, start: c.start_time_s, delay: c.modeled_delay_s, amplitude: c.relative_amplitude, noise: c.noise.asset_id ?? "",
      rate: String(p.bit_rate_bits_per_second), bandwidth: Number(p.occupied_bandwidth_hz), cycles: String(p.simulation_cycle_count),
      period: Number(p.planning_cycle_duration_ns) / 1e9, seed: String(p.deterministic_seed), mac: String(p.mac_mode),
      margin: Number((p.adaptive_tdma as { safety_margin_ns?: string })?.safety_margin_ns ?? 0) / 1e9,
      slot: Number(p.slot_duration_ns ?? 5000000000) / 1e9, guard: Number(p.guard_interval_ns ?? 100000000) / 1e9,
      modulation: String(p.waveform_modulation), osdm: (p.osdm ?? initial.osdm) as typeof initial.osdm,
      packets: Number((p.flows as { packets_per_cycle: number }[])[0]?.packets_per_cycle ?? 1) };
    setValue(next); setSaved(existing); setSavedScene(s); loaded.current = existing.draft_id;
    setSavedValue(JSON.stringify(next));
  }, [existing, drafts.data]);
  const source = catalog.data?.find(s => s.manifest.asset_id === value.asset);
  const selectedNoise = noises.data?.find(n => n.manifest.asset_id === value.noise);
  const ns = (v: number) => String(Math.round(v * 1e9));
  const saveComposition = async () => {
    if (!value.name.trim()) throw new Error("请填写实验名称。");
    if (!source) throw new Error("请选择已导入的录音资源。");
    if (value.noise && !selectedNoise) throw new Error("所选连续噪声资源不可用，未改为无噪声运行。");
    if (![value.start, value.delay, value.amplitude, value.bandwidth, value.period, value.margin, value.slot, value.guard].every(Number.isFinite)
      || value.start < 0 || value.delay < 0 || value.amplitude <= 0 || value.period <= 0) throw new Error("录音起点、时延、幅度和周期必须是有效数值。");
    if (!/^[1-9]\d*$/.test(value.rate) || !/^[1-9]\d*$/.test(value.cycles) || !/^\d+$/.test(value.seed)) throw new Error("速率、周期数和随机序列编号需为有效整数。");
    if (value.start + value.period * Number(value.cycles) > source.manifest.duration_s) throw new Error("所选片段不够长，请缩短实验或前移录音起点。");
    let scenePayload = savedScene?.payload;
    if (!scenePayload) {
      // Persist ordinary Trajectory Assets even for schematic, immobile endpoints.
      if (!trajectory.current) {
        const assetId = `recorded-layout-${crypto.randomUUID()}`;
        await apiClient.importTrajectoryCsv({ asset_id: assetId, version: 1, name: "录音链路示意端点（非测量坐标）",
          content: "track_id,time_s,x_m,y_m,z_m\nTX,0,0,0,-1\nTX,86400,0,0,-1\nRX,0,1,0,-1\nRX,86400,1,0,-1\n" });
        trajectory.current = assetId;
      }
      scenePayload = { fusion_center_node_id: null, spatial_domain: null,
        nodes: [0, 1].map(i => ({ node_id: String(i), can_transmit: i === 0, can_receive: i === 1, duplex_mode: "HalfDuplex",
          initial_position: { x_meters: i, y_meters: 0, z_meters: -1 }, initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } })),
        trajectory_bindings: [0, 1].map(i => ({ node_id: String(i), asset_id: trajectory.current, version: 1, track_id: i === 0 ? "TX" : "RX" })) };
    }
    scenePayload = { ...scenePayload, environment_asset_id: value.asset, environment_version: source.manifest.version, environment_source: "Recorded" };
    const scene = savedScene ? await apiClient.updateDraft(savedScene, `${value.name} · 录音链路`, scenePayload)
      : await apiClient.createDraft("scenario", `${value.name} · 录音链路`, scenePayload);
    setSavedScene(scene);
    const payload = { scenario_id: scene.draft_id, scenario_revision: scene.revision, execution_mode: "PlanningV1", routing_mode: "Direct",
      workload: "CommunicationOnly", mac_mode: value.mac, ...(value.mac === "AdaptiveTdma" ? { adaptive_tdma: { safety_margin_ns: ns(value.margin) } }
        : { slot_duration_ns: ns(value.slot), guard_interval_ns: ns(value.guard) }),
      planning_cycle_duration_ns: ns(value.period), flows: [{
        source_node_id: (scenePayload.nodes as { node_id: string; can_transmit: boolean }[]).find(n => n.can_transmit)?.node_id,
        destination_node_id: (scenePayload.nodes as { node_id: string; can_receive: boolean }[]).find(n => n.can_receive)?.node_id, packets_per_cycle: value.packets }],
      bit_rate_bits_per_second: value.rate, simulation_cycle_count: value.cycles, deterministic_seed: value.seed,
      physical_layer: "Channel3Recorded", waveform_modulation: value.modulation, ...(value.modulation === "Osdm" ? { osdm: value.osdm } : {}),
      waveform_noise: "None", waveform_noise_psd_db_re_1upa2_per_hz: 0, source_level_db: 0,
      center_frequency_hz: source.manifest.fc, occupied_bandwidth_hz: value.bandwidth,
      recorded_channel: { asset_id: value.asset, version: source.manifest.version, start_time_s: value.start, modeled_delay_s: value.delay,
        relative_amplitude: value.amplitude, noise: value.noise ? { mode: "ContinuousLibrary", asset_id: value.noise, version: 1 } : { mode: "None" } } };
    const experiment = saved ? await apiClient.updateDraft(saved, value.name, payload) : await apiClient.createDraft("experiment", value.name, payload);
    setSaved(experiment); setSavedValue(JSON.stringify(value));
    await client.invalidateQueries({ queryKey: ["authoring-drafts"] });
    const issues = [...scene.validation_issues, ...experiment.validation_issues];
    return { experiment, issues };
  };
  const save = useMutation({ mutationFn: saveComposition });
  const run = useMutation({ mutationFn: async () => {
    const { experiment, issues } = await saveComposition();
    if (issues.length) throw new Error(`草稿已保存，运行前请修正：${issues.join("；")}`);
    return apiClient.createAuthoringRun(experiment.draft_id, experiment.revision);
  }, onSuccess: result => { void client.invalidateQueries({ queryKey: ["runs"] }); navigate(`/runs/${result.run_id}?presentation=live`); } });
  if (catalog.isPending || (draftId && drafts.isPending)) return <LoadingState />;
  if (catalog.error || drafts.error) return <ErrorState error={catalog.error || drafts.error} />;
  if (draftId && (!existing || existing.payload.physical_layer !== "Channel3Recorded")) return <p role="alert">此录音实验不存在或模式不符，未加载默认案例。</p>;
  if (existing && !drafts.data?.some(d => d.draft_id === existing.payload.scenario_id)) return <p role="alert">实验所绑定的场景不存在，不能替换为默认场景。</p>;
  if (existing && loaded.current !== existing.draft_id) return <LoadingState />;
  return <div className="recorded-experiment-page">
    <header className="panel recorded-heading"><div><h1>实验工作台 · 模式 C</h1><p>实测信道估计回放，不经过 WOA / Bellhop。</p></div><Link to="/workspace/experiment">新建模式 A / B 实验</Link></header>
    <fieldset disabled={save.isPending || run.isPending} className="recorded-workspace">
      <section className="panel recorded-setup"><h2>录音与实验</h2>
        <label>实验名称<input value={value.name} onChange={e => set("name", e.target.value)} /></label>
        <label>录音资源<select value={value.asset} onChange={e => { const s = catalog.data!.find(x => x.manifest.asset_id === e.target.value); setValue(v => ({ ...v, asset: e.target.value, bandwidth: s?.manifest.usable_bandwidth_hz ?? v.bandwidth })); }}><option value="">请选择录音</option>{catalog.data!.map(s => <option value={s.manifest.asset_id} key={s.manifest.asset_id}>{s.manifest.name} · v{s.manifest.version}</option>)}</select></label>
        {!catalog.data?.length && <p role="status">尚无录音资源。请按集成手册在服务器导入官方 MAT 文件，不会使用 Bellhop 环境替代。</p>}
        {source && <RecordedSource source={source} />}
        <div className="form-grid"><label>录音起点（s）<input type="number" min="0" step="0.1" value={value.start} onChange={e => set("start", Number(e.target.value))} /></label>
          <label>建模传播时延（s）<input type="number" min="0" step="0.1" value={value.delay} onChange={e => set("delay", Number(e.target.value))} /></label></div>
        <small>时延是本实验的显式假设，不由归一化录音推算距离。它会影响 MAC 排槽和到达时刻。</small>
        <label>相对发送幅度<input type="number" min="0.001" step="0.1" value={value.amplitude} onChange={e => set("amplitude", Number(e.target.value))} /></label>
        <div className="recorded-save-actions"><button onClick={() => save.mutate()}>保存实验草稿</button><button className="primary-button" onClick={() => run.mutate()}>{run.isPending ? "正在启动…" : "保存并运行"}</button></div>
        <p role="status">{saved ? savedValue === JSON.stringify(value) ? `已保存 · 修订 ${saved.revision}` : "有未保存修改" : "尚未保存"}</p>
        {(save.error || run.error) && <ErrorState error={save.error || run.error} />}
        {saved && saved.validation_issues.length > 0 && <p role="alert">{saved.validation_issues.join("；")}</p>}
      </section>
      <main className="panel recorded-center"><RecordedLinkScene /><section className="recorded-scope"><h2>当前实验范围</h2><p>一个只发节点 → 一个只收节点，统计波形解码、误码、CRC 与交付。选中的阵元只作为这条接收链路，不虚构阵列组网。</p><p>规划器只使用冻结的时延与接收时长上界，不读取未来录音系数。实验结束与录音覆盖不足均不会补默认数据。</p></section></main>
      <section className="panel recorded-parameters"><h2>调制、规划与时长</h2>
        <label>调制方式<select value={value.modulation} onChange={e => set("modulation", e.target.value)}><option value="Osdm">OSDM · 训练序列估计</option><option value="Bpsk">BPSK</option><option value="Bfsk">BFSK</option></select></label>
        <div className="form-grid"><label>标称比特率（bit/s）<input value={value.rate} onChange={e => set("rate", e.target.value)} /></label><label>占用带宽（Hz）<input type="number" value={value.bandwidth} onChange={e => set("bandwidth", Number(e.target.value))} /></label></div>
        {value.modulation === "Osdm" && <details><summary>OSDM 分块与同步</summary><label>星座<select value={value.osdm.constellation} onChange={e => set("osdm", { ...value.osdm, constellation: e.target.value })}><option value="Qpsk">QPSK</option><option value="Bpsk">BPSK</option></select></label><div className="form-grid">{([['block_length', '分块长度 K'], ['vector_length', '向量长度 M'], ['cyclic_prefix', '循环前缀'], ['maximum_sync_search', '同步搜索样本数'], ['minimum_sync_correlation', '同步相关门限']] as const).map(([k, label]) => <label key={k}>{label}<input type="number" step={k === "minimum_sync_correlation" ? .01 : 1} value={value.osdm[k]} onChange={e => set("osdm", { ...value.osdm, [k]: Number(e.target.value) })} /></label>)}</div><small>MAC 按包含训练、补齐和循环前缀的真实波形时长排槽。</small></details>}
        <label>连续噪声<select value={value.noise} onChange={e => { const n = noises.data?.find(n => n.manifest.asset_id === e.target.value); setValue(v => ({ ...v, noise: e.target.value, bandwidth: n ? Math.min(v.bandwidth, n.manifest.bandwidth_hz) : v.bandwidth })); }}><option value="">不额外加入</option>{noises.data?.filter(n => !source || n.manifest.center_frequency_hz === source.manifest.fc && n.manifest.elements[0] === Number(source.manifest.element)).map(n => <option value={n.manifest.asset_id} key={n.manifest.asset_id}>{n.manifest.name}</option>)}{value.noise && !selectedNoise && <option value={value.noise} disabled>原噪声资源不可用</option>}</select></label>
        {noises.error && <p role="alert">噪声目录读取失败；不会替换已保存的选择。</p>}
        {selectedNoise ? <small>官方统计模型生成 · α={selectedNoise.manifest.alpha} · 幅度 ×{selectedNoise.manifest.amplitude_scale} · 固定编号 {selectedNoise.manifest.seed} · {selectedNoise.manifest.shape[0] / selectedNoise.manifest.sample_rate_hz} s。全程使用同一连续样本，不逐包重抽。</small> : <small>录音是估计的信道，不是带原始背景噪声的音频。</small>}
        <label>MAC 规划<select value={value.mac} onChange={e => set("mac", e.target.value)}><option value="AdaptiveTdma">自适应 TDMA</option><option value="Tdma">配置式 TDMA</option></select></label>
        <div className="form-grid"><label>业务周期（s）<input type="number" min="0.001" step="1" value={value.period} onChange={e => set("period", Number(e.target.value))} /></label><label>周期数<input type="number" min="1" value={value.cycles} onChange={e => set("cycles", e.target.value)} /></label><label>每周期包数<input type="number" min="1" value={value.packets} onChange={e => set("packets", Number(e.target.value))} /></label></div>
        <details><summary>规划私有参数</summary>{value.mac === "AdaptiveTdma" ? <label>安全余量（s）<input type="number" min="0" step="0.1" value={value.margin} onChange={e => set("margin", Number(e.target.value))} /></label> : <><label>时隙长度（s）<input type="number" value={value.slot} onChange={e => set("slot", Number(e.target.value))} /></label><label>保护时间（s）<input type="number" value={value.guard} onChange={e => set("guard", Number(e.target.value))} /></label></>}</details>
        <p className="duration-preview">仿真 {value.period * Number(value.cycles)} s · 使用录音 {value.start}～{value.start + value.period * Number(value.cycles)} s</p>
        <details><summary>可重复性</summary><label>随机序列编号<input value={value.seed} onChange={e => set("seed", e.target.value)} /></label><small>录音系数不随此编号变化；重复使用同一资源、片段和参数。</small></details>
      </section>
    </fieldset>
  </div>;
}
