import { useEffect, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { useNavigate } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import type { AuthoringDraftDto, ProxyBindings, ProxyNode, ProxySample, RecordedProxyResolved } from "../api/types";
import { ErrorState, LoadingState } from "../components/common";
import { OsdmSettings } from "../components/OsdmSettings";
import type { OsdmConfig } from "../domain/experimentPresets";
import { useExperimentIdentity } from "./experimentEditorContext";
import { RecordedExperimentPage } from "./recordedExperiment";
import "./recordedProxy.css";

type Layout = Record<string, { x: number; y: number }>;
const canonical = (value: unknown): string => JSON.stringify(value, (_key, item) =>
  item && typeof item === "object" && !Array.isArray(item)
    ? Object.fromEntries(Object.keys(item).sort().map(key => [key, item[key]])) : item);
const position = (id: string, index: number, nodes: ProxyNode[], layout: Layout) =>
  layout[id] ?? { x: nodes[index].can_transmit ? 120 : 580, y: 80 + index * 80 % 360 };
export function ProxyNetworkCanvas({ nodes, edges, layout = {}, selected, select, move }: {
  nodes: ProxyNode[]; edges: { tx_id: string; rx_id: string }[]; layout?: Layout; selected?: string;
  select?: (id: string) => void; move?: (id: string, x: number, y: number) => void;
}) {
  const dragging = useRef<string | null>(null);
  return <svg className="proxy-network-canvas" viewBox="0 0 700 460" role="img" aria-label="代理网络逻辑画布">
    <defs><marker id="proxy-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0 0L10 5L0 10Z" fill="context-stroke" /></marker></defs>
    <text x="20" y="25">逻辑布局 · 非测量坐标</text>
    {edges.map(e => {
      const a = nodes.findIndex(n => n.node_id === e.tx_id), b = nodes.findIndex(n => n.node_id === e.rx_id);
      if (a < 0 || b < 0) return null;
      const p = position(e.tx_id, a, nodes, layout), q = position(e.rx_id, b, nodes, layout);
      const length = Math.hypot(q.x - p.x, q.y - p.y) || 1;
      const key = `${e.tx_id}:${e.rx_id}`;
      return <g key={key} onClick={() => select?.(key)} role={select ? "button" : undefined}
        tabIndex={select ? 0 : undefined} onKeyDown={event => { if (event.key === "Enter") select?.(key); }}
        aria-label={`链路 N${e.tx_id} 到 N${e.rx_id}`}>
        <line x1={p.x} y1={p.y} x2={q.x} y2={q.y} stroke="transparent" strokeWidth="20" />
        <line x1={p.x + (q.x - p.x) * 28 / length} y1={p.y + (q.y - p.y) * 28 / length}
          x2={q.x - (q.x - p.x) * 36 / length} y2={q.y - (q.y - p.y) * 36 / length}
          stroke={selected === key ? "#b55c00" : "#397c9c"} strokeWidth={selected === key ? 4 : 2} markerEnd="url(#proxy-arrow)" />
      </g>;
    })}
    {nodes.map((n, i) => {
      const p = position(n.node_id, i, nodes, layout);
      return <g key={n.node_id} transform={`translate(${p.x} ${p.y})`} tabIndex={select ? 0 : undefined}
        role={select ? "button" : undefined} aria-label={`节点 N${n.node_id}`} onClick={() => select?.(n.node_id)}
        onKeyDown={e => { if (e.key === "Enter") select?.(n.node_id); }}
        onPointerDown={e => { if (move) { dragging.current = n.node_id; e.currentTarget.setPointerCapture(e.pointerId); } }}
        onPointerUp={() => { dragging.current = null; }}
        onPointerMove={e => {
          if (dragging.current !== n.node_id || !move) return;
          const svg = e.currentTarget.ownerSVGElement!, matrix = svg.getScreenCTM();
          if (!matrix) return;
          const point = new DOMPoint(e.clientX, e.clientY).matrixTransform(matrix.inverse());
          move(n.node_id, Math.max(40, Math.min(660, point.x)), Math.max(60, Math.min(420, point.y)));
        }}>
        <circle r="27" fill={selected === n.node_id ? "#fff1d9" : "#e8f3fa"} stroke="#306b8d" strokeWidth="2" />
        <text textAnchor="middle" y="5">N{n.node_id}</text>
        <text textAnchor="middle" y="47">{n.can_transmit ? "发送" : ""}{n.can_receive ? " 接收" : ""}</text>
      </g>;
    })}
  </svg>;
}

export function RecordedWorkbench() {
  const { draftId } = useExperimentIdentity();
  const drafts = useQuery({ ...queries.drafts(), enabled: !!draftId });
  const [proxy, setProxy] = useState(false);
  if (draftId && drafts.isPending) return <LoadingState />;
  if (drafts.error) return <ErrorState error={drafts.error} />;
  const existing = drafts.data?.find(d => d.draft_id === draftId);
  const mode = draftId ? !!existing?.payload.recorded_proxy : proxy;
  return <>
    {!draftId && <details className="proxy-mode-switch"><summary>实录实验类型：{mode ? "代理网络" : "单链路回放"}</summary>
      <p>切换会重新打开编辑器，请先保存当前输入。</p>
      <button onClick={() => { if (window.confirm("已保存当前输入？切换将关闭当前编辑器。")) setProxy(!mode); }}>
        {mode ? "使用原单链路回放" : "新建实录样本代理网络"}</button>
    </details>}
    {mode ? <RecordedProxyEditor /> : <RecordedExperimentPage />}
  </>;
}

const initialNodes: ProxyNode[] = [
  { node_id: "0", can_transmit: true, can_receive: false },
  { node_id: "1", can_transmit: true, can_receive: false },
  { node_id: "2", can_transmit: false, can_receive: true },
];
export function RecordedProxyEditor() {
  const { draftId, context } = useExperimentIdentity();
  const navigate = useNavigate(), cache = useQueryClient();
  const records = useQuery({ queryKey: ["recorded-channels"], queryFn: apiClient.recordedChannels });
  const noise = useQuery({ queryKey: ["recorded-noise"], queryFn: apiClient.recordedNoise });
  const drafts = useQuery({ ...queries.drafts(), enabled: !!draftId });
  const [nodes, setNodes] = useState(initialNodes), [layout, setLayout] = useState<Layout>({});
  const [name, setName] = useState("实录样本代理网络"), [asset, setAsset] = useState("");
  const [pool, setPool] = useState<string[]>([]), [allocation, setAllocation] = useState<ProxyBindings["allocation"]>("GlobalSame");
  const [overrides, setOverrides] = useState<ProxyBindings["overrides"]>([]);
  const [disabledFlows, setDisabledFlows] = useState<string[]>([]);
  const [receiverNoise, setReceiverNoise] = useState<Record<string, string>>({});
  const [selected, setSelected] = useState("0"), [delay, setDelay] = useState(2.048);
  const [rate, setRate] = useState("6000"), [bandwidth, setBandwidth] = useState(4000);
  const [period, setPeriod] = useState(16.384), [cycles, setCycles] = useState("2");
  const [slot, setSlot] = useState(2.048), [mac, setMac] = useState("AdaptiveTdma");
  const [osdm, setOsdm] = useState<OsdmConfig | null>(null), [seed, setSeed] = useState("23");
  const [modulation, setModulation] = useState("Osdm");
  const [nodeModems, setNodeModems] = useState<NonNullable<ProxyBindings["transmitter_modulations"]>>([]);
  const [accepted, setAccepted] = useState(false), [saved, setSaved] = useState<AuthoringDraftDto | null>(null);
  const [scene, setScene] = useState<AuthoringDraftDto | null>(null), [savedState, setSavedState] = useState("");
  const [loadedStatePending, setLoadedStatePending] = useState(false);
  const loaded = useRef("");
  const state = JSON.stringify({ nodes, layout, name, asset, pool, allocation, overrides, disabledFlows, receiverNoise, delay, rate, bandwidth, period, cycles, slot, mac, modulation, nodeModems, osdm, seed, accepted });
  useEffect(() => {
    if (!draftId || loaded.current === draftId || !drafts.data) return;
    const d = drafts.data.find(d => d.draft_id === draftId), p = d?.payload;
    const s = drafts.data.find(s => s.draft_id === p?.scenario_id);
    if (!d || !p?.recorded_proxy || !s) return;
    const b = p.recorded_proxy as ProxyBindings;
    setNodes(b.nodes); setName(d.name); setLayout((p.proxy_ui_layout ?? {}) as Layout);
    setAsset(String(s.payload.environment_asset_id)); setPool(b.samples.map(s => s.asset_id)); setAllocation(b.allocation);
    setOverrides(b.overrides); setReceiverNoise(Object.fromEntries(b.receiver_noise.map(n => [n.rx_id, n.asset_id ?? ""])));
    const flows = p.flows as { source_node_id: string; destination_node_id: string }[];
    setDisabledFlows(b.nodes.flatMap(a => b.nodes.filter(r => a.can_transmit && r.can_receive && a.node_id !== r.node_id)
      .filter(r => !flows.some(f => f.source_node_id === a.node_id && f.destination_node_id === r.node_id))
      .map(r => `${a.node_id}:${r.node_id}`)));
    setModulation(String(p.waveform_modulation));
    setNodeModems(b.transmitter_modulations ?? []);
    setDelay(Number(b.samples[0]?.modeled_reference_delay_ns ?? "2048000000") / 1e9);
    setRate(String(p.bit_rate_bits_per_second)); setBandwidth(b.occupied_bandwidth_hz);
    setPeriod(Number(p.planning_cycle_duration_ns) / 1e9); setCycles(String(p.simulation_cycle_count));
    setSlot(Number(p.slot_duration_ns ?? "2048000000") / 1e9); setMac(String(p.mac_mode));
    setOsdm(p.osdm as OsdmConfig); setSeed(String(p.deterministic_seed)); setAccepted(true);
    setSaved(d); setScene(s); loaded.current = draftId; setLoadedStatePending(true);
  }, [draftId, drafts.data]);
  useEffect(() => {
    if (loadedStatePending) { setSavedState(state); setLoadedStatePending(false); }
  }, [loadedStatePending, state]);
  const source = records.data?.find(r => r.manifest.asset_id === asset);
  const pairs = nodes.flatMap(a => nodes.filter(b => a.can_transmit && b.can_receive && a.node_id !== b.node_id)
    .map(b => ({ tx_id: a.node_id, rx_id: b.node_id })));
  const sample = (id: string): ProxySample => ({ asset_id: id, version: 1, start_sample: "0",
    modeled_reference_delay_ns: String(Math.round(delay * 1e9)), relative_amplitude: 1 });
  const spec = (): ProxyBindings => ({
    schema: "ns3-factory-recorded-proxy-bindings-v1", nodes, allocation,
    samples: allocation === "Explicit" ? [] : (allocation === "GlobalSame" ? [asset] : pool).map(sample),
    overrides, receiver_noise: nodes.filter(n => pairs.some(p => p.rx_id === n.node_id)).map(n => ({
      rx_id: n.node_id, ...(receiverNoise[n.node_id] ? { mode: "ContinuousLibrary" as const, asset_id: receiverNoise[n.node_id], version: 1 as const } : { mode: "None" as const }),
    })), carrier_hz: source?.manifest.fc ?? 0, baseband_rate_hz: modulation === "Osdm"
      ? Number(rate) / (osdm?.constellation === "Qpsk" ? 2 : 1) : Number(rate) * 100,
    occupied_bandwidth_hz: bandwidth, duration_ns: String(Math.round(period * 1e9) * Number(cycles)),
    transmitter_modulations: nodeModems,
  });
  const validationInput = JSON.stringify(spec());
  const validate = useMutation({ mutationFn: async (input: string) => ({
    input, result: await apiClient.validateRecordedProxy(JSON.parse(input) as ProxyBindings),
  }) });
  const currentValidation = validate.data?.input === validationInput ? validate.data.result : null;
  const saveComposition = async () => {
    if (draftId && loaded.current !== draftId) throw new Error("原草稿尚未完整读取，不能创建替代实验。");
    if (!name.trim() || !source || (modulation === "Osdm" && !osdm) || !accepted) throw new Error("请填写名称、选择信道和波形配置，并确认代理网络假设。");
    if (!pairs.some(p => !disabledFlows.includes(`${p.tx_id}:${p.rx_id}`))) throw new Error("请至少启用一条业务流；物理旁听链路不自动产生业务。");
    if (!/^[1-9]\d*$/.test(rate) || !/^[1-9]\d*$/.test(cycles) || !/^\d+$/.test(seed)) throw new Error("速率、周期数和随机序列编号须为整数。");
    if (![period, slot, delay, bandwidth].every(v => Number.isFinite(v) && v > 0)) throw new Error("时间和带宽须为正数。");
    await apiClient.validateRecordedProxy(spec());
    const signature = nodes.map(n => n.node_id).join(",");
    let bindings = scene?.payload.trajectory_bindings;
    if (!scene || (scene.payload.nodes as ProxyNode[]).map(n => n.node_id).join(",") !== signature) {
      const id = `proxy-layout-${crypto.randomUUID()}`;
      await apiClient.importTrajectoryCsv({ asset_id: id, version: 1, name: "代理节点固定适配点（非物理位置）",
        content: "track_id,time_s,x_m,y_m,z_m\n" + nodes.flatMap(n => [0, 86400].map(t => `N${n.node_id},${t},0,0,-1\n`)).join("") });
      bindings = nodes.map(n => ({ node_id: n.node_id, asset_id: id, version: 1, track_id: `N${n.node_id}` }));
    }
    const sp = { environment_asset_id: asset, environment_version: 1, environment_source: "Recorded",
      fusion_center_node_id: null, spatial_domain: null, trajectory_bindings: bindings,
      nodes: nodes.map(n => ({ ...n, duplex_mode: "HalfDuplex", initial_position: { x_meters: 0, y_meters: 0, z_meters: -1 },
        initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } })) };
    // A pure layout edit must not publish a changed physical scene.
    const s = scene && canonical(scene.payload) === canonical(sp) ? scene
      : scene ? await apiClient.updateDraft(scene, `${name} · 逻辑网络`, sp) : await apiClient.createDraft("scenario", `${name} · 逻辑网络`, sp);
    setScene(s);
    const p = { scenario_id: s.draft_id, scenario_revision: s.revision, execution_mode: "PlanningV1",
      routing_mode: "Direct", workload: "CommunicationOnly", mac_mode: mac,
      ...(mac === "AdaptiveTdma" ? { adaptive_tdma: { safety_margin_ns: "100000000" } } : { slot_duration_ns: String(Math.round(slot * 1e9)), guard_interval_ns: "0" }),
      planning_cycle_duration_ns: String(Math.round(period * 1e9)), simulation_cycle_count: cycles, deterministic_seed: seed,
      flows: pairs.filter(p => !disabledFlows.includes(`${p.tx_id}:${p.rx_id}`))
        .map(p => ({ source_node_id: p.tx_id, destination_node_id: p.rx_id, packets_per_cycle: 1 })),
      fusion_receiver_node_id: null, physical_layer: "Channel3Recorded", waveform_modulation: modulation,
      ...(modulation === "Osdm" ? { osdm } : {}),
      waveform_noise: "None", waveform_noise_psd_db_re_1upa2_per_hz: 0, source_level_db: 0,
      bit_rate_bits_per_second: rate, center_frequency_hz: source.manifest.fc, occupied_bandwidth_hz: bandwidth,
      recorded_proxy: spec(), proxy_ui_layout: layout };
    const d = saved ? await apiClient.updateDraft(saved, name, p) : await apiClient.createDraft("experiment", name, p);
    setSaved(d); setSavedState(state); await cache.invalidateQueries({ queryKey: ["authoring-drafts"] });
    if (d.validation_issues.length) throw new Error(`已保存，请修正：${d.validation_issues.join("；")}`);
    return d;
  };
  const save = useMutation({ mutationFn: saveComposition });
  const copy = useMutation({ mutationFn: async () => {
    if (!saved || savedState !== state) throw new Error("请先保存当前输入，再创建副本。");
    return apiClient.createDraft("experiment", `${saved.name} · 副本`, saved.payload);
  }, onSuccess: async d => {
    await cache.invalidateQueries({ queryKey: ["authoring-drafts"] });
    navigate(`/workspace/experiment?draft=${d.draft_id}`);
  } });
  const run = useMutation({ mutationFn: async () => { const d = await saveComposition(); return apiClient.createAuthoringRun(d.draft_id, d.revision); },
    onSuccess: result => { void cache.invalidateQueries({ queryKey: ["runs"] }); navigate(`/runs/${result.run_id}?presentation=live`); } });
  useEffect(() => { context?.registerSave(save.mutateAsync); });
  useEffect(() => { context?.reportState(savedState !== state, save.isPending || run.isPending || copy.isPending); });
  useEffect(() => { context?.reportResource(asset); }, [context, asset]);
  useEffect(() => { if (context?.selection) setAsset(context.selection.id); }, [context?.selection]);
  if (records.isPending || (draftId && drafts.isPending)) return <LoadingState />;
  if (records.error || drafts.error) return <ErrorState error={records.error || drafts.error} />;
  if (draftId && !drafts.data?.some(d => d.draft_id === draftId && d.payload.recorded_proxy)) return <p role="alert">代理实验不存在。</p>;
  const edge = pairs.find(p => `${p.tx_id}:${p.rx_id}` === selected);
  const override = overrides.find(p => `${p.tx_id}:${p.rx_id}` === selected);
  const updateEdge = (s: ProxySample | null) => {
    if (!edge) return;
    setOverrides(v => [...v.filter(p => `${p.tx_id}:${p.rx_id}` !== selected), ...(s ? [{ ...edge, sample: s }] : [])]);
  };
  return <div className="proxy-editor">
    <header className="panel proxy-toolbar"><label>实验名称<input value={name} onChange={e => setName(e.target.value)} /></label>
      <span>实录样本驱动 · 非同一海试的多发一收</span>
      <button disabled={save.isPending || run.isPending || copy.isPending} onClick={() => save.mutate()}>保存草稿</button>
      <button disabled={!saved || savedState !== state || save.isPending || run.isPending || copy.isPending}
        title={savedState !== state ? "先保存当前输入，再创建独立实验副本" : undefined}
        onClick={() => copy.mutate()}>另存实验副本</button>
      <button disabled={save.isPending || run.isPending || copy.isPending} onClick={() => run.mutate()}>{run.isPending ? "校验并启动…" : "保存并运行"}</button>
    </header>
    {(save.error || run.error || copy.error || validate.error) && <ErrorState error={save.error || run.error || copy.error || validate.error} />}
    {save.isSuccess && <p role="status">代理网络草稿已保存。</p>}
    <div className="proxy-columns"><main className="panel">
      <div className="proxy-toolbar"><h2>逻辑节点与有向信道</h2><button onClick={() => setNodes(v => [...v, {
        node_id: String(Math.max(...v.map(n => Number(n.node_id))) + 1), can_transmit: true, can_receive: false }])}>添加节点</button></div>
      <ProxyNetworkCanvas nodes={nodes} edges={pairs} layout={layout} selected={selected} select={setSelected}
        move={(id, x, y) => setLayout(v => ({ ...v, [id]: { x, y } }))} />
      <p>拖动只改变布局，不改变传播。所有可接收方向都需绑定，包括旁听。</p>
      <button disabled={validate.isPending} onClick={() => validate.mutate(validationInput)}>{validate.isPending ? "检查中…" : "检查并展开链路映射"}</button>
      {currentValidation && <table><thead><tr><th>方向</th><th>解析信道</th><th>分配 / 片段</th></tr></thead><tbody>{currentValidation.resolved_links.map(l =>
        <tr key={`${l.tx_id}:${l.rx_id}`}><td>N{l.tx_id} → N{l.rx_id}</td><td>{l.manifest.name} · v{l.version}</td><td>{l.origin === "EdgeOverride" ? "单边覆盖" : "全局模板"}<br />起点样本 {l.start_sample} · 时延 {Number(l.modeled_reference_delay_ns) / 1e9} s · 幅度 ×{l.relative_amplitude}</td></tr>)}</tbody></table>}
    </main><aside className="panel proxy-controls">
      <h2>{edge ? `链路 N${edge.tx_id} → N${edge.rx_id}` : `节点 N${selected}`}</h2>
      {edge ? <><label><input type="checkbox" checked={!disabledFlows.includes(selected)}
        onChange={e => setDisabledFlows(v => e.target.checked ? v.filter(k => k !== selected) : [...v, selected])} />向此节点发送业务</label>
        <p>关闭业务仍保留物理接收与干扰，不等于删除声学传播。</p>
        <label>单边信道覆盖<select value={override?.sample.asset_id ?? ""} onChange={e => updateEdge(e.target.value ? sample(e.target.value) : null)}>
        <option value="">继承全局配置</option>{records.data?.map(r => <option key={r.manifest.asset_id} value={r.manifest.asset_id}>{r.manifest.name}</option>)}
      </select></label>{override && <><label>录音起点（样本）<input value={override.sample.start_sample} onChange={e => updateEdge({ ...override.sample, start_sample: e.target.value })} /></label>
        <label>建模时延（s）<input type="number" value={Number(override.sample.modeled_reference_delay_ns) / 1e9}
          onChange={e => updateEdge({ ...override.sample, modeled_reference_delay_ns: String(Math.round(Number(e.target.value) * 1e9)) })} /></label>
        <label>相对幅度<input type="number" value={override.sample.relative_amplitude} onChange={e => updateEdge({ ...override.sample, relative_amplitude: Number(e.target.value) })} /></label>
        <button onClick={() => updateEdge(null)}>移除单边覆盖</button></>}</> : nodes.filter(n => n.node_id === selected).map(n => <div key={n.node_id}>
          {(["can_transmit", "can_receive"] as const).map(k => <label key={k}><input type="checkbox" checked={n[k]} onChange={e => {
            const enabled = e.target.checked;
            setNodes(v => v.map(item => item.node_id === n.node_id ? { ...item, [k]: enabled } : item));
            if (!enabled) setOverrides(v => v.filter(edge => (k === "can_transmit" ? edge.tx_id : edge.rx_id) !== n.node_id));
            if (!enabled && k === "can_transmit") setNodeModems(v => v.filter(m => m.node_id !== n.node_id));
          }} />{k === "can_transmit" ? "可发送" : "可接收"}</label>)}
          {n.can_transmit && modulation === "Osdm" && <label>此发送节点的调制<select
            value={nodeModems.find(m => m.node_id === n.node_id)?.modulation ?? "Osdm"}
            onChange={e => setNodeModems(v => [...v.filter(m => m.node_id !== n.node_id),
              ...(e.target.value === "Osdm" ? [] : [{ node_id:n.node_id, modulation:e.target.value as "Bpsk" | "Bfsk" }])])}>
            <option value="Osdm">继承 OSDM</option>
            <option value="Bpsk">BPSK · 公共时钟</option><option value="Bfsk">BFSK · 公共时钟</option>
          </select><small>BPSK/BFSK 标称速率为 {Number(rate)/(osdm?.constellation === "Qpsk" ? 2 : 1)/100} bit/s；使用共同采样时钟，不是自动适配。</small></label>}
          <button disabled={nodes.length <= 2} onClick={() => { setNodes(v => v.filter(x => x.node_id !== n.node_id)); setOverrides(v => v.filter(x => x.tx_id !== n.node_id && x.rx_id !== n.node_id)); setNodeModems(v => v.filter(m => m.node_id !== n.node_id)); }}>删除节点</button>
        </div>)}
      <h3>全局信道</h3>
      <label>主资源<select value={asset} onChange={e => setAsset(e.target.value)}><option value="">请选择</option>
        {records.data?.map(r => <option key={r.manifest.asset_id} value={r.manifest.asset_id}>{r.manifest.name}</option>)}</select></label>
      <label>分配方式<select value={allocation} onChange={e => setAllocation(e.target.value as typeof allocation)}>
        <option value="GlobalSame">全部链路使用同一 CIR</option><option value="GlobalPool">按节点编号顺序循环分配样本池</option><option value="Explicit">逐边显式指定</option></select></label>
      {allocation === "GlobalPool" && records.data?.map(r => <label key={r.manifest.asset_id}><input type="checkbox" checked={pool.includes(r.manifest.asset_id)}
        onChange={e => setPool(v => e.target.checked ? [...v, r.manifest.asset_id] : v.filter(id => id !== r.manifest.asset_id))} />{r.manifest.name}</label>)}
      <label>默认建模时延（s）<input type="number" value={delay} onChange={e => setDelay(Number(e.target.value))} /></label>
      <label><input type="checkbox" checked={accepted} onChange={e => setAccepted(e.target.checked)} />确认：跨链路共时、噪声分配及相对增益是仿真假设，不是实测网络关系。</label>
      <h3>接收端噪声（每个节点只加一次）</h3>
      {nodes.filter(n => pairs.some(p => p.rx_id === n.node_id)).map(n => <label key={n.node_id}>N{n.node_id}<select value={receiverNoise[n.node_id] ?? ""}
        onChange={e => setReceiverNoise(v => ({ ...v, [n.node_id]: e.target.value }))}><option value="">无噪声</option>
        {noise.data?.map(r => <option key={r.manifest.asset_id} value={r.manifest.asset_id}>{r.manifest.asset_id}</option>)}</select></label>)}
      <h3>波形与运行</h3>
      <label>调制方式<select value={modulation} onChange={e => { setModulation(e.target.value); if (e.target.value !== "Osdm") setNodeModems([]); }}>
        <option value="Osdm">OSDM</option><option value="Bpsk">BPSK</option><option value="Bfsk">BFSK</option>
      </select></label>
      {modulation === "Osdm" && <OsdmSettings layer="Channel3Recorded" value={osdm} onChange={setOsdm} allowInitialPreset={!draftId || !osdm} />}
      <label>标称速率（bit/s）<input value={rate} onChange={e => setRate(e.target.value)} /></label>
      <label>占用带宽（Hz）<input type="number" value={bandwidth} onChange={e => setBandwidth(Number(e.target.value))} /></label>
      <label>MAC<select value={mac} onChange={e => setMac(e.target.value)}><option value="AdaptiveTdma">自适应 TDMA</option><option value="Tdma">配置 TDMA（重叠研究）</option></select></label>
      {mac === "Tdma" && <label>发送时隙（s）<input type="number" value={slot} onChange={e => setSlot(Number(e.target.value))} /></label>}
      <label>规划周期（s）<input type="number" value={period} onChange={e => setPeriod(Number(e.target.value))} /></label>
      <label>周期数<input value={cycles} onChange={e => setCycles(e.target.value)} /></label>
      <details><summary>复现设置</summary><label>随机序列编号<input value={seed} onChange={e => setSeed(e.target.value)} /></label></details>
    </aside></div>
  </div>;
}
