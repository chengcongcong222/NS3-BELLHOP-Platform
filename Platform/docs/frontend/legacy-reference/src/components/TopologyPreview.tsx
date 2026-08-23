import type { DemoNode, LinkMetric } from '../types';

interface TopologyPreviewProps {
  nodes: DemoNode[];
  metrics: LinkMetric[];
}

function mapNodes(nodes: DemoNode[]) {
  const xValues = nodes.map((node) => node.position[0]);
  const yValues = nodes.map((node) => node.position[1]);
  const minX = Math.min(...xValues);
  const maxX = Math.max(...xValues);
  const minY = Math.min(...yValues);
  const maxY = Math.max(...yValues);
  const width = maxX - minX || 1;
  const height = maxY - minY || 1;

  return nodes.map((node) => ({
    ...node,
    svgX: 70 + ((node.position[0] - minX) / width) * 300,
    svgY: 260 - ((node.position[1] - minY) / height) * 180,
  }));
}

export function TopologyPreview({ nodes, metrics }: TopologyPreviewProps) {
  const mappedNodes = mapNodes(nodes);

  return (
    <div className="topology-preview">
      <svg viewBox="0 0 440 300" className="topology-preview__svg" role="img" aria-label="链路拓扑预览">
        <rect x="0" y="0" width="440" height="300" rx="28" fill="rgba(4, 16, 32, 0.9)" />
        {metrics.map((metric) => {
          const tx = mappedNodes.find((node) => node.id === metric.tx_id);
          const rx = mappedNodes.find((node) => node.id === metric.rx_id);
          if (!tx || !rx) {
            return null;
          }
          const nlos = metric.is_nlos === 1;

          return (
            <g key={`${metric.tx_id}-${metric.rx_id}`}>
              <line
                x1={tx.svgX}
                y1={tx.svgY}
                x2={rx.svgX}
                y2={rx.svgY}
                stroke={nlos ? 'rgba(239, 68, 68, 0.85)' : 'rgba(34, 211, 238, 0.8)'}
                strokeWidth="3"
                strokeDasharray={nlos ? '4 4' : '8 6'}
              />
              <text x={(tx.svgX + rx.svgX) / 2} y={(tx.svgY + rx.svgY) / 2 - 10} className="topology-preview__label">
                {nlos ? 'NLOS' : `${metric.delay_s.toFixed(3)} s`}
              </text>
            </g>
          );
        })}
        {mappedNodes.map((node) => (
          <g key={node.id}>
            <circle
              cx={node.svgX}
              cy={node.svgY}
              r={node.role === 'sink' ? 18 : 12}
              fill={node.role === 'sink' ? 'rgba(251, 191, 36, 0.92)' : 'rgba(34, 211, 238, 0.92)'}
            />
            <text x={node.svgX} y={node.svgY + 28} className="topology-preview__node-name">
              Node {node.id}
            </text>
            <text x={node.svgX} y={node.svgY + 41} className="topology-preview__node-role">
              {node.role}
            </text>
          </g>
        ))}
      </svg>
    </div>
  );
}