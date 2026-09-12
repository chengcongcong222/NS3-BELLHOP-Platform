import type { ScenarioDto } from "../api/types";
import { createWorldViewTransform } from "../domain/worldViewTransform";

function isMoving(node: ScenarioDto["nodes"][number]): boolean {
  const velocity = node.initial_velocity;
  return velocity.x_meters_per_second !== 0 ||
    velocity.y_meters_per_second !== 0 ||
    velocity.z_meters_per_second !== 0;
}

export function ScenarioTopology({ scenario }: { scenario: ScenarioDto }) {
  const fusionCenter = scenario.nodes.find(
    (node) => node.node_id === scenario.fusion_center_node_id,
  );
  const transform = createWorldViewTransform(scenario.nodes.map(n => n.initial_position), undefined,
    { width: 600, height: 320, padding: 42 });
  const point = (x: number, y: number) => transform.point({ x_meters: x, y_meters: y });

  return (
    <div className="topology-layout">
      <svg
        className="topology-plot"
        viewBox="0 0 600 320"
        role="img"
        aria-label="Scenario initial topology"
      >
        <rect x="1" y="1" width="598" height="318" rx="4" className="plot-frame" />
        <line x1="42" y1="278" x2="558" y2="278" className="plot-axis" />
        <line x1="42" y1="42" x2="42" y2="278" className="plot-axis" />
        <text x="550" y="300" className="plot-label">x (m)</text>
        <text x="12" y="32" className="plot-label">y (m)</text>
        {scenario.nodes.map((node) => {
          const position = point(node.initial_position.x_meters, node.initial_position.y_meters);
          const fusion = node.node_id === scenario.fusion_center_node_id;
          return (
            <g key={node.node_id} transform={`translate(${position.x} ${position.y})`}>
              <circle r={fusion ? 12 : 9} className={fusion ? "node-fusion" : "node-participant"} />
              {isMoving(node) && <path d="M -14 -16 L 14 -16 L 9 -21 M 14 -16 L 9 -11" className="node-motion" />}
              <text x="15" y="5" className="node-label">N{node.node_id}</text>
            </g>
          );
        })}
      </svg>
      <div className="topology-legend" aria-label="Topology legend">
        <span><i className="legend-fusion" />{fusionCenter && isMoving(fusionCenter) ? "移动融合中心" : "固定融合中心"}</span>
        <span><i className="legend-node" />参与节点</span>
        <span>箭头：轨迹生成输入速度非零</span>
        <small>仅显示 Scenario DTO 的初始 x/y 几何；不表示实时轨迹。深度见节点表（m）。</small>
        <small>正式运行统一查询轨迹；绑定资源详情尚未发布。</small>
      </div>
    </div>
  );
}
