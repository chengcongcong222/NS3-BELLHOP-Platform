import { useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import type { TrajectoryAssetDto } from "../api/types";
import { apiClient } from "../api/client";
import type { ScenarioDraft } from "../domain/workspace";
import { createBoundedSceneView, createRecordedSceneView } from "../domain/worldViewTransform";
import { insideSceneXY, isSceneDomain } from "../domain/sceneSpatial";
import { SceneDepthProfile } from "./SceneDepthProfile";
import { UuvNodeGlyph } from "./UuvNodeGlyph";
import { TrajectoryDialog } from "./TrajectoryDialog";
import { ErrorState } from "./common";

interface Props {
  draft: ScenarioDraft;
  onChange: (draft: ScenarioDraft) => void;
  trajectories?: readonly TrajectoryAssetDto[];
  maximumDepthMeters?: number;
  maximumRangeMeters?: number;
  initialSelectedIds?: readonly string[];
  globalInspector?: ReactNode;
  workspaceControls?: ReactNode;
  onTrajectoryCreated?: () => void;
  environmentInputs?: unknown;
  surfaceZMeters?: number;
  allowRelativePlacement?: boolean;
  applicationReceiverNodeId?: string | null;
  target?: { x: number; y: number };
  onTargetChange?: (target: { x: number; y: number }) => void;
  onTargetSelect?: () => void;
}

// Adapted from legacy StudioCanvas + PropertyInspector's node/scene selection.
// No legacy store, simulation loop, or generated positions are imported.
export function ScenarioDesigner({ draft, onChange, trajectories = [], maximumDepthMeters,
  maximumRangeMeters, initialSelectedIds, globalInspector, workspaceControls, onTrajectoryCreated, environmentInputs, surfaceZMeters, allowRelativePlacement = true, applicationReceiverNodeId, target, onTargetChange, onTargetSelect }: Props) {
  const receiverNodeId = applicationReceiverNodeId === undefined ? draft.fusionCenterNodeId : applicationReceiverNodeId;
  const [selectedIds, setSelectedIds] = useState<string[]>(() => (initialSelectedIds ?? [])
    .filter(id => draft.nodes.some(node => node.node_id === id)).slice(0, 2));
  const [showNodes, setShowNodes] = useState(!!workspaceControls || !globalInspector);
  const [showDepth, setShowDepth] = useState(true);
  const dragging = useRef<string | null>(null);
  const draggingTarget = useRef(false);
  const [dragView, setDragView] = useState<ReturnType<typeof createRecordedSceneView> | null>(null);
  const [bindingError, setBindingError] = useState<string | Error>("");
  const [derived, setDerived] = useState<TrajectoryAssetDto[]>([]);
  const available = [...trajectories, ...derived.filter(item => !trajectories.some(other => other.asset_id === item.asset_id && other.version === item.version))];
  const [placement, setPlacement] = useState<{ x: number; y: number; depth: number } | null>(null);
  const [placing, setPlacing] = useState(false);
  const [trajectoryNodeId, setTrajectoryNodeId] = useState<string | null>(null);
  const canvasRef = useRef<SVGSVGElement>(null);
  const [{ width: viewWidth, height: viewHeight }, setViewport] = useState({ width: 700, height: 360 });
  useEffect(() => {
    const svg = canvasRef.current;
    if (!svg || typeof ResizeObserver === "undefined") return;
    const observer = new ResizeObserver(([entry]) => {
      // Display pixels, not a stretched fixed-width drawing: keep icons and text
      // readable and pass the same X viewport to the depth section below.
      if (entry.contentRect.width > 0 && entry.contentRect.height > 0) setViewport({ width: entry.contentRect.width, height: entry.contentRect.height });
    });
    observer.observe(svg); return () => observer.disconnect();
  }, []);
  const currentDraft = useRef(draft); currentDraft.current = draft;
  const selected = draft.nodes.find(node => node.node_id === selectedIds[0]) ??
    (globalInspector ? undefined : draft.nodes[0]);
  const peer = draft.nodes.find(node => node.node_id === selectedIds[1]);
  // Bellhop's horizontal_range is pairwise separation, not a georeferenced XY rectangle.
  const domain = isSceneDomain(draft.spatialDomain) ? draft.spatialDomain : undefined;
  const computedTransform = useMemo(() => domain ? createBoundedSceneView(domain, { width: viewWidth, height: viewHeight, padding: 50 })
    : createRecordedSceneView([...draft.nodes.map(node => node.initial_position), ...(target && Number.isFinite(target.x) && Number.isFinite(target.y) ? [{ x_meters: target.x, y_meters: target.y, z_meters: 0 }] : [])], { width: viewWidth, height: viewHeight, padding: 50 }),
    [draft.nodes, domain, viewWidth, viewHeight, target?.x, target?.y]);
  const transform = dragView ?? computedTransform;
  const distance = selected && peer ? Math.hypot(selected.initial_position.x_meters - peer.initial_position.x_meters,
    selected.initial_position.y_meters - peer.initial_position.y_meters, selected.initial_position.z_meters - peer.initial_position.z_meters) : null;
  const select = (id: string, pair: boolean) => {
    setBindingError("");
    setPlacement(null);
    setSelectedIds(current => pair && current[0] && current[0] !== id ? [current[0], id] : [id]);
  };
  const update = (patch: Partial<NonNullable<typeof selected>>) => selected &&
    onChange({ ...draft, nodes: draft.nodes.map(node => node.node_id === selected.node_id ? { ...node, ...patch } : node) });
  const add = () => {
    let id = 0n; const ids = new Set(draft.nodes.map(node => node.node_id));
    while (ids.has(String(id))) id += 1n;
    const node = { node_id: String(id), can_transmit: true, can_receive: true, duplex_mode: "HalfDuplex",
      initial_position: { x_meters: 0, y_meters: 0, z_meters: -20 },
      initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } };
    onChange({ ...draft, nodes: [...draft.nodes, node] }); select(node.node_id, false);
  };
  const trajectoryBinding = selected ? draft.trajectoryBindings?.find(binding => binding.node_id === selected.node_id) : undefined;
  const absoluteBinding = !!trajectoryBinding && trajectoryBinding.placement_mode !== "RelativeToNodeStart";
  const boundAsset = available.find(asset => asset.asset_id === trajectoryBinding?.asset_id && asset.version === trajectoryBinding.version);
  const boundTrack = boundAsset?.tracks.find(track => track.track_id === trajectoryBinding?.track_id);
  const placeTrajectory = async () => {
    if (!selected || !placement || !trajectoryBinding || !boundAsset || placing) return;
    if (![placement.x, placement.y, placement.depth].every(Number.isFinite) || placement.depth < 0) { setBindingError("坐标必须为有限数值，水深不能为负数。"); return; }
    const nodeId = selected.node_id, source = trajectoryBinding;
    setPlacing(true); setBindingError("");
    try {
      const asset = await apiClient.translateTrajectory({ source_asset_id: source.asset_id, source_version: source.version,
        source_track_id: source.track_id, asset_id: `placement-${crypto.randomUUID()}`, name: `${boundAsset.name} · N${nodeId} 布设`,
        x_m: placement.x, y_m: placement.y, z_m: -placement.depth });
      setDerived(values => [...values, asset]); onTrajectoryCreated?.();
      const latest = currentDraft.current;
      const stillBound = latest.trajectoryBindings?.some(binding => binding.node_id === nodeId && binding.asset_id === source.asset_id && binding.version === source.version && binding.track_id === source.track_id);
      if (!stillBound || !latest.nodes.some(node => node.node_id === nodeId)) {
        setBindingError("新轨迹已保存，但该节点的绑定已变化，未覆盖当前设置。可以从目录手动选择新轨迹。"); return;
      }
      const origin = asset.tracks[0].points[0];
      onChange({ ...latest, nodes: latest.nodes.map(node => node.node_id === nodeId ? { ...node, initial_position: { x_meters: origin.x_m, y_meters: origin.y_m, z_meters: origin.z_m } } : node),
        trajectoryBindings: latest.trajectoryBindings!.map(binding => binding.node_id === nodeId ? { node_id: nodeId, asset_id: asset.asset_id, version: asset.version, track_id: asset.tracks[0].track_id } : binding) });
      setPlacement(null);
    } catch (error) { setBindingError(new Error("轨迹另存失败，原绑定和节点位置未改变。请检查起点设置后重试。", { cause: error })); }
    finally { setPlacing(false); }
  };
  const drag = (event: React.PointerEvent<SVGSVGElement>) => {
    if (draggingTarget.current && onTargetChange && event.buttons === 1) {
      const ctm = event.currentTarget.getScreenCTM(); if (!ctm) return;
      const point = new DOMPoint(event.clientX, event.clientY).matrixTransform(ctm.inverse());
      onTargetChange({ x: Math.round(transform.xDomain[0] + (point.x - 50) / (viewWidth - 100) * (transform.xDomain[1] - transform.xDomain[0])),
        y: Math.round(transform.yDomain[1] - (point.y - 50) / (viewHeight - 100) * (transform.yDomain[1] - transform.yDomain[0])) });
      return;
    }
    if (!selected || dragging.current !== selected.node_id || event.buttons !== 1 || absoluteBinding) return;
    const ctm = event.currentTarget.getScreenCTM(); if (!ctm) return;
    const point = new DOMPoint(event.clientX, event.clientY).matrixTransform(ctm.inverse());
    const x = Math.round(transform.xDomain[0] + (point.x - 50) / (viewWidth - 100) * (transform.xDomain[1] - transform.xDomain[0]));
    const y = Math.round(transform.yDomain[1] - (point.y - 50) / (viewHeight - 100) * (transform.yDomain[1] - transform.yDomain[0]));
    if (domain && !insideSceneXY(domain, x, y)) { setBindingError("位置超出本实验范围，未移动节点。可在环境与实验参数中调整范围。"); return; }
    setBindingError(""); update({ initial_position: { ...selected.initial_position, x_meters: x, y_meters: y } });
  };
  const updateTrajectoryBinding = (encoded: string) => {
    if (!selected) return;
    const retained = (draft.trajectoryBindings ?? []).filter(binding => binding.node_id !== selected.node_id);
    if (!encoded) { onChange({ ...draft, trajectoryBindings: retained }); return; }
    const [asset_id, version, track_id] = JSON.parse(encoded) as [string, number, string];
    const track = available.find(asset => asset.asset_id === asset_id && asset.version === version)?.tracks.find(track => track.track_id === track_id);
    const origin = track?.points.find(point => point.time_s === 0);
    if (!origin) { setBindingError("该轨道没有仿真起点 t=0 的位置，不能用于当前实验。请生成从 0 秒开始的轨迹。"); return; }
    setBindingError("");
    onChange({ ...draft,
      nodes: allowRelativePlacement ? draft.nodes : draft.nodes.map(node => node.node_id === selected.node_id ? { ...node, initial_position: { x_meters: origin.x_m, y_meters: origin.y_m, z_meters: origin.z_m } } : node),
      trajectoryBindings: [...retained, { node_id: selected.node_id, asset_id, version, track_id,
      placement_mode: allowRelativePlacement ? "RelativeToNodeStart" : "Absolute" }] });
  };
  return <div className={`scenario-studio with-depth-profile ${globalInspector ? "unified-scene" : ""} ${workspaceControls ? "with-workspace-controls" : ""} ${showNodes ? "with-node-list" : ""}`}>
    {(showNodes || workspaceControls) && <aside className="studio-tools" aria-label="场景节点工具">
      {workspaceControls}
      {showNodes && <><div className="studio-pane-title"><span>节点 · {draft.nodes.length}</span><button type="button" onClick={add} aria-label="＋ 添加节点">＋ 添加</button></div>
      <div className="node-tool-list">{draft.nodes.map(node => <button type="button" key={node.node_id}
        className={selectedIds.includes(node.node_id) ? "selected" : ""} onClick={event => select(node.node_id, event.shiftKey)}>
        <i className={node.node_id === receiverNodeId ? "fusion" : "participant"} /><span><strong>N{node.node_id}</strong>
        <small>{node.can_transmit ? node.can_receive ? "可发送 · 可接收" : "仅发送" : node.can_receive ? "仅接收 · 不发送" : "收发均关闭"}</small>{node.node_id === receiverNodeId && <small>当前算法处理节点</small>}</span></button>)}</div>
      <div className="tool-help"><span>Shift + 第二节点：测距</span><span>复用路径，起点在场景中布设。</span></div></>}
    </aside>}
    <section className="studio-canvas">
      <div className="canvas-toolbar">
        {globalInspector && <><button type="button" aria-expanded={showNodes} onClick={() => setShowNodes(!showNodes)}>节点列表</button>
          {!showNodes && <button type="button" onClick={add} aria-label="＋ 添加节点">＋ 添加节点</button>}
          <span className="canvas-view-label">俯视图</span>
          <button type="button" aria-pressed={showDepth} className={showDepth ? "selected" : ""} onClick={() => setShowDepth(!showDepth)}>深度</button>
          <button type="button" onClick={() => setSelectedIds([])}>环境与实验参数</button></>}
        {!globalInspector && <strong>平面布设</strong>}
        <span>局部坐标 · 米 · 等比例</span>
      </div>
      <svg ref={canvasRef} viewBox={`0 0 ${viewWidth} ${viewHeight}`} role="img" aria-label="可编辑场景画布"
        onPointerMove={drag} onPointerUp={() => { dragging.current = null; draggingTarget.current = false; setDragView(null); }} onPointerCancel={() => { dragging.current = null; draggingTarget.current = false; setDragView(null); }}
        onPointerDown={() => { if (globalInspector) setSelectedIds([]); }}>
        <defs><pattern id="studio-grid" width="58" height="52" patternUnits="userSpaceOnUse"><path d="M58 0H0V52" className="grid-line" /></pattern></defs>
        <rect width={viewWidth} height={viewHeight} className="water-boundary" /><rect x="50" y="50" width={Math.max(1, viewWidth - 100)} height={Math.max(1, viewHeight - 100)} fill="url(#studio-grid)" className="workspace-boundary" />
        {domain && <rect className="scenario-domain-boundary" x={transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_max_m }).x}
          y={transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_max_m }).y}
          width={transform.point({ x_meters: domain.x_max_m, y_meters: domain.y_max_m }).x - transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_max_m }).x}
          height={transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_min_m }).y - transform.point({ x_meters: domain.x_min_m, y_meters: domain.y_max_m }).y} />}
        {[0, .25, .5, .75, 1].map(factor => {
          const x = transform.xDomain[0] + factor * (transform.xDomain[1] - transform.xDomain[0]);
          const y = transform.yDomain[1] - factor * (transform.yDomain[1] - transform.yDomain[0]);
          return <g key={factor} className="studio-axis-label"><text x={50 + (viewWidth - 100) * factor} y={viewHeight - 28} textAnchor="middle">{Math.round(x)}</text>
            <text x="44" y={54 + (viewHeight - 100) * factor} textAnchor="end">{Math.round(y)}</text></g>;
        })}<text x={viewWidth - 50} y="22" textAnchor="end" className="studio-axis-label">X / m</text><text x="25" y="22" className="studio-axis-label">Y / m</text>
        {selected && peer && <line x1={transform.point(selected.initial_position).x} y1={transform.point(selected.initial_position).y}
          x2={transform.point(peer.initial_position).x} y2={transform.point(peer.initial_position).y} className="geometry-link measured" />}
        {target && Number.isFinite(target.x) && Number.isFinite(target.y) && <g role="button" aria-label="探测目标（拖动或方向键调整）" tabIndex={0} className="scenario-target"
          transform={`translate(${transform.point({ x_meters: target.x, y_meters: target.y }).x} ${transform.point({ x_meters: target.x, y_meters: target.y }).y})`}
          onPointerDown={event => { event.stopPropagation(); event.currentTarget.setPointerCapture?.(event.pointerId); setDragView(transform); draggingTarget.current = true; dragging.current = null; setSelectedIds([]); onTargetSelect?.(); }}
          onKeyDown={event => { const delta: Record<string, [number, number]> = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, 1], ArrowDown: [0, -1] }; if (delta[event.key] && onTargetChange) { event.preventDefault(); const [dx, dy] = delta[event.key]; onTargetChange({ x: target.x + dx * (event.shiftKey ? 10 : 1), y: target.y + dy * (event.shiftKey ? 10 : 1) }); } }}>
          <title>模拟目标，不是融合估计结果</title><circle r="18" fill="white" stroke="#a63f12" strokeWidth="2" /><circle r="7" fill="none" stroke="#a63f12" strokeWidth="2" /><path d="M-24 0H24 M0 -24V24" stroke="#a63f12" strokeWidth="2" /><text x="27" y="-5" className="node-label">探测目标</text><text x="27" y="12" className="node-depth">({target.x}, {target.y}) m</text>
        </g>}
        {draft.nodes.map(node => { const point = transform.point(node.initial_position); return <g key={node.node_id}
          role="button" aria-label={`选择节点 N${node.node_id}`} tabIndex={0} transform={`translate(${point.x} ${point.y})`}
          onKeyDown={event => { if (event.key === "Enter") select(node.node_id, event.shiftKey); }}
          onPointerDown={event => { event.stopPropagation(); event.currentTarget.setPointerCapture?.(event.pointerId); select(node.node_id, event.shiftKey); dragging.current = node.node_id; }}
          className="draggable-node">
          <UuvNodeGlyph tone={selectedIds[0] === node.node_id ? "selected" : selectedIds[1] === node.node_id ? "peer" : node.node_id === receiverNodeId ? "fusion" : "standard"} />
          <circle r="25" className={selectedIds.includes(node.node_id) ? "selection-halo" : "selection-halo hidden"} />
          <text x="28" y="5" className="node-label">N{node.node_id}</text><text x="28" y="20" className="node-depth">{(surfaceZMeters ?? 0) - node.initial_position.z_meters} m 深</text>
        </g>; })}
        {!draft.nodes.length && <text x={viewWidth / 2} y={viewHeight / 2} textAnchor="middle" className="studio-axis-label">从“添加节点”开始布设实验</text>}
      </svg>
      <SceneDepthProfile nodes={draft.nodes.map(node => ({ node_id: node.node_id, position: node.initial_position,
        tone: node.node_id === receiverNodeId ? "fusion" : "standard" }))}
        selectedNodeIds={selectedIds} onSelectNode={select} xDomain={transform.xDomain}
        spatialDomain={domain} plotWidth={viewWidth} plotHeight={workspaceControls ? 190 : undefined} plotPadding={50}
        environmentInputs={environmentInputs} surfaceZMeters={surfaceZMeters} expanded={showDepth} onExpandedChange={setShowDepth} />
      <div className="studio-statusbar"><span>{selected ? `N${selected.node_id} · (${selected.initial_position.x_meters}, ${selected.initial_position.y_meters}, ${selected.initial_position.z_meters}) m` : "点击节点编辑属性；点击背景配置实验"}</span>
        <span>{distance === null ? "Shift + 选择第二节点测距" : `距离 ${distance.toFixed(1)} m`}</span></div>
    </section>
    <aside className="property-panel" aria-label="节点与实验属性">
      {!selected && globalInspector ? globalInspector : <>
        <div className="studio-pane-title"><span>节点属性</span><small>{selected ? `N${selected.node_id}` : "未选择"}</small></div>
        {selected && <>
          <label>节点编号<input value={selected.node_id} disabled /></label><h3>初始位置</h3>
          <div className="form-grid">
            <label>X（m）<input aria-label="X（m）" type="number" min={domain?.x_min_m} max={domain?.x_max_m} disabled={absoluteBinding} value={selected.initial_position.x_meters} onChange={event => update({ initial_position: { ...selected.initial_position, x_meters: Number(event.target.value) } })} /></label>
            <label>Y（m）<input aria-label="Y（m）" type="number" min={domain?.y_min_m} max={domain?.y_max_m} disabled={absoluteBinding} value={selected.initial_position.y_meters} onChange={event => update({ initial_position: { ...selected.initial_position, y_meters: Number(event.target.value) } })} /></label>
            <label>深度（m）<input type="number" min="0" disabled={absoluteBinding} value={Math.abs(selected.initial_position.z_meters)} onChange={event => update({ initial_position: { ...selected.initial_position, z_meters: -Math.abs(Number(event.target.value)) } })} /></label>
          </div>
          {domain && !insideSceneXY(domain, selected.initial_position.x_meters, selected.initial_position.y_meters) && <p role="alert">起点超出已选范围，请调整坐标；运行前还会检查完整路径。</p>}
          <h3>场景运动路径</h3><button type="button" onClick={() => setTrajectoryNodeId(selected.node_id)}>新增轨迹</button><label>绑定轨迹<select aria-label="绑定轨迹资源" value={trajectoryBinding ? JSON.stringify([trajectoryBinding.asset_id, trajectoryBinding.version, trajectoryBinding.track_id]) : ""} onChange={event => updateTrajectoryBinding(event.target.value)}>
            <option value="">尚未绑定</option>{available.flatMap(asset => asset.tracks.map(track => <option key={JSON.stringify([asset.asset_id, asset.version, track.track_id])} value={JSON.stringify([asset.asset_id, asset.version, track.track_id])}>{asset.name} · v{asset.version} · {track.track_id}</option>))}
          </select></label>
          {bindingError && <ErrorState error={typeof bindingError === "string" ? new Error(bindingError) : bindingError} />}
          <p className="property-help">{trajectoryBinding ? absoluteBinding ? "旧绑定保留轨迹原始坐标。切换为跟随场景起点后，可直接拖拽或输入坐标布设，不修改原轨迹。" : "共用轨迹的相对路径；起点由本节点决定，可直接编辑或拖拽。运行前自动生成完整位置记录，不改变原轨迹、速度和时间。" : "请选择从 0 秒开始的路径。多个节点可以共用一条路径，分别设置起点；静止节点也使用轨迹。"}</p>
          {!allowRelativePlacement && <p className="property-help">当前服务尚未支持场景起点布设，仍使用轨迹原坐标。请在新版服务中启用复用路径。</p>}
          {absoluteBinding && allowRelativePlacement && <button type="button" onClick={() => onChange({ ...draft, trajectoryBindings: draft.trajectoryBindings?.map(binding => binding.node_id === selected.node_id ? { ...binding, placement_mode: "RelativeToNodeStart" } : binding) })}>改为跟随场景起点</button>}
          {boundTrack && <p className="trajectory-validity">有效时段 {boundTrack.points[0].time_s}～{boundTrack.points.at(-1)!.time_s} s · {boundTrack.points.length} 个位置样本</p>}
          {boundAsset && absoluteBinding && <button type="button" disabled={placing} onClick={() => setPlacement({ x: selected.initial_position.x_meters, y: selected.initial_position.y_meters, depth: Math.abs(selected.initial_position.z_meters) })}>调整轨迹起点</button>}
          {placement && <fieldset className="trajectory-placement" disabled={placing}><legend>平移路径，另存并绑定</legend><div className="form-grid">
            <label>新起点 X（m）<input type="number" value={placement.x} onChange={e => setPlacement({ ...placement, x: Number(e.target.value) })} /></label>
            <label>新起点 Y（m）<input type="number" value={placement.y} onChange={e => setPlacement({ ...placement, y: Number(e.target.value) })} /></label>
            <label>新起点水深（m）<input type="number" min="0" value={placement.depth} onChange={e => setPlacement({ ...placement, depth: Number(e.target.value) })} /></label>
          </div><p>只平移位置，不改变航行速度、采样时刻或时长。完成后仍需保存实验。</p><button type="button" onClick={placeTrajectory}>{placing ? "正在另存…" : "另存新轨迹并绑定"}</button><button type="button" onClick={() => setPlacement(null)}>取消</button></fieldset>}
          <h3>硬件收发能力</h3><label className="check"><input type="checkbox" checked={selected.can_transmit} onChange={event => update({ can_transmit: event.target.checked })} /> 可发送</label>
          <label className="check"><input type="checkbox" checked={selected.can_receive} onChange={event => update({ can_receive: event.target.checked })} /> 可接收</label>
          <p className="property-help">通信流与应用接收点在“环境与实验参数 → 网络”中配置，不属于硬件能力。</p>
          <button type="button" className="danger-button" disabled={draft.nodes.length <= 1} onClick={() => {
            const nodes = draft.nodes.filter(node => node.node_id !== selected.node_id);
            onChange({ ...draft, nodes, trajectoryBindings: (draft.trajectoryBindings ?? []).filter(binding => binding.node_id !== selected.node_id),
              fusionCenterNodeId: draft.fusionCenterNodeId === selected.node_id ? null : draft.fusionCenterNodeId });
            setSelectedIds([]);
          }}>删除节点</button>
        </>}
      </>}
    </aside>
    {trajectoryNodeId && draft.nodes.some(node => node.node_id === trajectoryNodeId) && <TrajectoryDialog
      nodeId={trajectoryNodeId}
      initialPosition={draft.nodes.find(node => node.node_id === trajectoryNodeId)!.initial_position}
      onClose={() => setTrajectoryNodeId(null)} onChoose={(asset, trackId) => {
        const latest = currentDraft.current, nodeId = trajectoryNodeId;
        const origin = asset.tracks.find(track => track.track_id === trackId)?.points.find(point => point.time_s === 0);
        if (!origin || !latest.nodes.some(node => node.node_id === nodeId)) { setBindingError("轨迹需覆盖 0 秒且目标节点仍存在，未修改绑定。"); return; }
        setDerived(values => [...values, asset]); onTrajectoryCreated?.();
        onChange({ ...latest,
          nodes: allowRelativePlacement ? latest.nodes : latest.nodes.map(node => node.node_id === nodeId ? { ...node, initial_position: { x_meters: origin.x_m, y_meters: origin.y_m, z_meters: origin.z_m } } : node),
          trajectoryBindings: [...(latest.trajectoryBindings ?? []).filter(binding => binding.node_id !== nodeId),
            { node_id: nodeId, asset_id: asset.asset_id, version: asset.version, track_id: trackId, placement_mode: allowRelativePlacement ? "RelativeToNodeStart" : "Absolute" }] });
        setTrajectoryNodeId(null);
      }} />}
  </div>;
}
