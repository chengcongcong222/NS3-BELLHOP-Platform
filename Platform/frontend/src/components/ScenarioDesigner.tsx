import { useMemo, useState } from "react";
import type { ScenarioDraft } from "../domain/workspace";

interface Props { draft: ScenarioDraft; onChange: (draft: ScenarioDraft) => void }

export function ScenarioDesigner({ draft, onChange }: Props) {
  const [selectedId, setSelectedId] = useState(draft.nodes[0]?.node_id ?? "");
  const selected = draft.nodes.find((node) => node.node_id === selectedId);
  const bounds = useMemo(() => {
    const points = draft.nodes.flatMap((node) => [Math.abs(node.initial_position.x_meters), Math.abs(node.initial_position.y_meters)]);
    return Math.max(1000, ...points) * 1.25;
  }, [draft.nodes]);
  const view = (value: number) => 300 + (value / bounds) * 250;
  const updateNode = (patch: Partial<NonNullable<typeof selected>>) => {
    onChange({ ...draft, nodes: draft.nodes.map((node) => node.node_id === selectedId ? { ...node, ...patch } : node) });
  };
  const addNode = () => {
    let id = 0n;
    const ids = new Set(draft.nodes.map((node) => node.node_id));
    while (ids.has(id.toString())) id += 1n;
    const node = { node_id: id.toString(), can_transmit: true, can_receive: true, duplex_mode: "HalfDuplex", initial_position: { x_meters: 0, y_meters: 0, z_meters: -20 }, initial_velocity: { x_meters_per_second: 0, y_meters_per_second: 0, z_meters_per_second: 0 } };
    onChange({ ...draft, nodes: [...draft.nodes, node] }); setSelectedId(node.node_id);
  };
  const pointer = (event: React.PointerEvent<SVGSVGElement>) => {
    if (!selected || event.buttons !== 1) return;
    const rect = event.currentTarget.getBoundingClientRect();
    const x = ((event.clientX - rect.left) / rect.width) * 600;
    const y = ((event.clientY - rect.top) / rect.height) * 600;
    updateNode({ initial_position: { ...selected.initial_position, x_meters: Math.round(((x - 300) / 250) * bounds), y_meters: Math.round(((300 - y) / 250) * bounds) } });
  };
  return (
    <div className="designer-layout">
      <section className="designer-canvas">
        <div className="canvas-toolbar"><strong>二维场景画布</strong><span>拖动节点或使用右侧精确输入</span><button type="button" onClick={addNode}>＋ 添加节点</button></div>
        <svg viewBox="0 0 600 600" role="img" aria-label="可编辑场景画布" onPointerMove={pointer}>
          <defs><pattern id="grid" width="50" height="50" patternUnits="userSpaceOnUse"><path d="M50 0H0V50" className="grid-line" /></pattern></defs>
          <rect width="600" height="600" fill="url(#grid)" /><line x1="300" y1="0" x2="300" y2="600" className="plot-axis" /><line x1="0" y1="300" x2="600" y2="300" className="plot-axis" />
          {draft.nodes.flatMap((from, index) => draft.nodes.slice(index + 1).map((to) => <line key={`${from.node_id}-${to.node_id}`} x1={view(from.initial_position.x_meters)} y1={view(-from.initial_position.y_meters)} x2={view(to.initial_position.x_meters)} y2={view(-to.initial_position.y_meters)} className="geometry-link" />))}
          {draft.nodes.map((node) => <g key={node.node_id} transform={`translate(${view(node.initial_position.x_meters)} ${view(-node.initial_position.y_meters)})`} onPointerDown={(event) => { event.currentTarget.setPointerCapture(event.pointerId); setSelectedId(node.node_id); }} className="draggable-node"><circle r={node.node_id === draft.fusionCenterNodeId ? 18 : 14} className={node.node_id === selectedId ? "node-selected" : node.node_id === draft.fusionCenterNodeId ? "node-fusion" : "node-participant"} /><text x="20" y="5" className="node-label">N{node.node_id}</text></g>)}
        </svg>
        <div className="depth-strip" aria-label="节点深度剖面">{draft.nodes.map((node, index) => <span key={node.node_id} style={{ left: `${8 + index * (84 / Math.max(draft.nodes.length - 1, 1))}%`, top: `${Math.min(88, Math.max(8, Math.abs(node.initial_position.z_meters) / Math.max(...draft.nodes.map((item) => Math.abs(item.initial_position.z_meters)), 1) * 80))}%` }}>N{node.node_id} · {Math.abs(node.initial_position.z_meters)} m</span>)}</div>
      </section>
      <aside className="property-panel">
        <h2>节点属性</h2>
        {!selected ? <p>请选择节点。</p> : <>
          <label>节点编号<input value={selected.node_id} disabled /></label>
          <div className="form-grid"><label>X（m）<input type="number" value={selected.initial_position.x_meters} onChange={(event) => updateNode({ initial_position: { ...selected.initial_position, x_meters: Number(event.target.value) } })} /></label><label>Y（m）<input type="number" value={selected.initial_position.y_meters} onChange={(event) => updateNode({ initial_position: { ...selected.initial_position, y_meters: Number(event.target.value) } })} /></label><label>深度（m）<input type="number" min="0" value={Math.abs(selected.initial_position.z_meters)} onChange={(event) => updateNode({ initial_position: { ...selected.initial_position, z_meters: -Math.abs(Number(event.target.value)) } })} /></label></div>
          <label className="check"><input type="checkbox" checked={selected.can_transmit} onChange={(event) => updateNode({ can_transmit: event.target.checked })} /> 可发送</label><label className="check"><input type="checkbox" checked={selected.can_receive} onChange={(event) => updateNode({ can_receive: event.target.checked })} /> 可接收</label><label className="check"><input type="radio" checked={draft.fusionCenterNodeId === selected.node_id} onChange={() => onChange({ ...draft, fusionCenterNodeId: selected.node_id })} /> 设为融合中心</label>
          <h3>运动速度（m/s）</h3><div className="form-grid"><label>Vx<input type="number" value={selected.initial_velocity.x_meters_per_second} onChange={(event) => updateNode({ initial_velocity: { ...selected.initial_velocity, x_meters_per_second: Number(event.target.value) } })} /></label><label>Vy<input type="number" value={selected.initial_velocity.y_meters_per_second} onChange={(event) => updateNode({ initial_velocity: { ...selected.initial_velocity, y_meters_per_second: Number(event.target.value) } })} /></label></div>
          <button type="button" className="danger-button" disabled={draft.nodes.length <= 1} onClick={() => { const nodes = draft.nodes.filter((node) => node.node_id !== selected.node_id); onChange({ ...draft, nodes, fusionCenterNodeId: draft.fusionCenterNodeId === selected.node_id ? nodes[0].node_id : draft.fusionCenterNodeId }); setSelectedId(nodes[0]?.node_id ?? ""); }}>删除节点</button>
        </>}
      </aside>
    </div>
  );
}
