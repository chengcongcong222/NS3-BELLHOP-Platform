/**
 * ProfileView — 节点纵剖面与链路声线剖面。
 *
 * 横轴与俯视图使用同一套 X 坐标域，
 * 避免因为 tx/rx 顺序或节点编号导致剖面左右翻转。
 */
import { useEffect, useMemo, useRef, useState } from 'react';

import { fetchEnvironmentPreviewRays } from '../services/api';

import type { BathymetryData, CommunicationEvent, DemoNode, EnvironmentPreviewRays, LinkMetric, LinkRays, RayComponent, StudioEnvironmentBounds } from '../types';

interface ProfileViewProps {
  nodes: DemoNode[];
  bathymetry?: BathymetryData;
  metrics: LinkMetric[];
  metricHistory: LinkMetric[];
  rays: LinkRays[];
  previewArrivalsPath?: string | null;
  selectedNodeId: number | null;
  focusedEdgeKey: string | null;
  currentTimeSeconds?: number | null;
  currentEvent?: CommunicationEvent | null;
  environmentBounds?: StudioEnvironmentBounds;
  editable?: boolean;
  onMoveNode?: (nodeId: number, position: [number, number, number]) => void;
  onSelectNode?: (nodeId: number) => void;
  onSelectEdge: (edgeKey: string) => void;
}

interface PreparedLink {
  edgeKey: string;
  txNode: DemoNode;
  rxNode: DemoNode;
  horizontalDistance: number;
  projectedBathymetry?: BathymetryData;
  rays: RayComponent[];
  isNlos: boolean;
  source: 'rays' | 'metrics' | 'preview';
  delay_s?: number;
  receivedLevelDb?: number;
  multipathCount?: number;
  previewMeta?: {
    mode?: 'nearest' | 'interpolated';
    contributingSamples?: number;
    queryDistanceM: number;
    nearestRangeM: number;
    rangeBoundsM?: [number, number];
    querySourceDepthM: number;
    nearestSourceDepthM: number;
    sourceDepthBoundsM?: [number, number];
    queryReceiverDepthM: number;
    nearestReceiverDepthM: number;
    receiverDepthBoundsM?: [number, number];
  };
}

interface ActiveLinkPreview extends EnvironmentPreviewRays {
  edgeKey: string;
}

interface TrendPoint {
  time: number;
  value: number;
}

interface MetricTrendDefinition {
  seriesKey: string;
  label: string;
  unit: string;
  stroke: string;
  fill: string;
  decimals: number;
  accessor: (metric: LinkMetric) => number | undefined;
}

interface MetricTrendCardProps extends MetricTrendDefinition {
  points: TrendPoint[];
  markerTime?: number | null;
}

const VB = { w: 980, h: 520, left: 72, right: 32, top: 36, bottom: 52 };
const PW = VB.w - VB.left - VB.right;
const PH = VB.h - VB.top - VB.bottom;

const HISTORY_SERIES: MetricTrendDefinition[] = [
  {
    seriesKey: 'received-level',
    label: '接收电平',
    unit: 'dB',
    stroke: '#34d399',
    fill: 'rgba(52,211,153,0.18)',
    decimals: 1,
    accessor: (metric) => metric.received_level_db,
  },
  {
    seriesKey: 'delay',
    label: '传播时延',
    unit: 's',
    stroke: '#f59e0b',
    fill: 'rgba(245,158,11,0.16)',
    decimals: 3,
    accessor: (metric) => metric.delay_s,
  },
  {
    seriesKey: 'first-arrival',
    label: '首达时延',
    unit: 's',
    stroke: '#60a5fa',
    fill: 'rgba(96,165,250,0.16)',
    decimals: 3,
    accessor: (metric) => metric.first_arrival_delay_s,
  },
  {
    seriesKey: 'snr',
    label: '信噪比',
    unit: 'dB',
    stroke: '#22d3ee',
    fill: 'rgba(34,211,238,0.16)',
    decimals: 1,
    accessor: (metric) => metric.snr_db,
  },
  {
    seriesKey: 'pseudo-range',
    label: '伪距',
    unit: 'm',
    stroke: '#c084fc',
    fill: 'rgba(192,132,252,0.16)',
    decimals: 1,
    accessor: (metric) => metric.pseudo_range_m,
  },
  {
    seriesKey: 'multipath',
    label: '多径条数',
    unit: 'paths',
    stroke: '#f472b6',
    fill: 'rgba(244,114,182,0.16)',
    decimals: 0,
    accessor: (metric) => metric.multipath_count,
  },
];

function compareProfileNodeOrder(left: DemoNode, right: DemoNode) {
  if (left.position[0] !== right.position[0]) {
    return left.position[0] - right.position[0];
  }
  if (left.position[1] !== right.position[1]) {
    return left.position[1] - right.position[1];
  }
  return left.id - right.id;
}

function traceRayPath(
  srcRange: number,
  srcDepth: number,
  rcvRange: number,
  rcvDepth: number,
  ray: RayComponent,
  waterDepth: number,
): Array<[number, number]> {
  const totalBounces = ray.surface_bounces + ray.bottom_bounces;
  if (totalBounces === 0) {
    return [[srcRange, srcDepth], [rcvRange, rcvDepth]];
  }

  const points: Array<[number, number]> = [[srcRange, srcDepth]];
  const nSegments = totalBounces + 1;
  const dr = (rcvRange - srcRange) / nSegments;

  let currentDepth = srcDepth;
  let goingDown = ray.launch_angle_deg > 0;
  let surfLeft = ray.surface_bounces;
  let botLeft = ray.bottom_bounces;

  for (let seg = 1; seg <= totalBounces; seg++) {
    const r = srcRange + dr * seg;
    if (goingDown && botLeft > 0) {
      currentDepth = waterDepth;
      botLeft--;
      goingDown = false;
    } else if (!goingDown && surfLeft > 0) {
      currentDepth = 0;
      surfLeft--;
      goingDown = true;
    } else if (botLeft > 0) {
      currentDepth = waterDepth;
      botLeft--;
      goingDown = false;
    } else {
      currentDepth = 0;
      surfLeft--;
      goingDown = true;
    }
    points.push([r, currentDepth]);
  }

  points.push([rcvRange, rcvDepth]);
  return points;
}

function sampleBathymetryDepth(bathymetry: BathymetryData, x: number) {
  const { range_m, depth_m } = bathymetry;
  if (range_m.length === 0) return 0;
  if (x <= range_m[0]) return depth_m[0];
  if (x >= range_m[range_m.length - 1]) return depth_m[depth_m.length - 1];

  for (let index = 1; index < range_m.length; index++) {
    if (x <= range_m[index]) {
      const x0 = range_m[index - 1];
      const x1 = range_m[index];
      const d0 = depth_m[index - 1];
      const d1 = depth_m[index];
      const ratio = x1 === x0 ? 0 : (x - x0) / (x1 - x0);
      return d0 + (d1 - d0) * ratio;
    }
  }
  return depth_m[depth_m.length - 1];
}

function projectBathymetryAlongLink(txNode: DemoNode, rxNode: DemoNode, bathymetry?: BathymetryData): BathymetryData | undefined {
  if (!bathymetry || bathymetry.range_m.length === 0) return undefined;

  const dx = rxNode.position[0] - txNode.position[0];
  const steps = Math.max(24, bathymetry.range_m.length);
  const fractions = Array.from({ length: steps + 1 }, (_, index) => index / steps);
  const range_m = fractions.map((fraction) => txNode.position[0] + dx * fraction);
  const depth_m = fractions.map((fraction) => {
    const t = fraction;
    const globalX = txNode.position[0] + dx * t;
    return sampleBathymetryDepth(bathymetry, globalX);
  });
  return { range_m, depth_m };
}

function buildFallbackDirectRay(metric: LinkMetric): RayComponent {
  return {
    delay_s: metric.first_arrival_delay_s ?? metric.delay_s,
    amplitude_db: metric.received_level_db,
    surface_bounces: 0,
    bottom_bounces: 0,
    launch_angle_deg: 0,
    arrival_angle_deg: 0,
    path_length_m: metric.pseudo_range_m,
  };
}

function buildNodePairKey(leftId: number, rightId: number) {
  return leftId < rightId ? `${leftId}-${rightId}` : `${rightId}-${leftId}`;
}

function formatMetricValue(value: number | null | undefined, unit: string, decimals: number) {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return '--';
  }
  if (unit === 's') {
    if (Math.abs(value) < 1) {
      return `${(value * 1000).toFixed(Math.min(decimals + 1, 2))} ms`;
    }
    return `${value.toFixed(decimals)} s`;
  }
  if (unit === 'm') {
    if (Math.abs(value) >= 1000) {
      return `${(value / 1000).toFixed(Math.min(decimals + 1, 2))} km`;
    }
    return `${value.toFixed(decimals)} m`;
  }
  if (unit === 'dB') {
    return `${value.toFixed(decimals)} dB`;
  }
  if (unit === 'paths') {
    return `${Math.round(value)}`;
  }
  return unit ? `${value.toFixed(decimals)} ${unit}` : value.toFixed(decimals);
}

function formatTrendTimeLabel(value: number | null | undefined) {
  if (typeof value !== 'number' || !Number.isFinite(value)) {
    return '静态';
  }
  if (Math.abs(value) < 1) {
    return `${(value * 1000).toFixed(0)} ms`;
  }
  return `${value.toFixed(value >= 10 ? 1 : 2)} s`;
}

function formatPreviewBounds(bounds: [number, number] | undefined, fallback: number) {
  if (!bounds || bounds.length !== 2) {
    return `${Math.round(fallback)} m`;
  }
  const [lower, upper] = bounds;
  if (!Number.isFinite(lower) || !Number.isFinite(upper)) {
    return `${Math.round(fallback)} m`;
  }
  if (Math.abs(lower - upper) < 1e-6) {
    return `${Math.round(lower)} m`;
  }
  return `${Math.round(lower)}-${Math.round(upper)} m`;
}

function MetricTrendCard({ seriesKey, label, unit, stroke, fill, decimals, points, markerTime }: MetricTrendCardProps) {
  if (points.length === 0) {
    return null;
  }

  const width = 236;
  const height = 116;
  const padLeft = 10;
  const padRight = 10;
  const padTop = 10;
  const padBottom = 22;
  const chartWidth = width - padLeft - padRight;
  const chartHeight = height - padTop - padBottom;
  const values = points.map((point) => point.value);
  const min = Math.min(...values);
  const max = Math.max(...values);
  const avg = values.reduce((sum, value) => sum + value, 0) / values.length;
  const timeStart = points[0]?.time ?? 0;
  const timeEnd = points[points.length - 1]?.time ?? timeStart;
  const valueRange = Math.max(max - min, 1e-9);
  const timeRange = Math.max(timeEnd - timeStart, 1e-9);
  const markerIndex = typeof markerTime === 'number'
    ? points.reduce((bestIndex, point, index, allPoints) => (
      Math.abs(point.time - markerTime) < Math.abs(allPoints[bestIndex].time - markerTime) ? index : bestIndex
    ), 0)
    : points.length - 1;

  const coordinates = points.map((point, index) => {
    const x = points.length === 1
      ? padLeft + chartWidth / 2
      : padLeft + ((point.time - timeStart) / timeRange) * chartWidth;
    const y = max === min
      ? padTop + chartHeight / 2
      : padTop + chartHeight - ((point.value - min) / valueRange) * chartHeight;
    return { x, y, point, index };
  });
  const markerCoordinate = coordinates[markerIndex] ?? coordinates[coordinates.length - 1];
  const linePoints = coordinates.map((coordinate) => `${coordinate.x.toFixed(1)},${coordinate.y.toFixed(1)}`).join(' ');
  const areaPoints = coordinates.length > 1
    ? [
      `${coordinates[0].x.toFixed(1)},${(height - padBottom).toFixed(1)}`,
      ...coordinates.map((coordinate) => `${coordinate.x.toFixed(1)},${coordinate.y.toFixed(1)}`),
      `${coordinates[coordinates.length - 1].x.toFixed(1)},${(height - padBottom).toFixed(1)}`,
    ].join(' ')
    : null;

  return (
    <article
      style={{
        flex: '1 1 236px',
        minWidth: 0,
        padding: '10px 12px',
        borderRadius: 14,
        border: '1px solid rgba(148,163,184,0.14)',
        background: 'linear-gradient(180deg, rgba(15,23,42,0.92), rgba(3,10,18,0.86))',
        boxShadow: 'inset 0 1px 0 rgba(148,163,184,0.05)',
      }}
    >
      <div style={{ display: 'flex', alignItems: 'baseline', justifyContent: 'space-between', gap: 12, marginBottom: 6 }}>
        <span style={{ fontSize: 11, color: '#dbeafe' }}>{label}</span>
        <strong style={{ fontSize: 12, color: '#f8fafc', fontWeight: 600 }}>
          {formatMetricValue(markerCoordinate?.point.value ?? null, unit, decimals)}
        </strong>
      </div>

      <svg viewBox={`0 0 ${width} ${height}`} style={{ width: '100%', height: 116, display: 'block' }} role="img" aria-label={label}>
        <defs>
          <linearGradient id={`profile-trend-${seriesKey}`} x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%" stopColor={fill} />
            <stop offset="100%" stopColor="rgba(15,23,42,0.01)" />
          </linearGradient>
        </defs>

        {[0, 0.5, 1].map((fraction) => {
          const y = padTop + chartHeight * fraction;
          return (
            <line
              key={fraction}
              x1={padLeft}
              y1={y}
              x2={width - padRight}
              y2={y}
              stroke="rgba(148,163,184,0.08)"
              strokeWidth="1"
            />
          );
        })}

        {markerCoordinate && (
          <line
            x1={markerCoordinate.x}
            y1={padTop}
            x2={markerCoordinate.x}
            y2={height - padBottom}
            stroke="rgba(248,250,252,0.22)"
            strokeWidth="1"
            strokeDasharray="3 3"
          />
        )}

        {areaPoints && <polygon points={areaPoints} fill={`url(#profile-trend-${seriesKey})`} />}
        {coordinates.length > 1 && (
          <polyline
            fill="none"
            stroke={stroke}
            strokeWidth="2"
            strokeLinecap="round"
            strokeLinejoin="round"
            points={linePoints}
          />
        )}
        {markerCoordinate && (
          <circle cx={markerCoordinate.x} cy={markerCoordinate.y} r="4" fill={stroke} stroke="rgba(248,250,252,0.85)" strokeWidth="1.25" />
        )}

        <text x={padLeft} y={height - 6} fill="rgba(148,163,184,0.5)" fontSize="8">
          {formatTrendTimeLabel(timeStart)}
        </text>
        <text x={width - padRight} y={height - 6} fill="rgba(148,163,184,0.5)" fontSize="8" textAnchor="end">
          {formatTrendTimeLabel(timeEnd)}
        </text>
        {markerCoordinate && (
          <text x={markerCoordinate.x} y={10} fill="rgba(226,232,240,0.78)" fontSize="8" textAnchor="middle">
            {formatTrendTimeLabel(markerCoordinate.point.time)}
          </text>
        )}
      </svg>

      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8, marginTop: 6, fontSize: 10, color: 'rgba(148,163,184,0.74)' }}>
        <span>{`min ${formatMetricValue(min, unit, decimals)}`}</span>
        <span>{`avg ${formatMetricValue(avg, unit, decimals)}`}</span>
        <span>{`max ${formatMetricValue(max, unit, decimals)}`}</span>
      </div>
    </article>
  );
}

export function ProfileView({
  nodes,
  bathymetry,
  metrics,
  metricHistory,
  rays,
  previewArrivalsPath,
  selectedNodeId,
  focusedEdgeKey,
  currentTimeSeconds,
  currentEvent,
  environmentBounds,
  editable = false,
  onMoveNode,
  onSelectNode,
  onSelectEdge,
}: ProfileViewProps) {
  const nodeById = useMemo(() => new Map(nodes.map((node) => [node.id, node])), [nodes]);
  const [pickedNodeIds, setPickedNodeIds] = useState<number[]>([]);
  const svgRef = useRef<SVGSVGElement | null>(null);
  const suppressPickRef = useRef(false);
  const [draggingNodeId, setDraggingNodeId] = useState<number | null>(null);
  const [dragPosition, setDragPosition] = useState<[number, number, number] | null>(null);
  const [previewLink, setPreviewLink] = useState<ActiveLinkPreview | null>(null);

  const preparedLinks = useMemo(() => {
    const links = new Map<string, PreparedLink>();

    metrics.forEach((metric) => {
      const txNode = nodeById.get(metric.tx_id);
      const rxNode = nodeById.get(metric.rx_id);
      if (!txNode || !rxNode) return;
      const horizontalDistance = Math.max(
        Math.hypot(rxNode.position[0] - txNode.position[0], rxNode.position[1] - txNode.position[1]),
        1,
      );
      const edgeKey = `${metric.tx_id}-${metric.rx_id}`;
      links.set(edgeKey, {
        edgeKey,
        txNode,
        rxNode,
        horizontalDistance,
        projectedBathymetry: projectBathymetryAlongLink(txNode, rxNode, bathymetry),
        rays: [buildFallbackDirectRay(metric)],
        isNlos: metric.is_nlos === 1,
        source: 'metrics',
        delay_s: metric.delay_s,
        receivedLevelDb: metric.received_level_db,
        multipathCount: metric.multipath_count,
      });
    });

    rays.forEach((link) => {
      const txNode = nodeById.get(link.tx_id);
      const rxNode = nodeById.get(link.rx_id);
      if (!txNode || !rxNode) return;
      const horizontalDistance = Math.max(
        Math.hypot(rxNode.position[0] - txNode.position[0], rxNode.position[1] - txNode.position[1]),
        1,
      );
      const edgeKey = `${link.tx_id}-${link.rx_id}`;
      const existing = links.get(edgeKey);
      links.set(edgeKey, {
        edgeKey,
        txNode,
        rxNode,
        horizontalDistance,
        projectedBathymetry: projectBathymetryAlongLink(txNode, rxNode, bathymetry),
        rays: link.rays.length > 0 ? link.rays : existing?.rays ?? [],
        isNlos: link.is_nlos !== 0,
        source: 'rays',
        delay_s: existing?.delay_s,
        receivedLevelDb: existing?.receivedLevelDb ?? link.direct_ray_amplitude_db,
        multipathCount: link.rays.length || existing?.multipathCount,
      });
    });

    if (previewLink) {
      const existing = links.get(previewLink.edgeKey);
      if (existing) {
        links.set(previewLink.edgeKey, {
          ...existing,
          rays: previewLink.rays,
          source: 'preview',
          multipathCount: previewLink.rays.length,
          previewMeta: {
            mode: previewLink.mode,
            contributingSamples: previewLink.contributing_samples,
            queryDistanceM: previewLink.query_distance_m,
            nearestRangeM: previewLink.nearest_range_m,
            rangeBoundsM: previewLink.range_bounds_m,
            querySourceDepthM: previewLink.query_source_depth_m,
            nearestSourceDepthM: previewLink.nearest_source_depth_m,
            sourceDepthBoundsM: previewLink.source_depth_bounds_m,
            queryReceiverDepthM: previewLink.query_receiver_depth_m,
            nearestReceiverDepthM: previewLink.nearest_receiver_depth_m,
            receiverDepthBoundsM: previewLink.receiver_depth_bounds_m,
          },
        });
      }
    }

    return Array.from(links.values());
  }, [bathymetry, metrics, nodeById, previewLink, rays]);

  const linkByNodePair = useMemo(() => {
    const lookup = new Map<string, PreparedLink>();
    preparedLinks.forEach((link) => {
      lookup.set(buildNodePairKey(link.txNode.id, link.rxNode.id), link);
    });
    return lookup;
  }, [preparedLinks]);

  const metricSnapshotByEdgeKey = useMemo(() => {
    const lookup = new Map<string, LinkMetric>();
    metrics.forEach((metric) => {
      lookup.set(`${metric.tx_id}-${metric.rx_id}`, metric);
    });
    return lookup;
  }, [metrics]);

  const metricHistoryByEdgeKey = useMemo(() => {
    const lookup = new Map<string, LinkMetric[]>();
    metricHistory.forEach((metric) => {
      const edgeKey = `${metric.tx_id}-${metric.rx_id}`;
      const bucket = lookup.get(edgeKey);
      if (bucket) {
        bucket.push(metric);
      } else {
        lookup.set(edgeKey, [metric]);
      }
    });
    lookup.forEach((bucket) => {
      bucket.sort((left, right) => (left.time_s ?? 0) - (right.time_s ?? 0));
    });
    return lookup;
  }, [metricHistory]);

  useEffect(() => {
    if (focusedEdgeKey) {
      setPickedNodeIds([]);
    }
  }, [focusedEdgeKey]);

  const activeLink = useMemo(() => {
    if (focusedEdgeKey) {
      return preparedLinks.find((item) => item.edgeKey === focusedEdgeKey) ?? null;
    }
    if (pickedNodeIds.length === 2) {
      return linkByNodePair.get(buildNodePairKey(pickedNodeIds[0], pickedNodeIds[1])) ?? null;
    }
    return preparedLinks.length === 1 ? preparedLinks[0] : null;
  }, [focusedEdgeKey, linkByNodePair, pickedNodeIds, preparedLinks]);

  useEffect(() => {
    if (!editable || !previewArrivalsPath || !activeLink) {
      setPreviewLink(null);
      return;
    }

    let cancelled = false;
    const timer = window.setTimeout(() => {
      fetchEnvironmentPreviewRays(
        previewArrivalsPath,
        activeLink.txNode.position,
        activeLink.rxNode.position,
        'interpolate',
      )
        .then((preview) => {
          if (cancelled) {
            return;
          }
          if (!preview) {
            setPreviewLink(null);
            return;
          }
          setPreviewLink({
            edgeKey: activeLink.edgeKey,
            ...preview,
          });
        })
        .catch(() => {
          if (!cancelled) {
            setPreviewLink(null);
          }
        });
    }, 120);

    return () => {
      cancelled = true;
      window.clearTimeout(timer);
    };
  }, [
    editable,
    previewArrivalsPath,
    activeLink?.edgeKey,
    activeLink?.txNode.position[0],
    activeLink?.txNode.position[1],
    activeLink?.txNode.position[2],
    activeLink?.rxNode.position[0],
    activeLink?.rxNode.position[1],
    activeLink?.rxNode.position[2],
  ]);

  const orientedActiveLink = useMemo(() => {
    if (!activeLink) {
      return null;
    }
    const leftFirst = compareProfileNodeOrder(activeLink.txNode, activeLink.rxNode) <= 0;
    const leftNode = leftFirst ? activeLink.txNode : activeLink.rxNode;
    const rightNode = leftFirst ? activeLink.rxNode : activeLink.txNode;
    return {
      ...activeLink,
      txNode: leftNode,
      rxNode: rightNode,
      projectedBathymetry: projectBathymetryAlongLink(leftNode, rightNode, bathymetry),
    };
  }, [activeLink, bathymetry]);

  const activeLinkHistory = useMemo(() => {
    if (!activeLink) {
      return [] as LinkMetric[];
    }
    const history = metricHistoryByEdgeKey.get(activeLink.edgeKey);
    if (history && history.length > 0) {
      return history;
    }
    const snapshot = metricSnapshotByEdgeKey.get(activeLink.edgeKey);
    return snapshot ? [snapshot] : [];
  }, [activeLink, metricHistoryByEdgeKey, metricSnapshotByEdgeKey]);

  const hasTimedHistory = useMemo(
    () => activeLinkHistory.some((metric) => typeof metric.time_s === 'number' && Number.isFinite(metric.time_s)),
    [activeLinkHistory],
  );

  const historyMarkerTime = useMemo(() => {
    if (orientedActiveLink && currentEvent?.edgeKey === orientedActiveLink.edgeKey && typeof currentEvent.time_s === 'number') {
      return currentEvent.time_s;
    }
    if (typeof currentTimeSeconds === 'number' && Number.isFinite(currentTimeSeconds)) {
      return currentTimeSeconds;
    }
    const lastTimedMetric = [...activeLinkHistory]
      .reverse()
      .find((metric) => typeof metric.time_s === 'number' && Number.isFinite(metric.time_s));
    return lastTimedMetric?.time_s ?? null;
  }, [activeLinkHistory, currentEvent, currentTimeSeconds, orientedActiveLink]);

  const metricTrendCards = useMemo(() => {
    if (!activeLink) {
      return [] as Array<MetricTrendDefinition & { points: TrendPoint[] }>;
    }

    return HISTORY_SERIES
      .map((series) => {
        const points = activeLinkHistory.flatMap((metric, index) => {
          const value = series.accessor(metric);
          if (typeof value !== 'number' || !Number.isFinite(value)) {
            return [] as TrendPoint[];
          }
          const fallbackTime = typeof historyMarkerTime === 'number' ? historyMarkerTime : index;
          const time = typeof metric.time_s === 'number' && Number.isFinite(metric.time_s) ? metric.time_s : fallbackTime;
          return [{ time, value }];
        });
        return { ...series, points };
      })
      .filter((series) => series.points.length > 0);
  }, [activeLink, activeLinkHistory, historyMarkerTime]);

  const overviewNodes = useMemo(() => {
    const finiteNodes = nodes.filter((node) => (
      Array.isArray(node.position)
      && node.position.length >= 3
      && node.position.every((value) => Number.isFinite(value))
    ));
    if (finiteNodes.length === 0) {
      return [] as Array<{ node: DemoNode; range: number }>;
    }
    return finiteNodes
      .map((node) => ({ node, range: node.position[0] }))
      .sort((left, right) => left.range - right.range);
  }, [nodes]);

  const isOverviewMode = !orientedActiveLink;

  const selectedNodeLinks = useMemo(
    () => (typeof selectedNodeId === 'number'
      ? preparedLinks.filter((item) => item.txNode.id === selectedNodeId || item.rxNode.id === selectedNodeId)
      : []),
    [preparedLinks, selectedNodeId],
  );

  const displayNodes = useMemo(() => {
    if (orientedActiveLink) {
      return [
        { node: orientedActiveLink.txNode, range: orientedActiveLink.txNode.position[0] },
        { node: orientedActiveLink.rxNode, range: orientedActiveLink.rxNode.position[0] },
      ];
    }
    return overviewNodes;
  }, [orientedActiveLink, overviewNodes]);

  const displayBathymetry = orientedActiveLink?.projectedBathymetry ?? (isOverviewMode ? bathymetry : undefined);

  const layout = useMemo(() => {
    const ranges = displayNodes.map((item) => item.range);
    const depths = displayNodes.map((item) => Math.abs(item.node.position[2]));
    const bathRanges = displayBathymetry?.range_m ?? [];
    const minRangeRaw = Math.min(...ranges, ...bathRanges, 0);
    const maxRangeRaw = Math.max(...ranges, ...bathRanges, 1000);
    const fallbackDepth = Math.max(
      ...(bathymetry?.depth_m ?? []),
      ...nodes.map((node) => Math.abs(node.position[2])),
      100,
    );
    const span = Math.max(maxRangeRaw - minRangeRaw, 100);
    const pad = span * 0.08;
    let minRange = environmentBounds?.minX ?? (minRangeRaw - pad);
    let maxRange = environmentBounds?.maxX ?? (maxRangeRaw + pad);
    let maxDepth = environmentBounds?.depthMax ?? Math.max(...depths, fallbackDepth, 50);

    if (!environmentBounds && displayBathymetry && displayBathymetry.range_m.length > 0) {
      maxDepth = Math.max(maxDepth, ...displayBathymetry.depth_m);
    }

    if (maxRange <= minRange) {
      maxRange = minRange + 100;
    }

    return { minRange, maxRange, maxDepth: environmentBounds?.depthMax ?? (maxDepth * 1.15) };
  }, [bathymetry, displayBathymetry, displayNodes, environmentBounds, nodes]);

  const selectionHint = useMemo(() => {
    if (pickedNodeIds.length === 0) {
      return '先点一个节点，再点另一个节点，直接在本页刻画两节点间的声线。';
    }
    if (pickedNodeIds.length === 1) {
      return `已选起点 #${pickedNodeIds[0]}，再点一个节点查看对应链路。`;
    }
    if (!activeLink) {
      return `#${pickedNodeIds[0]} 与 #${pickedNodeIds[1]} 当前没有可用链路数据，请重新选择。`;
    }
    return `已按俯视图位置对齐到节点 #${pickedNodeIds[0]} 与 #${pickedNodeIds[1]} 的链路剖面。`;
  }, [activeLink, pickedNodeIds]);

  function handleProfileNodePick(nodeId: number) {
    if (pickedNodeIds.length === 0 || pickedNodeIds.length === 2) {
      setPickedNodeIds([nodeId]);
      return;
    }

    if (pickedNodeIds[0] === nodeId) {
      setPickedNodeIds([]);
      return;
    }

    const nextPair = [pickedNodeIds[0], nodeId];
    setPickedNodeIds(nextPair);
    const link = linkByNodePair.get(buildNodePairKey(nextPair[0], nextPair[1]));
    if (link) {
      onSelectEdge(link.edgeKey);
    }
  }

  function handleProfileNodeInteraction(nodeId: number) {
    if (suppressPickRef.current) {
      suppressPickRef.current = false;
      return;
    }
    onSelectNode?.(nodeId);
    handleProfileNodePick(nodeId);
  }

  function startProfileDrag(event: React.PointerEvent<SVGCircleElement>, nodeId: number) {
    if (!canDragNodes) {
      return;
    }
    event.preventDefault();
    event.stopPropagation();
    suppressPickRef.current = false;
    setDraggingNodeId(nodeId);
    onSelectNode?.(nodeId);
  }

  function sx(range: number) {
    return VB.left + ((range - layout.minRange) / Math.max(layout.maxRange - layout.minRange, 1)) * PW;
  }

  function sy(depth: number) {
    return VB.top + (depth / layout.maxDepth) * PH;
  }

  const canDragNodes = Boolean(editable && onMoveNode);

  useEffect(() => {
    if (editable) {
      return;
    }
    setDraggingNodeId(null);
    setDragPosition(null);
  }, [editable]);

  useEffect(() => {
    function onPointerMove(event: PointerEvent) {
      if (!canDragNodes || draggingNodeId === null || !svgRef.current || !onMoveNode) {
        return;
      }
      const rect = svgRef.current.getBoundingClientRect();
      const svgX = ((event.clientX - rect.left) / rect.width) * VB.w;
      const svgY = ((event.clientY - rect.top) / rect.height) * VB.h;
      const clampedX = Math.min(VB.left + PW, Math.max(VB.left, svgX));
      const clampedY = Math.min(VB.top + PH, Math.max(VB.top, svgY));
      const sourceNode = nodeById.get(draggingNodeId);
      if (!sourceNode) {
        return;
      }

      const nextRange = layout.minRange + ((clampedX - VB.left) / PW) * Math.max(layout.maxRange - layout.minRange, 1);
      const nextDepth = ((clampedY - VB.top) / PH) * layout.maxDepth;
      const nextPosition: [number, number, number] = [
        Math.round(nextRange),
        sourceNode.position[1],
        Math.round(nextDepth),
      ];
      suppressPickRef.current = true;
      setDragPosition(nextPosition);
      onMoveNode(draggingNodeId, nextPosition);
    }

    function onPointerUp() {
      setDraggingNodeId(null);
      setDragPosition(null);
    }

    window.addEventListener('pointermove', onPointerMove);
    window.addEventListener('pointerup', onPointerUp);
    return () => {
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
    };
  }, [canDragNodes, draggingNodeId, layout.maxDepth, layout.maxRange, layout.minRange, nodeById, onMoveNode]);

  const terrainPolyline = useMemo(() => {
    if (!displayBathymetry || displayBathymetry.range_m.length === 0) return null;
    return displayBathymetry.range_m.map((range, index) => `${sx(range).toFixed(1)},${sy(displayBathymetry.depth_m[index]).toFixed(1)}`).join(' ');
  }, [displayBathymetry, layout.maxDepth, layout.maxRange, layout.minRange]);

  const terrainPoly = useMemo(() => {
    if (!terrainPolyline) return null;
    return `${sx(layout.minRange).toFixed(1)},${(VB.top + PH).toFixed(1)} ${terrainPolyline} ${sx(layout.maxRange).toFixed(1)},${(VB.top + PH).toFixed(1)}`;
  }, [layout.maxRange, layout.minRange, terrainPolyline]);

  const waterDepth = environmentBounds?.waterDepth ?? (displayBathymetry ? Math.max(...displayBathymetry.depth_m) : layout.maxDepth);

  const activeEventSummary = useMemo(() => {
    if (!orientedActiveLink || !currentEvent || currentEvent.edgeKey !== orientedActiveLink.edgeKey) {
      return null;
    }
    const parts = [`当前事件 ${currentEvent.eventCode}`];
    if (typeof currentEvent.delay_s === 'number') {
      parts.push(formatMetricValue(currentEvent.delay_s, 's', 3));
    }
    if (typeof currentEvent.received_level_db === 'number') {
      parts.push(formatMetricValue(currentEvent.received_level_db, 'dB', 1));
    }
    return parts.join(' / ');
  }, [currentEvent, orientedActiveLink]);

  const metricPanelHint = useMemo(() => {
    if (!orientedActiveLink) {
      return '选中一条链路后，下方会把该链路的 Bellhop 剖面与接收电平、时延、SNR、多径等曲线对齐展示。';
    }
    if (orientedActiveLink.source === 'preview') {
      if (orientedActiveLink.previewMeta?.mode === 'interpolated') {
        return `当前剖面按环境库 arrivals 三维插值实时预览；距离区间 ${formatPreviewBounds(orientedActiveLink.previewMeta?.rangeBoundsM, orientedActiveLink.horizontalDistance)}，源深区间 ${formatPreviewBounds(orientedActiveLink.previewMeta?.sourceDepthBoundsM, Math.abs(orientedActiveLink.txNode.position[2]))}，收深区间 ${formatPreviewBounds(orientedActiveLink.previewMeta?.receiverDepthBoundsM, Math.abs(orientedActiveLink.rxNode.position[2]))}。`;
      }
      return `当前剖面按环境库 arrivals 最近邻实时预览；当前查询距离 ${Math.round(orientedActiveLink.previewMeta?.queryDistanceM ?? orientedActiveLink.horizontalDistance)} m，对应采样距离 ${Math.round(orientedActiveLink.previewMeta?.nearestRangeM ?? orientedActiveLink.horizontalDistance)} m。`;
    }
    if (metricTrendCards.length === 0) {
      return '当前链路还没有可用的时间序列指标，上方仍可查看 Bellhop 声线剖面。';
    }
    if (!hasTimedHistory) {
      return '当前是静态预估快照，因此以下曲线按单帧指标退化显示。';
    }
    return `当前播放游标 ${formatTrendTimeLabel(historyMarkerTime)}，下方曲线与上方剖面保持同一条链路。`;
  }, [hasTimedHistory, historyMarkerTime, metricTrendCards.length, orientedActiveLink]);

  return (
    <div style={{ width: '100%', height: '100%', display: 'flex', flexDirection: 'column', gap: 8, minHeight: 0 }}>
      <svg
        ref={svgRef}
        viewBox={`0 0 ${VB.w} ${VB.h}`}
        className="profile-view__svg"
        style={{ width: '100%', flex: 1, minHeight: 0 }}
      >
      <defs>
        <linearGradient id="pvWater" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(14,165,233,0.10)" />
          <stop offset="100%" stopColor="rgba(14,165,233,0.02)" />
        </linearGradient>
        <linearGradient id="pvTerrain" x1="0" y1="0" x2="0" y2="1">
          <stop offset="0%" stopColor="rgba(148,163,184,0.25)" />
          <stop offset="100%" stopColor="rgba(100,116,139,0.08)" />
        </linearGradient>
      </defs>

      <rect x={VB.left} y={VB.top} width={PW} height={PH} fill="url(#pvWater)" rx="4" />
      <line x1={VB.left} y1={VB.top} x2={VB.left + PW} y2={VB.top} stroke="rgba(14,165,233,0.4)" strokeWidth="1.5" />
      <text x={VB.left + 6} y={VB.top + 14} fill="rgba(14,165,233,0.5)" fontSize="10">水面</text>

      {terrainPoly && terrainPolyline && (
        <>
          <polygon points={terrainPoly} fill="url(#pvTerrain)" />
          <polyline points={terrainPolyline} fill="none" stroke="rgba(148,163,184,0.5)" strokeWidth="1.5" />
        </>
      )}

      {[0, 0.25, 0.5, 0.75, 1].map((fraction) => {
        const depth = fraction * layout.maxDepth;
        const y = sy(depth);
        return (
          <g key={fraction}>
            <line x1={VB.left - 4} y1={y} x2={VB.left + PW} y2={y} stroke="rgba(148,163,184,0.06)" strokeWidth="0.5" />
            <text x={VB.left - 8} y={y + 3} fill="rgba(148,163,184,0.5)" fontSize="9" textAnchor="end">
              {Math.round(depth)} m
            </text>
          </g>
        );
      })}

      {[0, 0.25, 0.5, 0.75, 1].map((fraction) => {
        const range = layout.minRange + (layout.maxRange - layout.minRange) * fraction;
        return (
          <text key={fraction} x={sx(range)} y={VB.top + PH + 18} fill="rgba(148,163,184,0.5)" fontSize="9" textAnchor="middle">
            {range >= 1000 ? `${(range / 1000).toFixed(1)} km` : `${Math.round(range)} m`}
          </text>
        );
      })}
      <text x={VB.left + PW / 2} y={VB.top + PH + 38} fill="rgba(148,163,184,0.4)" fontSize="10" textAnchor="middle">
        俯视图 X 坐标 / m
      </text>
      <text x={14} y={VB.top + PH / 2} fill="rgba(148,163,184,0.4)" fontSize="10" textAnchor="middle" transform={`rotate(-90 14 ${VB.top + PH / 2})`}>
        深度 (Depth)
      </text>

      {orientedActiveLink && (
        <g>
          {orientedActiveLink.rays.map((ray, rayIndex) => {
            const pts = traceRayPath(
              orientedActiveLink.txNode.position[0],
              Math.abs(orientedActiveLink.txNode.position[2]),
              orientedActiveLink.rxNode.position[0],
              Math.abs(orientedActiveLink.rxNode.position[2]),
              ray,
              waterDepth,
            );
            const d = pts.map((point, pointIndex) => `${pointIndex === 0 ? 'M' : 'L'}${sx(point[0]).toFixed(1)} ${sy(point[1]).toFixed(1)}`).join(' ');
            const isDirect = ray.surface_bounces === 0 && ray.bottom_bounces === 0;
            return (
              <path
                key={rayIndex}
                d={d}
                fill="none"
                stroke={orientedActiveLink.isNlos ? (isDirect ? 'rgba(251,113,133,0.72)' : '#f97316') : (isDirect ? 'rgba(52,211,153,0.85)' : 'rgba(96,165,250,0.7)')}
                strokeWidth={isDirect ? 2 : 1.2}
                strokeDasharray={isDirect ? 'none' : '5 3'}
                opacity={0.9}
                onClick={() => onSelectEdge(orientedActiveLink.edgeKey)}
              />
            );
          })}
          <text x={VB.left + 8} y={VB.top + 28} fill="#e7f4ff" fontSize="11">
            {`链路 ${orientedActiveLink.txNode.id} ↔ ${orientedActiveLink.rxNode.id} ｜ ${orientedActiveLink.horizontalDistance >= 1000 ? `${(orientedActiveLink.horizontalDistance / 1000).toFixed(2)} km` : `${Math.round(orientedActiveLink.horizontalDistance)} m`} ｜ ${orientedActiveLink.isNlos ? 'NLOS' : 'LOS'}${activeEventSummary ? ` ｜ ${activeEventSummary}` : ''}`}
          </text>
          <text x={VB.left + 8} y={VB.top + 44} fill="rgba(148,163,184,0.72)" fontSize="10">
            {orientedActiveLink.source === 'metrics'
              ? `未加载 Bellhop 射线，当前按链路度量退化显示直达近似（预估多径 ${orientedActiveLink.multipathCount ?? 1} 条）`
              : orientedActiveLink.source === 'preview'
                ? orientedActiveLink.previewMeta?.mode === 'interpolated'
                  ? `环境库插值射线 ${orientedActiveLink.multipathCount ?? orientedActiveLink.rays.length} 条 ｜ 距离区间 ${formatPreviewBounds(orientedActiveLink.previewMeta?.rangeBoundsM, orientedActiveLink.horizontalDistance)} ｜ 收深区间 ${formatPreviewBounds(orientedActiveLink.previewMeta?.receiverDepthBoundsM, Math.abs(orientedActiveLink.rxNode.position[2]))} ｜ 采样点 ${orientedActiveLink.previewMeta?.contributingSamples ?? 1}`
                  : `环境库最近邻射线 ${orientedActiveLink.multipathCount ?? orientedActiveLink.rays.length} 条 ｜ 采样距 ${Math.round(orientedActiveLink.previewMeta?.nearestRangeM ?? orientedActiveLink.horizontalDistance)} m ｜ 收深 ${Math.round(orientedActiveLink.previewMeta?.nearestReceiverDepthM ?? Math.abs(orientedActiveLink.rxNode.position[2]))} m`
                : `Bellhop 射线 ${orientedActiveLink.multipathCount ?? orientedActiveLink.rays.length} 条`}
          </text>
          {selectedNodeId !== null && focusedEdgeKey === orientedActiveLink.edgeKey && (
            <text x={VB.left + 8} y={VB.top + 60} fill="rgba(125,211,252,0.82)" fontSize="10">
              {`已按节点 #${selectedNodeId} 自动聚焦到当前链路`}
            </text>
          )}
        </g>
      )}

      {!activeLink && preparedLinks.length > 0 && (
        <g transform={`translate(${VB.left + 8}, ${VB.top + 28})`}>
          <text x="0" y="0" fill="#e2e8f0" fontSize="12">节点纵剖面概览</text>
          <text x="0" y="18" fill="rgba(148,163,184,0.72)" fontSize="10">{selectionHint}</text>
        </g>
      )}

      {selectedNodeLinks.length > 1 && (
        <g transform={`translate(${VB.left + 8}, ${VB.top + 58})`}>
          {selectedNodeLinks.slice(0, 4).map((item, index) => {
            const active = activeLink?.edgeKey === item.edgeKey;
            const chipX = index * 140;
            return (
              <g key={item.edgeKey} transform={`translate(${chipX}, 0)`} onClick={() => onSelectEdge(item.edgeKey)} style={{ cursor: 'pointer' }}>
                <rect x="0" y="0" width="128" height="24" rx="8" fill={active ? 'rgba(34,211,238,0.18)' : 'rgba(5,18,32,0.72)'} stroke={active ? 'rgba(34,211,238,0.55)' : 'rgba(148,163,184,0.14)'} />
                <text x="64" y="15" fill={active ? '#67e8f9' : '#cbd5e1'} fontSize="9" textAnchor="middle">
                  {`#${item.txNode.id} → #${item.rxNode.id}`}
                </text>
              </g>
            );
          })}
        </g>
      )}

      {displayNodes.map(({ node }) => {
        const displayPosition = draggingNodeId === node.id && dragPosition ? dragPosition : node.position;
        const cx = sx(displayPosition[0]);
        const cy = sy(Math.abs(displayPosition[2]));
        const radius = node.role === 'sink' ? 10 : 7;
        const picked = pickedNodeIds.includes(node.id);
        return (
          <g key={node.id} onClick={() => handleProfileNodeInteraction(node.id)} style={{ cursor: canDragNodes ? 'grab' : 'pointer' }}>
            <circle
              cx={cx}
              cy={cy}
              r={radius}
              fill={picked ? '#f59e0b' : node.role === 'sink' ? '#22d3ee' : '#60a5fa'}
              fillOpacity={0.88}
              stroke={picked ? 'rgba(251,191,36,0.95)' : 'rgba(255,255,255,0.5)'}
              strokeWidth={picked ? '2' : '1'}
              onPointerDown={canDragNodes ? (event) => startProfileDrag(event, node.id) : undefined}
            />
            <text x={cx} y={cy - radius - 4} fill="#e7f4ff" fontSize="9" textAnchor="middle">
              {`#${node.id}`}
            </text>
            <text x={cx} y={cy + radius + 12} fill="rgba(148,163,184,0.5)" fontSize="8" textAnchor="middle">
              {Math.abs(displayPosition[2])} m
            </text>
          </g>
        );
      })}

      <g transform={`translate(${VB.left + PW - 286}, ${VB.top + 12})`}>
        <rect x="-6" y="-6" width="292" height="48" rx="6" fill="rgba(5,18,32,0.75)" stroke="rgba(148,163,184,0.1)" />
        <line x1="0" y1="8" x2="20" y2="8" stroke="rgba(52,211,153,0.8)" strokeWidth="1.5" />
        <text x="24" y="11" fill="rgba(148,163,184,0.7)" fontSize="9">LOS 直射</text>
        <line x1="68" y1="8" x2="88" y2="8" stroke="rgba(96,165,250,0.6)" strokeWidth="1" strokeDasharray="4 2" />
        <text x="92" y="11" fill="rgba(148,163,184,0.7)" fontSize="9">LOS 反射</text>
        <line x1="140" y1="8" x2="160" y2="8" stroke="rgba(251,113,133,0.7)" strokeWidth="1.5" />
        <text x="164" y="11" fill="rgba(148,163,184,0.7)" fontSize="9">NLOS 直射</text>
        <line x1="210" y1="8" x2="230" y2="8" stroke="#f97316" strokeWidth="1" strokeDasharray="4 2" />
        <text x="234" y="11" fill="rgba(148,163,184,0.7)" fontSize="9">NLOS 反射</text>
        <text x="0" y="32" fill="rgba(148,163,184,0.5)" fontSize="8">
          {isOverviewMode ? '未选中链路时显示节点概览；先后点两个节点即可切到对应链路剖面' : '当前剖面左右顺序与俯视图 X 轴保持一致，不再按节点编号或 tx/rx 翻转'}
        </text>
      </g>
      </svg>

      <section
        style={{
          display: 'flex',
          flexDirection: 'column',
          gap: 10,
          flexShrink: 0,
          padding: '0 4px',
        }}
      >
        <div
          style={{
            display: 'flex',
            alignItems: 'center',
            justifyContent: 'space-between',
            gap: 12,
            flexWrap: 'wrap',
          }}
        >
          <div style={{ display: 'flex', flexDirection: 'column', gap: 2 }}>
            <strong style={{ fontSize: 12, color: '#e2e8f0', fontWeight: 600 }}>链路历史曲线</strong>
            <span style={{ fontSize: 11, color: 'rgba(148,163,184,0.76)' }}>{metricPanelHint}</span>
          </div>
          {orientedActiveLink && (
            <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap' }}>
              <span style={{ fontSize: 10, color: '#67e8f9', padding: '3px 8px', borderRadius: 999, background: 'rgba(34,211,238,0.12)', border: '1px solid rgba(34,211,238,0.2)' }}>
                {`#${orientedActiveLink.txNode.id} ↔ #${orientedActiveLink.rxNode.id}`}
              </span>
              <span style={{ fontSize: 10, color: orientedActiveLink.isNlos ? '#fda4af' : '#86efac', padding: '3px 8px', borderRadius: 999, background: orientedActiveLink.isNlos ? 'rgba(251,113,133,0.1)' : 'rgba(74,222,128,0.1)', border: orientedActiveLink.isNlos ? '1px solid rgba(251,113,133,0.18)' : '1px solid rgba(74,222,128,0.18)' }}>
                {orientedActiveLink.isNlos ? 'NLOS' : 'LOS'}
              </span>
              <span style={{ fontSize: 10, color: 'rgba(226,232,240,0.78)', padding: '3px 8px', borderRadius: 999, background: 'rgba(15,23,42,0.72)', border: '1px solid rgba(148,163,184,0.12)' }}>
                {orientedActiveLink.source === 'metrics'
                  ? '按链路度量退化'
                  : orientedActiveLink.source === 'preview'
                    ? `环境库最近邻 ${orientedActiveLink.rays.length || orientedActiveLink.multipathCount || 0} 条声线`
                    : `Bellhop ${orientedActiveLink.rays.length || orientedActiveLink.multipathCount || 0} 条声线`}
              </span>
            </div>
          )}
        </div>

        {metricTrendCards.length > 0 ? (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 10 }}>
            {metricTrendCards.map((series) => (
              <MetricTrendCard
                key={series.seriesKey}
                {...series}
                markerTime={historyMarkerTime}
              />
            ))}
          </div>
        ) : (
          <div
            style={{
              padding: '12px 14px',
              borderRadius: 14,
              border: '1px solid rgba(148,163,184,0.14)',
              background: 'linear-gradient(180deg, rgba(15,23,42,0.82), rgba(2,6,23,0.76))',
              color: 'rgba(148,163,184,0.78)',
              fontSize: 11,
            }}
          >
            {metricPanelHint}
          </div>
        )}
      </section>

      <div style={{ display: 'flex', alignItems: 'center', gap: 8, flexWrap: 'wrap', flexShrink: 0, padding: '0 4px 4px' }}>
        <span style={{ fontSize: 11, color: 'rgba(148,163,184,0.82)', whiteSpace: 'nowrap' }}>节点选链</span>
        <span style={{ fontSize: 11, color: pickedNodeIds.length === 2 && !activeLink ? 'rgba(248,113,113,0.92)' : 'rgba(148,163,184,0.66)', whiteSpace: 'nowrap' }}>
          {selectionHint}
        </span>
        {nodes
          .slice()
          .sort((left, right) => left.id - right.id)
          .map((node) => {
            const picked = pickedNodeIds.includes(node.id);
            const active = orientedActiveLink?.txNode.id === node.id || orientedActiveLink?.rxNode.id === node.id;
            return (
              <button
                key={node.id}
                type="button"
                onClick={() => handleProfileNodePick(node.id)}
                style={{
                  padding: '4px 8px',
                  borderRadius: 999,
                  border: picked
                    ? '1px solid rgba(251,191,36,0.72)'
                    : active
                      ? '1px solid rgba(34,211,238,0.52)'
                      : '1px solid rgba(148,163,184,0.18)',
                  background: picked
                    ? 'rgba(245,158,11,0.14)'
                    : active
                      ? 'rgba(34,211,238,0.12)'
                      : 'rgba(5,18,32,0.72)',
                  color: picked ? '#fde68a' : active ? '#67e8f9' : '#e2e8f0',
                  fontSize: 11,
                  cursor: 'pointer',
                }}
              >
                {`#${node.id}`}
              </button>
            );
          })}
      </div>
    </div>
  );
}
