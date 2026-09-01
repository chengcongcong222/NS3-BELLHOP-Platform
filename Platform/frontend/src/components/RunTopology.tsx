import type { ScenarioDto } from "../api/types";

interface ActiveLink { sender: string | null; receiver: string; outcome: string }

export function RunTopology({ scenario, active }: { scenario: ScenarioDto; active: ActiveLink | null }) {
  const xs = scenario.nodes.map((node) => node.initial_position.x_meters);
  const ys = scenario.nodes.map((node) => node.initial_position.y_meters);
  const minX = Math.min(...xs); const maxX = Math.max(...xs);
  const minY = Math.min(...ys); const maxY = Math.max(...ys);
  const point = (x: number, y: number) => ({
    x: 50 + ((x - minX) / Math.max(maxX - minX, 1)) * 500,
    y: 285 - ((y - minY) / Math.max(maxY - minY, 1)) * 235,
  });
  const sender = scenario.nodes.find((node) => node.node_id === active?.sender);
  const receiver = scenario.nodes.find((node) => node.node_id === active?.receiver);
  return (
    <svg className="run-topology" viewBox="0 0 600 330" role="img" aria-label="运行通信拓扑">
      <defs><marker id="arrow" markerUnits="userSpaceOnUse" markerWidth="12" markerHeight="12" refX="10" refY="5" orient="auto" viewBox="0 0 12 10"><path d="M0 0L12 5L0 10Z" /></marker></defs>
      <rect x="1" y="1" width="598" height="328" rx="8" className="plot-frame" />
      {sender && receiver && <line
        x1={point(sender.initial_position.x_meters, sender.initial_position.y_meters).x}
        y1={point(sender.initial_position.x_meters, sender.initial_position.y_meters).y}
        x2={point(receiver.initial_position.x_meters, receiver.initial_position.y_meters).x}
        y2={point(receiver.initial_position.x_meters, receiver.initial_position.y_meters).y}
        className={active?.outcome === "NoArrival" ? "active-link failed" : "active-link"}
        markerEnd="url(#arrow)"
      />}
      {scenario.nodes.map((node) => {
        const p = point(node.initial_position.x_meters, node.initial_position.y_meters);
        const role = node.node_id === active?.sender ? "发送" : node.node_id === active?.receiver
          ? active.outcome === "NoArrival" ? "无有效到达" : "接收" : "";
        return <g key={node.node_id} transform={`translate(${p.x} ${p.y})`}><circle r="15" className={node.node_id === active?.sender ? "run-node sender" : node.node_id === active?.receiver ? "run-node receiver" : "run-node"} /><text x="20" y="4" className="node-label">N{node.node_id}</text>{role && <text x="20" y="21" className="activity-label">{role}</text>}</g>;
      })}
      <text x="20" y="315" className="plot-label">正式 Scenario 初始几何；仅用 Run events 高亮通信，不按浏览器时间推演节点运动。</text>
    </svg>
  );
}
