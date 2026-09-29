import { useState } from "react";
import { formatNanoseconds } from "../domain/format";
import { UuvNodeGlyph, type UuvNodeTone } from "./UuvNodeGlyph";
import type { SceneSpatialDomain } from "../api/types";
import { isSceneDomain } from "../domain/sceneSpatial";
import "./sceneDepthProfile.css";

export interface SceneDepthNode {
  node_id: string;
  position: { x_meters: number; y_meters: number; z_meters: number };
  tone?: UuvNodeTone;
}
export interface EnvironmentSampleProfile {
  name: string;
  points: readonly { range_meters: number; depth_meters: number }[];
  referenceOnly: boolean;
}
const object = (value: unknown): Record<string, unknown> | undefined =>
  value !== null && typeof value === "object" && !Array.isArray(value) ? value as Record<string, unknown> : undefined;

/** Decode only preserved publication samples; coverage limits cannot manufacture terrain. */
export function environmentSampleProfile(input: unknown): EnvironmentSampleProfile | undefined {
  const envelope = object(input), payload = object(envelope?.payload);
  if (envelope?.available !== true || !Array.isArray(payload?.bathymetry) || payload.bathymetry.length < 2) return undefined;
  const points: { range_meters: number; depth_meters: number }[] = [];
  for (const value of payload.bathymetry) {
    const point = object(value), range = point?.range_meters, depth = point?.depth_meters;
    if (typeof range !== "number" || typeof depth !== "number" || !Number.isFinite(range) || !Number.isFinite(depth)
      || range < 0 || depth < 0 || (points.length > 0 && range <= points.at(-1)!.range_meters)) return undefined;
    points.push({ range_meters: range, depth_meters: depth });
  }
  return { name: typeof envelope.name === "string" ? envelope.name : "已发布环境",
    points, referenceOnly: object(payload.acquisition)?.useReferenceCache === true };
}

interface Props {
  /** Exactly the same node positions as the upper plan view at the displayed time. */
  nodes: readonly SceneDepthNode[];
  selectedNodeIds?: readonly string[];
  onSelectNode?: (id: string, additive: boolean) => void;
  simulationTimeNs?: string;
  /** Use the plan view's X domain to preserve the spatial relationship. */
  xDomain?: readonly [number, number];
  environmentInputs?: unknown;
  spatialDomain?: SceneSpatialDomain | null;
  plotWidth?: number;
  plotHeight?: number;
  plotPadding?: number;
  surfaceZMeters?: number;
  expanded?: boolean;
  onExpandedChange?: (expanded: boolean) => void;
}
const compact = (value: number) => Number(value.toFixed(Math.abs(value) < 10 ? 1 : 0)).toString();

/** Read-only display: never queries a model, samples on a timer, or invents a sea floor. */
export function SceneDepthProfile({ nodes, selectedNodeIds = [], onSelectNode, simulationTimeNs,
  xDomain, environmentInputs, spatialDomain, plotWidth = 660, plotHeight = 212, plotPadding = 52,
  surfaceZMeters = 0, expanded: controlledExpanded, onExpandedChange }: Props) {
  const chart = { width: plotWidth, height: plotHeight, left: plotPadding, right: plotPadding, top: 27, bottom: 42 };
  const [internalExpanded, setInternalExpanded] = useState(true);
  const expanded = controlledExpanded ?? internalExpanded;
  const toggle = () => { setInternalExpanded(!expanded); onExpandedChange?.(!expanded); };
  const validNodes = nodes.filter(node => [node.position.x_meters, node.position.y_meters, node.position.z_meters].every(Number.isFinite));
  const selected = selectedNodeIds.slice(0, 2).map(id => validNodes.find(node => node.node_id === id)).filter(node => node !== undefined);
  const profile = environmentSampleProfile(environmentInputs);
  const aligned = isSceneDomain(spatialDomain) && !!profile;
  // A shared X section must not silently switch to A–B distance after selecting two nodes.
  const pair = !aligned && selected.length === 2 ? selected : null;
  const horizontalDistance = pair ? Math.hypot(pair[1].position.x_meters - pair[0].position.x_meters,
    pair[1].position.y_meters - pair[0].position.y_meters) : null;
  const shown = pair ?? validNodes;
  const nodeXs = validNodes.map(node => node.position.x_meters);
  const validDomain = xDomain?.every(Number.isFinite) && xDomain[1] > xDomain[0];
  const minimumX = pair ? 0 : validDomain ? xDomain![0] : Math.min(0, ...nodeXs);
  const maximumX = pair ? horizontalDistance! : validDomain ? xDomain![1] : Math.max(1, ...nodeXs);
  const terrain = aligned ? clipTerrain(profile!.points, Math.max(minimumX, spatialDomain!.x_min_m), Math.min(maximumX, spatialDomain!.x_max_m)) : [];
  const depths = [...shown.map(node => surfaceZMeters - node.position.z_meters), ...terrain.map(p => p.depth_meters)];
  const minimumDepth = Math.min(0, ...depths), maximumDepth = Math.max(1, ...depths) * 1.18;
  const width = chart.width - chart.left - chart.right, height = chart.height - chart.top - chart.bottom;
  const px = (x: number) => chart.left + (x - minimumX) / Math.max(maximumX - minimumX, 1) * width;
  const py = (depth: number) => chart.top + (depth - minimumDepth) / (maximumDepth - minimumDepth) * height;
  const terrainPath = terrain.map(p => `${px(p.range_meters)},${py(p.depth_meters)}`).join(" ");
  const timeLabel = simulationTimeNs === undefined ? "初始布设" : `仿真时间 ${formatNanoseconds(simulationTimeNs).split(" (")[0]}`;
  const title = aligned ? "节点与海底同坐标剖面" : pair ? "链路剖面" : "节点深度分布（X 坐标）";
  return <section className={`scene-depth-profile ${expanded ? "expanded" : "collapsed"}`} aria-label="场景深度与环境剖面" data-simulation-time-ns={simulationTimeNs}>
    <header><button type="button" onClick={toggle} aria-expanded={expanded} aria-label={expanded ? "收起深度剖面" : "展开深度剖面"}>
      <span aria-hidden="true">{expanded ? "▾" : "▸"}</span> 深度剖面</button><span>{timeLabel}{pair ? ` · N${pair[0].node_id} → N${pair[1].node_id} · 水平距离 ${horizontalDistance!.toFixed(1)} m` : " · 与上方节点位置同步"}</span></header>
    {expanded && <div className={`scene-depth-plots ${profile && !aligned ? "has-environment" : ""} ${aligned ? "aligned-environment" : ""}`}>
      <figure className="scene-node-depth"><figcaption>{aligned ? "节点与海底 · 共用场景 X 坐标" : pair ? "两节点深度 / 水平测距" : "节点深度 / 场景 X 坐标"}<small>深度向下 · m</small></figcaption>
        <svg viewBox={`0 0 ${chart.width} ${chart.height}`} preserveAspectRatio="none" role="img" aria-label={title}>
          <rect className="depth-water" x={chart.left} y={chart.top} width={width} height={height} />
          {[0, .25, .5, .75, 1].map(fraction => {
            const x = chart.left + fraction * width, y = chart.top + fraction * height;
            return <g key={fraction} className="depth-chart-grid"><path d={`M${x} ${chart.top} V${chart.top + height} M${chart.left} ${y} H${chart.left + width}`} />
              <text x={x} y={chart.top + height + 18} textAnchor="middle">{compact(minimumX + fraction * (maximumX - minimumX))}</text>
              <text x={chart.left - 9} y={y + 4} textAnchor="end">{compact(minimumDepth + fraction * (maximumDepth - minimumDepth))}</text></g>;
          })}
          <line className="depth-sea-surface" x1={chart.left} x2={chart.left + width} y1={py(0)} y2={py(0)} />
          {terrain.length > 1 && <><polygon className="depth-terrain-fill" points={`${terrainPath} ${px(terrain.at(-1)!.range_meters)},${chart.top + height} ${px(terrain[0].range_meters)},${chart.top + height}`} />
            <polyline className="depth-terrain-line" points={terrainPath} data-sample-count={terrain.length} /></>}
          <text className="depth-axis-label" x={chart.left} y="17">海面 0 m</text>
          <text className="depth-axis-label" x={chart.width / 2} y={chart.height - 3} textAnchor="middle">{pair ? "从第一节点起的水平距离 / m" : "场景 X / m"}</text>
          {pair && <line className="depth-measure-line" x1={px(0)} x2={px(horizontalDistance!)} y1={py(surfaceZMeters - pair[0].position.z_meters)} y2={py(surfaceZMeters - pair[1].position.z_meters)} />}
          {shown.map((node, index) => {
            const x = px(pair ? index === 0 ? 0 : horizontalDistance! : node.position.x_meters), depth = surfaceZMeters - node.position.z_meters;
            const tone = selectedNodeIds[0] === node.node_id ? "selected" : selectedNodeIds[1] === node.node_id ? "peer" : node.tone;
            return <g key={node.node_id} className={`depth-node ${onSelectNode ? "selectable" : ""}`} transform={`translate(${x},${py(depth)})`}
              role={onSelectNode ? "button" : "img"} tabIndex={onSelectNode ? 0 : undefined} aria-label={`深度节点 N${node.node_id} · ${compact(depth)} m`}
              onClick={event => onSelectNode?.(node.node_id, event.shiftKey)}
              onKeyDown={event => { if (event.key === "Enter" || event.key === " ") { event.preventDefault(); onSelectNode?.(node.node_id, event.shiftKey); } }}
              data-node-id={node.node_id} data-x-meters={node.position.x_meters} data-y-meters={node.position.y_meters} data-z-meters={node.position.z_meters}>
              <UuvNodeGlyph tone={tone} scale={.7} /><text x={0} y={py(depth) < chart.top + 20 ? 18 : -13} textAnchor="middle">N{node.node_id}</text>
              <title>{`N${node.node_id}: X ${node.position.x_meters} m，Y ${node.position.y_meters} m，深度 ${depth} m`}</title>
            </g>;
          })}
          {!shown.length && <text className="depth-empty-label" x={chart.width / 2} y={chart.height / 2} textAnchor="middle">尚无当前位置数据</text>}
        </svg>
        <p>{aligned ? "场景已明确采用二维剖面：X 沿地形测线，横向 Y 使用相同地形；这不是实测三维海底。上下视图 X 比例一致。" : pair ? "测距连线不是声线；没有该链路的真实声线路径时不绘制反射。" : "按 X 展示深度；不同 Y 的节点可能投影重合，点击节点可查看完整坐标。"}</p>
      </figure>
      {!aligned && (profile ? <TerrainProfile profile={profile} /> : <p className="depth-source-unavailable">未取得该环境保存的地形采样点；不从声场覆盖范围推造海底。</p>)}
    </div>}
  </section>;
}

/** Interpolate only inside saved samples, never extend the measured transect. */
function clipTerrain(points: EnvironmentSampleProfile["points"], lower: number, upper: number) {
  const start = Math.max(lower, points[0].range_meters), end = Math.min(upper, points.at(-1)!.range_meters);
  if (start >= end) return [];
  const sample = (x: number) => {
    const i = Math.max(1, points.findIndex(p => p.range_meters >= x));
    const a = points[i - 1], b = points[i];
    return { range_meters: x, depth_meters: a.depth_meters + (b.depth_meters - a.depth_meters) * (x - a.range_meters) / (b.range_meters - a.range_meters) };
  };
  return [sample(start), ...points.filter(p => p.range_meters > start && p.range_meters < end), sample(end)];
}

function TerrainProfile({ profile }: { profile: EnvironmentSampleProfile }) {
  const width = 480, height = 212, left = 52, right = 24, top = 27, bottom = 42;
  const xMin = profile.points[0].range_meters, xMax = profile.points.at(-1)!.range_meters;
  const maxDepth = Math.max(1, ...profile.points.map(point => point.depth_meters)) * 1.1;
  const px = (range: number) => left + (range - xMin) / (xMax - xMin) * (width - left - right);
  const py = (depth: number) => top + depth / maxDepth * (height - top - bottom);
  const path = profile.points.map(point => `${px(point.range_meters)},${py(point.depth_meters)}`).join(" ");
  return <figure className="scene-environment-depth"><figcaption>{profile.referenceOnly ? "参考环境地形样本" : "环境保存的地形剖面"}<small title={profile.name}>{profile.name}</small></figcaption>
    <svg viewBox={`0 0 ${width} ${height}`} role="img" aria-label="独立环境采样剖面">
      <rect className="depth-water" x={left} y={top} width={width - left - right} height={height - top - bottom} />
      {[0, .5, 1].map(fraction => <g key={fraction} className="depth-chart-grid">
        <path d={`M${left} ${py(maxDepth * fraction)} H${width - right}`} /><text x={left - 8} y={py(maxDepth * fraction) + 4} textAnchor="end">{compact(maxDepth * fraction)}</text>
        <text x={px(xMin + (xMax - xMin) * fraction)} y={height - bottom + 18} textAnchor="middle">{compact(xMin + (xMax - xMin) * fraction)}</text>
      </g>)}
      <text className="depth-axis-label" x={left} y="17">深度 / m</text>
      <text className="depth-axis-label" x={width / 2} y={height - 3} textAnchor="middle">沿原始采样剖面的距离 / m</text>
      <polygon className="depth-terrain-fill" points={`${path} ${width - right},${height - bottom} ${left},${height - bottom}`} />
      <polyline className="depth-terrain-line" points={path} data-sample-count={profile.points.length} />
    </svg>
    <p>一维地形，尚未与场景坐标对齐；不能据此认定节点脚下的海底。未填补或延长采样范围。</p>
  </figure>;
}
