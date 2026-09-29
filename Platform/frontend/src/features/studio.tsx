import { useEffect, useLayoutEffect, useMemo, useRef, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Link, useParams, useSearchParams } from "react-router-dom";
import { apiClient, ApiFailure } from "../api/client";
import { queries } from "../api/queries";
import { emptyProjection, RunEventProjection } from "../api/runEvents";
import type { ExperimentDto, PositionSample, ResultDto, RunEventDto } from "../api/types";
import { ErrorState, LoadingState, Status } from "../components/common";
import { RunFailure } from "../components/RunFailure";
import { RunActions } from "../components/RunActions";
import { SceneDepthProfile } from "../components/SceneDepthProfile";
import { UuvNodeGlyph, type UuvNodeTone } from "../components/UuvNodeGlyph";
import { ResultAnalysis } from "./resultAnalysis";
import { mergePositionSamples, observedRunScene, type RunScene } from "../domain/runScene";
import { physicalModeLabel } from "../productLanguage";
import { formatNanoseconds } from "../domain/format";
import { cumulativeTrends, eventTechnicalDetail, latestActiveLink, projectionCounts } from "../domain/runAnalytics";
import { createBoundedSceneView, createRecordedSceneView } from "../domain/worldViewTransform";
import { eventCurve, positionAt, positionSamples, replayDelay, seekTime, studioEvents, timePercent, type StudioEvent } from "../domain/studio";

const timelineLanes = [
  ["Transmission", "发送"],
  ["ChannelOutcome", "信道求解"],
  ["SignalArrival", "实际到达"],
  ["Reception", "解调/接收"],
  ["Fusion", "融合"],
  ["CycleCommit", "周期提交"],
] as const;
type TimelineKind = (typeof timelineLanes)[number][0];

function protocolIntentEvents(events: readonly RunEventDto[]) {
  const targets = new Map<string, string | null>();
  for (const event of events) if (event.trace.kind === "Transmission") {
    targets.set(event.trace.payload.transmission_id, event.trace.payload.target.type === "Unicast" ? event.trace.payload.target.node_id : null);
  }
  return events.filter((event) => {
    const trace = event.trace;
    if (trace.kind !== "ChannelOutcome" && trace.kind !== "SignalArrival" && trace.kind !== "Reception") return true;
    const target = targets.get(trace.payload.transmission_id);
    return target === null || target === undefined || target === trace.payload.receiver_node_id;
  });
}

function useEvents(runId: string) {
  const [projection, update] = useState(emptyProjection);
  const [error, fail] = useState<ApiFailure | null>(null);
  const [streamEnded, endStream] = useState(false);
  useEffect(() => {
    update(emptyProjection); fail(null); endStream(false);
    let latestSequence = "0";
    const source = new RunEventProjection(apiClient.url(`/runs/${encodeURIComponent(runId)}/events`), next => {
      latestSequence = next.latestSequence; update(next); fail(null);
    }, fail, url => {
      const events = new EventSource(url);
      // This only marks completion of observation delivery. Run lifecycle still
      // comes exclusively from GET /runs/{id}, including immediate failures.
      events.addEventListener("stream-end", event => {
        try { if (JSON.parse(event.data).last_sequence === latestSequence) endStream(true); }
        catch { /* RunEventProjection independently reports malformed framing. */ }
      });
      return events;
    });
    source.connect(); return () => source.close();
  }, [runId]);
  return { projection, error, streamEnded };
}

function StudioCanvas({ scenario, samples, row, events, selected, select, environmentInputs }: {
  scenario: RunScene; samples: PositionSample[]; row?: StudioEvent; events: readonly RunEventDto[];
  selected: string | null; select: (id: string) => void;
  environmentInputs?: unknown;
}) {
  const canvas = useRef<SVGSVGElement>(null);
  const [viewportHeight, setViewportHeight] = useState(320);
  useLayoutEffect(() => {
    const element = canvas.current;
    if (!element || typeof ResizeObserver === "undefined") return;
    // Match the SVG viewBox to its display box, rather than letterboxing the
    // plan inside a fixed aspect ratio. This preserves equal XY scale while
    // giving the plan and depth profile exactly the same full-width X mapping.
    const resize = () => {
      const bounds = element.getBoundingClientRect();
      if (bounds.width > 0 && bounds.height > 0) setViewportHeight(Math.max(120, 980 * bounds.height / bounds.width));
    };
    const observer = new ResizeObserver(resize); observer.observe(element); resize();
    return () => observer.disconnect();
  }, []);
  const positions = scenario.nodes.map(node => ({ node, p: row ? positionAt(samples, node.node_id, row.at) : null }))
    .filter(item => !scenario.observationsOnly || item.p !== null);
  const exact = positions.length > 0 && positions.every(item => item.p !== null);
  const domain = scenario.spatial_domain;
  const viewport = { width: 980, height: viewportHeight, padding: 45, paddingY: 28 };
  const plotBottom = viewportHeight - viewport.paddingY;
  const transform = domain ? createBoundedSceneView(domain, viewport)
    : createRecordedSceneView([...scenario.nodes.map(n => n.initial_position), ...samples], viewport);
  const domainTopLeft = domain ? transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_max_m }) : null;
  const domainBottomRight = domain ? transform.point({ x_meters: domain.x_max_m, y_meters: domain.y_min_m }) : null;
  const points = new Map(positions.map(({ node, p }) => [node.node_id, transform.point(p ?? node.initial_position)]));
  const activityEvents = events.filter((event) => ["Transmission", "ChannelOutcome", "SignalArrival", "Reception"].includes(event.trace.kind));
  const activity = row && ["Transmission", "SignalArrival", "Reception"].includes(row.kind) ? latestActiveLink(activityEvents) : null;
  const tx = activity?.sender ? points.get(activity.sender) : null;
  const rx = activity?.receiver ? points.get(activity.receiver) : null;
  const ticks = [0, .5, 1];
  const tone = (nodeId: string): UuvNodeTone => row?.kind === "Fusion" && nodeId === scenario.fusion_center_node_id ? "fusion"
    : nodeId === activity?.receiver && activity.phase === "SignalArrival" ? "arrival"
    : nodeId === activity?.receiver && activity.outcome === "NotDecoded" ? "failure"
    : nodeId === activity?.receiver && activity.phase === "Reception" ? "success"
    : nodeId === activity?.sender ? "tx" : nodeId === selected ? "selected" : "standard";
  return <><div className="canvas-toolbar"><strong>二维运行场景</strong><span>{exact ? "正式轨迹位置 · ns-3 仿真时刻" : "场景初始几何 · 当前通信事件"}</span>{activity && <b>{activity.outcome}</b>}</div>
    <svg ref={canvas} className={`run-topology phase-${activity?.phase.toLowerCase() ?? "idle"}`} viewBox={`0 0 980 ${viewportHeight}`} role="img" aria-label="运行画布">
      <defs><pattern id="studio-grid" width="49" height="54" patternUnits="userSpaceOnUse"><path d="M49 0H0V54" fill="none" stroke="#20364c" /></pattern><marker id="studio-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0 0L10 5L0 10Z" fill="#38bdf8" /></marker></defs>
      <rect width="980" height={viewportHeight} fill="#071827" /><rect x="45" y="28" width="890" height={viewportHeight - 56} fill="url(#studio-grid)" stroke="#527589" />
      {domainTopLeft && domainBottomRight && <rect data-testid="runtime-spatial-domain" x={domainTopLeft.x} y={domainTopLeft.y}
        width={domainBottomRight.x - domainTopLeft.x} height={domainBottomRight.y - domainTopLeft.y} fill="none" stroke="#78c9a8" strokeWidth="1.5" strokeDasharray="6 3" />}
      {ticks.map(fraction => { const xValue = transform.xDomain[0] + fraction * (transform.xDomain[1] - transform.xDomain[0]); const yValue = transform.yDomain[0] + fraction * (transform.yDomain[1] - transform.yDomain[0]); const x = transform.point({ x_meters: xValue, y_meters: 0 }).x; const y = transform.point({ x_meters: 0, y_meters: yValue }).y;
        return <g key={fraction}><line x1={x} y1="28" x2={x} y2={plotBottom} stroke="#345064" /><text x={x} y={plotBottom + 19} textAnchor="middle" fill="#bcd2df" fontSize="13">{Math.round(xValue)}</text><line x1="45" y1={y} x2="935" y2={y} stroke="#345064" /><text x="39" y={y + 4} textAnchor="end" fill="#bcd2df" fontSize="13">{Math.round(yValue)}</text></g>; })}
      <text x="935" y="16" textAnchor="end" fill="#c4d8e3" fontSize="13">X / m</text><text x="10" y="16" fill="#c4d8e3" fontSize="13">Y / m</text><text x="60" y="16" fill="#b3c9d6" fontSize="12">{domain ? "局部坐标 · XY 等比例 · 固定试验域（一维地形沿 Y 不变）" : "局部坐标 · XY 等比例 · 视野由节点及已记录轨迹确定"}</text>
      {scenario.nodes.map(node => <polyline key={`track-${node.node_id}`} data-testid={`track-${node.node_id}`} points={samples.filter(p => p.node_id === node.node_id).map(p => { const xy = transform.point(p); return `${xy.x},${xy.y}`; }).join(" ")} fill="none" stroke="#5a829a" strokeDasharray="4 4" strokeWidth="2" />)}
      {tx && rx && <path data-testid="communication-arrow" d={eventCurve(tx, rx)} fill="none" stroke={activity?.outcome === "NotDecoded" ? "#fb7185" : activity?.phase === "Reception" ? "#34d399" : activity?.phase === "SignalArrival" ? "#fbbf24" : "#38bdf8"} strokeWidth="3" markerEnd="url(#studio-arrow)" />}
      {positions.map(({ node, p }) => { const xy = points.get(node.node_id)!; const fusion = row?.kind === "Fusion" && node.node_id === scenario.fusion_center_node_id;
        return <g key={node.node_id} transform={`translate(${xy.x} ${xy.y})`} onClick={() => select(node.node_id)} onKeyDown={e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); select(node.node_id); } }} role="button" tabIndex={0} aria-label={`节点 ${node.node_id}`}>
          {(selected === node.node_id || fusion) && <circle r="29" fill="none" stroke={fusion ? "#fbbf24" : "#22d3ee"} strokeWidth="2" />}
          <UuvNodeGlyph tone={tone(node.node_id)} />
          <text x="28" y="-3" fill="#edf6ff">N{node.node_id}{fusion ? " · 融合完成" : ""}</text><text x="28" y="15" fill="#9bb0c5" fontSize="11">{(p ?? node.initial_position).z_meters.toFixed(1)} m</text>
        </g>; })}
    </svg><SceneDepthProfile nodes={positions.map(({ node, p }) => ({ node_id: node.node_id, position: p ?? node.initial_position, tone: tone(node.node_id) }))}
      selectedNodeIds={selected ? [selected] : []} onSelectNode={select} simulationTimeNs={row?.at}
      xDomain={transform.xDomain} environmentInputs={environmentInputs} spatialDomain={scenario.spatial_domain} plotWidth={980} plotHeight={180} plotPadding={45} />
    <div className="canvas-caption">虚线：正式轨迹查询样本。{exact ? "端点来自 Runtime 轨迹查询；仅允许在相邻正式样本间做显示插值，不产生仿真状态。" : "当前位置尚未投影，暂显示初始几何；不根据初始速度推算运动。"}</div></>;
}

function RunResultDrawer({ result, events, experiment, complete, onClose }: { result: ResultDto; events: readonly RunEventDto[]; experiment?: ExperimentDto; complete: boolean; onClose: () => void }) {
  useEffect(() => { const close = (event: KeyboardEvent) => { if (event.key === "Escape") onClose(); }; window.addEventListener("keydown", close); return () => window.removeEventListener("keydown", close); }, [onClose]);
  return <div className="result-drawer-backdrop" role="presentation" onMouseDown={onClose}><aside className="result-drawer" role="dialog" aria-modal="true" aria-label="本次运行结果" onMouseDown={event => event.stopPropagation()}><header><h2>本次运行分析</h2><button type="button" autoFocus onClick={onClose}>关闭结果</button></header><ResultAnalysis result={result} events={events} experiment={experiment} eventStreamComplete={complete} /></aside></div>;
}

export function StudioRunPage() {
  const runId = useParams().runId ?? "";
  const [parameters] = useSearchParams();
  const livePresentation = parameters.get("presentation") === "live";
  // Route reuse must not carry the previous Run's display cursor or event stack
  // into a newly created simulation.
  return <StudioRunSession key={`${runId}:${livePresentation}`} runId={runId} livePresentation={livePresentation} />;
}

function StudioRunSession({ runId, livePresentation }: { runId: string; livePresentation: boolean }) {
  const run = useQuery({ ...queries.run(runId), refetchInterval: q => q.state.data?.lifecycle === "Completed" || q.state.data?.lifecycle === "Failed" ? false : 500 });
  const context = useQuery({ ...queries.runDefinitionContext(runId), enabled: !!run.data });
  const environmentInputs = useQuery({ queryKey: ["environment-inputs", run.data?.environment_asset_id], queryFn: () => apiClient.environmentInputs(run.data!.environment_asset_id), enabled: !!run.data, retry: false });
  const result = useQuery({ ...queries.result(runId), enabled: run.data?.lifecycle === "Completed" });
  const { projection, error, streamEnded } = useEvents(runId);
  const [observationLayer, setObservationLayer] = useState<"protocol" | "physical">("protocol");
  const [cursor, setCursor] = useState<number | null>(livePresentation ? -1 : null), [playing, play] = useState(livePresentation), [speed, setSpeed] = useState(1), [compressed, setCompressed] = useState(false);
  const [progressive, setProgressive] = useState(livePresentation), [resultOpen, setResultOpen] = useState(false);
  const [selected, select] = useState<string | null>(null);
  const [visibleKinds, setVisibleKinds] = useState<ReadonlySet<TimelineKind>>(() => new Set(timelineLanes.map(([kind]) => kind)));
  const presentedEvents = useMemo(() => observationLayer === "protocol" ? protocolIntentEvents(projection.timeline) : projection.timeline, [observationLayer, projection.timeline]);
  const rows = useMemo(() => studioEvents(presentedEvents), [presentedEvents]);
  const streamedSamples = useMemo(() => positionSamples(projection.timeline), [projection.timeline]);
  const samples = useMemo(() => mergePositionSamples(result.data?.visualization?.positions ?? [], streamedSamples), [result.data?.visualization?.positions, streamedSamples]);
  const index = Math.min(cursor ?? rows.length - 1, rows.length - 1);
  const row = index >= 0 ? rows[index] : undefined;
  const changeObservationLayer = (layer: "protocol" | "physical") => {
    const nextEvents = layer === "protocol" ? protocolIntentEvents(projection.timeline) : projection.timeline;
    const nextRows = studioEvents(nextEvents);
    // Switching a filter must not reveal the still-unplayed tail of a live
    // presentation. Keep the same actual journal boundary, including empty.
    if (cursor !== null || progressive) {
      let nextIndex = -1;
      if (row) for (let i = nextRows.length - 1; i >= 0; --i) {
        if (BigInt(nextRows[i].event.sequence) <= BigInt(row.event.sequence)) { nextIndex = i; break; }
      }
      setCursor(nextIndex);
    }
    setObservationLayer(layer);
  };
  const nextRow = rows[index + 1];
  useEffect(() => {
    if (!playing || context.isPending || !run.data) return;
    // Stay armed when the live stream momentarily has no further events.
    // Never advance to an event that has not actually arrived from the server.
    if (!nextRow) { if (streamEnded) play(false); return; }
    const delay = replayDelay(row?.at ?? nextRow.at, nextRow.at, speed); setCompressed(delay.compressed);
    const timer = window.setTimeout(() => setCursor(index + 1), delay.milliseconds); return () => clearTimeout(timer);
    // New SSE tail records must not continuously postpone the current step.
  }, [playing, index, nextRow?.key, nextRow?.at, row?.at, speed, streamEnded, context.isPending, Boolean(run.data)]);
  // Only an absent endpoint on an older live server allows limited observation.
  // A corrupt/missing saved definition on a new server is an error, not fallback.
  const olderEndpoint = context.error instanceof ApiFailure && context.error.kind === "NotFound" && context.error.code === "ApiFailure";
  const scene = context.data?.scenario ?? (olderEndpoint ? observedRunScene(samples, result.data) : null);
  const experiment = context.data?.experiment;
  if (run.error || (context.error && !olderEndpoint)) return <ErrorState error={run.error ?? context.error} />;
  if (!run.data || context.isPending) return <LoadingState />;
  if (!scene) return <section className="state-panel">{run.data.failure && <RunFailure failure={run.data.failure} />}<RunActions runId={runId} lifecycle={run.data.lifecycle} /><p>历史服务尚未提供运行配置，等待真实位置记录后可查看回放；不会用预置场景替代。</p>{result.error && <ErrorState error={result.error} />}{result.data && <Link to={`/results/${runId}`}>查看已保存结果</Link>}</section>;
  const visible = row ? presentedEvents.filter(event => BigInt(event.sequence) <= BigInt(row.event.sequence)) : [];
  const counts = projectionCounts(visible), trends = cumulativeTrends(visible);
  const physicalCounts = projectionCounts(row ? projection.timeline.filter(event => BigInt(event.sequence) <= BigInt(row.event.sequence)) : []);
  const node = scene.nodes.find(n => n.node_id === selected);
  const p = node && row ? positionAt(samples, node.node_id, row.at) : null;
  const progress = experiment ? Number(BigInt(counts.cycles) * 100n / (BigInt(experiment.simulation_cycle_count) || 1n)) : null;
  const timelineRows = progressive ? rows.slice(0, index + 1) : rows;
  const firstTime = rows[0]?.at ?? "0", lastTime = rows.at(-1)?.at ?? firstTime;
  const playheadPercent = row ? timePercent(row.at, firstTime, lastTime) : 0;
  const shownCount = Math.max(0, index + 1);
  const lastDelay = trends.delay.at(-1)?.value;
  const eventStack = studioEvents(visible).slice(-30).reverse();
  return <div className="studio-product runtime-workspace" data-presentation={progressive ? "progressive" : "replay"} data-visible-event-count={shownCount}>
    <section className="r4-studio-layout" aria-label="运行观察工作台">
      <aside className="studio-node-rail runtime-experiment-panel" aria-label="实验与节点">
        <div className="runtime-experiment-heading"><span>本次实验</span><Status value={run.data.lifecycle} /></div>
        <h1>{experiment?.name ?? "历史运行回放"}</h1>
        <p className="runtime-mode">{physicalModeLabel(experiment?.phy.physical_layer ?? String(result.data?.provenance?.physical_layer ?? ""))}</p>
        <div className="runtime-time-summary"><div><small>当前展示仿真时间</small><strong title={row ? formatNanoseconds(row.at) : undefined}>{row ? formatNanoseconds(row.at).split(" (")[0] : "等待事件"}</strong></div><div><small>已展示周期</small><strong>{counts.cycles}{experiment ? <small> / {experiment.simulation_cycle_count}</small> : null}</strong></div></div>
        {progress !== null && <progress className="runtime-cycle-progress" aria-label="已展示周期进度" max={100} value={progress} />}
        <RunActions runId={runId} lifecycle={run.data.lifecycle} />
        <details className="runtime-resources"><summary>环境与场景 · 本次冻结配置</summary><dl><dt>环境</dt><dd>{run.data.environment_asset_id}</dd><dt>场景</dt><dd>{run.data.scenario_id} · v{run.data.scenario_version}</dd><dt>运行记录</dt><dd>{runId}</dd></dl></details>
    {olderEndpoint && <p className="playback-truth">旧服务的历史回放：节点取自实际运行位置记录；该服务未提供冻结配置接口，不展示或猜测未保存的参数。</p>}
    {run.data.failure && <RunFailure failure={run.data.failure} />}
    {error?.kind === "TransportUnavailable" && !["Completed", "Failed"].includes(run.data.lifecycle) && <p className="event-connection-notice" role="status">事件连接暂时中断，正在重连。运行状态仍以服务器记录为准，不代表仿真失败。</p>}
    {error && error.kind !== "TransportUnavailable" && <ErrorState error={error} />}{result.error && <ErrorState error={result.error} />}
        <div className="studio-pane-heading"><strong>场景节点</strong><small>{scene.nodes.length} 个 · 点击定位</small></div>
        <div className="runtime-node-list">{scene.nodes.map(n => <button className={selected === n.node_id ? "selected" : ""} key={n.node_id} onClick={() => select(n.node_id)} aria-pressed={selected === n.node_id}><i className={n.node_id === scene.fusion_center_node_id ? "fusion" : "participant"} /><span><strong>N{n.node_id}</strong><small>{n.node_id === scene.fusion_center_node_id ? "融合中心" : "通信节点"}</small></span></button>)}</div>
        <div className="studio-legend"><span><i className="legend-tx" />发送</span><span><i className="legend-arrival" />到达</span><span><i className="legend-success" />成功</span><span><i className="legend-failure" />失败</span></div>
      </aside>
      <article className="studio-surface"><StudioCanvas scenario={scene} samples={samples} row={row} events={visible} selected={selected} select={select} environmentInputs={environmentInputs.data} /></article>
      <aside className="studio-inspector link-inspector runtime-observation-panel" aria-label="通信观察与统计">
        <section className="runtime-view-toolbar"><div><strong>通信观察</strong><select aria-label="通信观测层" value={observationLayer} onChange={(event) => changeObservationLayer(event.target.value as "protocol" | "physical")}><option value="protocol">协议目标链路（默认）</option><option value="physical">完整物理信道（含旁听）</option></select></div><details><summary>显示范围与数据说明</summary><p>{observationLayer === "protocol" ? "只突出当前跳目标及其解码结果；真实旁听事件未删除，可切换完整物理信道查看。" : `当前显示共享声学信道全部候选接收：旁听 ${physicalCounts.overheard}，未解码 ${physicalCounts.notDecoded}。`}</p><p>信道计算不是到达事件；“实际到达”来自信号经过传播时延后的正式事件。</p></details></section>
        <div className="runtime-stat-heading"><strong>当前进度统计</strong><button type="button" disabled={!result.data} onClick={() => setResultOpen(true)}>查看结果</button></div>
        <section className="live-metric-strip" aria-label="当前展示进度统计"><span><small>累计发送</small><b>{counts.transmissions}</b></span><span><small>目标链路交付</small><b>{counts.localDeliveries}</b></span><span><small>解码失败</small><b>{counts.notDecoded}</b></span><span><small>物理旁听</small><b>{physicalCounts.overheard}</b></span><span><small>最近传播时延</small><b>{lastDelay === undefined ? "—" : `${lastDelay.toFixed(3)} s`}</b></span><span><small>融合轮次</small><b>{visible.filter((event) => event.trace.kind === "Fusion").length}</b></span></section>
        {node && <section className="runtime-selected-node" aria-label="节点检查器"><div><strong>N{node.node_id} · 当前位置</strong><button onClick={() => select(null)}>返回事件</button></div><p>{p ? `X ${p.x_meters.toFixed(3)} · Y ${p.y_meters.toFixed(3)} · Z ${p.z_meters.toFixed(3)} m` : "该时刻无位置投影"}</p><small>正式轨迹样本之间仅做显示插值。</small></section>}
        <div className="inspector-heading"><h2>实时事件堆栈</h2><span>最近 {eventStack.length} 条</span></div>
        <div className="event-stack">{eventStack.map((item) => <button type="button" aria-label={`检查事件序列 ${item.event.sequence}`} className={item.key === row?.key ? "selected" : ""} key={item.key} onClick={() => { const target = rows.findIndex((candidate) => candidate.key === item.key); if (target >= 0) { play(false); setCursor(target); } }}><time title={formatNanoseconds(item.at)}>{formatNanoseconds(item.at).split(" (")[0]}</time><strong>{item.title}</strong><small>{eventTechnicalDetail(item.event)}</small></button>)}{!eventStack.length && <p className="runtime-empty-events">等待首个事件，或使用下方时间轴开始观看。</p>}</div>
      </aside>
    </section>
    <section className="studio-history"><div className="studio-history-controls"><strong>离散事件时间轴</strong><button disabled={!rows.length} onClick={() => { setProgressive(false); play(false); setCursor(0); }}>完整回放</button><button disabled={!rows.length} onClick={() => { setProgressive(true); setCursor(-1); play(true); }}>从头演示运行</button><button disabled={!rows.length || index <= 0} onClick={() => { play(false); setCursor(Math.max(0, index - 1)); }}>上一事件</button><button disabled={!rows.length || index >= rows.length - 1} onClick={() => { play(false); setCursor(Math.min(rows.length - 1, index + 1)); }}>下一事件</button><button disabled={!rows.length && !playing} onClick={() => { if (playing) { play(false); return; } setCursor(index >= rows.length - 1 ? progressive ? -1 : 0 : index); play(true); }}>{playing ? "暂停" : "播放"}</button><button onClick={() => { setProgressive(false); play(false); setCursor(null); }}>实时跟随</button><label>观看速度 <select aria-label="回放速度" value={speed} onChange={e => setSpeed(Number(e.target.value))}>{[0.5, 1, 2, 4].map(s => <option key={s} value={s}>{s}×</option>)}</select></label></div>
      <div className="timeline-observation-status" role="status" title={row ? `当前展示：${formatNanoseconds(row.at)}` : "等待首个正式事件"}><span>已展示 {shownCount} / 已收到 {rows.length} 个通信事件{streamEnded ? " · 事件流已读完" : ""}{compressed ? " · 空闲间隔已压缩" : ""}</span></div>
      <details className="timeline-filter-menu"><summary>事件筛选</summary><div className="timeline-filters" aria-label="事件类型筛选">{timelineLanes.map(([kind, label]) => <button aria-pressed={visibleKinds.has(kind)} className={visibleKinds.has(kind) ? "selected" : ""} key={kind} onClick={() => setVisibleKinds(current => { const next = new Set(current); if (next.has(kind)) next.delete(kind); else next.add(kind); return next; })}>{label}</button>)}</div></details>
      <div className="timeline-coordinate-grid" data-time-domain={`${firstTime}:${lastTime}`}>
        <div className="timeline-row timeline-scrub-row"><span>时间定位</span><div className="timeline-plot"><input aria-label="按仿真时间定位" type="range" min="0" max="100" step="0.01" disabled={!rows.length} value={playheadPercent} onChange={e => { setProgressive(false); play(false); setCursor(seekTime(rows, Number(e.target.value))); }} /></div></div>
        <div className="timeline-row timeline-axis-row"><span>仿真时间</span><div className="timeline-plot timeline-axis">{[firstTime, String((BigInt(firstTime) + BigInt(lastTime)) / 2n), lastTime].map((time, i) => <span key={i} className={`tick-${i}`} style={{ left: `${i * 50}%` }} title={formatNanoseconds(time)}>{formatNanoseconds(time).split(" (")[0]}</span>)}</div></div>
        {timelineLanes.map(([kind, label]) => visibleKinds.has(kind) && <div className={`timeline-row event-lane lane-${kind.toLowerCase()}`} key={kind}><span>{label}</span><div className="timeline-plot"><i className="timeline-playhead" aria-hidden="true" style={{ left: `${playheadPercent}%` }} />{timelineRows.map((item, i) => item.kind === kind && <button key={item.key} title={`${item.title} · ${formatNanoseconds(item.at)}`} aria-label={`${item.title} · ${item.key}`} className={i === index ? "selected" : ""} style={{ left: `${timePercent(item.at, firstTime, lastTime)}%` }} onClick={() => { play(false); setCursor(i); }} />)}</div></div>)}
      </div>
      <details className="playback-explanation"><summary>观看进度与 ns-3 状态独立 · 暂停和倍速不改变仿真</summary><p className="playback-truth">{livePresentation ? "本次真实运行采用逐步展示。" : "“从头演示运行”只重播本次日志，不重新计算仿真。"} 展示进度与 ns-3 状态独立：后端可以瞬时完成，已收到的真实事件按仿真时间逐步展示；暂停和倍速仅控制观看。完整回放可查看全部已收到事件。</p></details>
    </section>
    {resultOpen && result.data && <RunResultDrawer result={result.data} events={projection.timeline} experiment={experiment} complete={run.data.event_stream_complete === true} onClose={() => setResultOpen(false)} />}
  </div>;
}

export { StudioResultPage } from "./resultAnalysis";
