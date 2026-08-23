import { useEffect, useMemo, useRef, useState } from 'react';
import type { CSSProperties } from 'react';

import type { BathymetryData, CommunicationEvent, DemoNode, LinkMetric, StudioEnvironmentBounds } from '../types';

export interface CanvasContextTarget {
  type: 'node' | 'canvas';
  nodeId?: number;
  x: number;
  y: number;
}

interface StudioCanvasProps {
  nodes: DemoNode[];
  metrics: LinkMetric[];
  selectedNodeId: number | null;
  selectedEdgeKey: string | null;
  focusedEdgeKeys: string[];
  nodeLabels: Record<number, string[]>;
  edgeLabels: Record<string, string>;
  activeEdgeKeys: string[];
  currentEvents: CommunicationEvent[];
  currentTimeLabel: string;
  frameMotionSeconds: number;
  motionEnabled: boolean;
  usesManualRangeLimit: boolean;
  bathymetry?: BathymetryData;
  environmentBounds?: StudioEnvironmentBounds;
  editable: boolean;
  onSelectNode: (nodeId: number) => void;
  onSelectEdge: (edgeKey: string) => void;
  onSelectScene: () => void;
  onContextMenu: (target: CanvasContextTarget) => void;
  onMoveNode: (nodeId: number, position: [number, number, number]) => void;
}

const VIEWBOX = { width: 980, height: 620, left: 92, right: 70, top: 52, bottom: 88 };

function getBounds(nodes: DemoNode[], environmentBounds?: StudioEnvironmentBounds) {
  if (environmentBounds) {
    return {
      minX: environmentBounds.minX,
      maxX: environmentBounds.maxX,
      minY: environmentBounds.minY,
      maxY: environmentBounds.maxY,
      width: Math.max(1, environmentBounds.maxX - environmentBounds.minX),
      height: Math.max(1, environmentBounds.maxY - environmentBounds.minY),
    };
  }

  const finiteNodes = nodes.filter((node) => (
    Array.isArray(node.position)
    && node.position.length >= 2
    && Number.isFinite(node.position[0])
    && Number.isFinite(node.position[1])
  ));
  if (finiteNodes.length === 0) {
    return {
      minX: -120,
      maxX: 1120,
      minY: -120,
      maxY: 1120,
      width: 1240,
      height: 1240,
    };
  }

  const xValues = finiteNodes.map((node) => node.position[0]);
  const yValues = finiteNodes.map((node) => node.position[1]);
  const minX = Math.min(...xValues, 0);
  const maxX = Math.max(...xValues, 1000);
  const minY = Math.min(...yValues, 0);
  const maxY = Math.max(...yValues, 1000);
  const width = Math.max(1, maxX - minX);
  const height = Math.max(1, maxY - minY);
  const padX = width * 0.12;
  const padY = height * 0.12;
  return {
    minX: minX - padX,
    maxX: maxX + padX,
    minY: minY - padY,
    maxY: maxY + padY,
    width: width + padX * 2,
    height: height + padY * 2,
  };
}

function toSvg(point: [number, number, number], bounds: ReturnType<typeof getBounds>) {
  const plotWidth = VIEWBOX.width - VIEWBOX.left - VIEWBOX.right;
  const plotHeight = VIEWBOX.height - VIEWBOX.top - VIEWBOX.bottom;
  return {
    x: VIEWBOX.left + ((point[0] - bounds.minX) / bounds.width) * plotWidth,
    y: VIEWBOX.height - VIEWBOX.bottom - ((point[1] - bounds.minY) / bounds.height) * plotHeight,
  };
}

function fromSvg(svgX: number, svgY: number, z: number, bounds: ReturnType<typeof getBounds>): [number, number, number] {
  const plotWidth = VIEWBOX.width - VIEWBOX.left - VIEWBOX.right;
  const plotHeight = VIEWBOX.height - VIEWBOX.top - VIEWBOX.bottom;
  const worldX = bounds.minX + ((svgX - VIEWBOX.left) / plotWidth) * bounds.width;
  const worldY = bounds.minY + ((VIEWBOX.height - VIEWBOX.bottom - svgY) / plotHeight) * bounds.height;
  return [Math.round(worldX), Math.round(worldY), z];
}

function toRadiusSvg(range: number, bounds: ReturnType<typeof getBounds>) {
  const plotWidth = VIEWBOX.width - VIEWBOX.left - VIEWBOX.right;
  return (range / bounds.width) * plotWidth;
}

function buildEventCurve(tx: { x: number; y: number }, rx: { x: number; y: number }, arcIndex: number) {
  const dx = rx.x - tx.x;
  const dy = rx.y - tx.y;
  const length = Math.max(Math.hypot(dx, dy), 1);
  const normalX = -dy / length;
  const normalY = dx / length;
  const curvature = Math.min(42, 16 + arcIndex * 10);
  const midX = (tx.x + rx.x) / 2;
  const midY = (tx.y + rx.y) / 2;
  const controlX = midX + normalX * curvature;
  const controlY = midY + normalY * curvature;

  return {
    path: `M ${tx.x.toFixed(1)} ${tx.y.toFixed(1)} Q ${controlX.toFixed(1)} ${controlY.toFixed(1)} ${rx.x.toFixed(1)} ${rx.y.toFixed(1)}`,
    labelX: controlX,
    labelY: controlY,
  };
}

function resolveEventColor(event: CommunicationEvent) {
  if (event.eventCode === 'drop' || event.eventCode === 'route_drop' || !event.withinRange) {
    return 'rgba(248,113,113,0.96)';
  }
  if (event.layer === 'mac') {
    return 'rgba(251,146,60,0.97)';
  }
  if (event.layer === 'routing') {
    return 'rgba(196,181,253,0.97)';
  }
  if (event.eventCode === 'tx') {
    return 'rgba(96,165,250,0.97)';
  }
  if (!event.withinRange) {
    return 'rgba(248,113,113,0.96)';
  }
  if (event.is_nlos) {
    return 'rgba(251,191,36,0.96)';
  }
  return 'rgba(34,211,238,0.96)';
}

function formatEventLayerLabel(layer: string) {
  switch (layer) {
    case 'app':
      return 'APP';
    case 'mac':
      return 'MAC';
    case 'routing':
      return 'Routing';
    case 'phy':
      return 'PHY';
    default:
      return layer ?? '';
  }
}

function formatEventAction(event: CommunicationEvent) {
  switch (event.eventCode) {
    case 'tx':
      return '发送';
    case 'rx':
      return event.is_nlos ? 'NLOS 接收' : '接收';
    case 'drop':
      return '丢弃';
    case 'mac_backoff':
      return '退避';
    case 'mac_wait_slot':
      return '等待时隙';
    case 'mac_wait_poll':
      return '等待轮询';
    case 'route_forward':
      return '路由转发';
    case 'route_drop':
      return '路由丢弃';
    case 'route_expand':
      return '泛洪扩散';
    default:
      return event.eventCode;
  }
}

function formatEventShortAction(event: CommunicationEvent) {
  switch (event.eventCode) {
    case 'tx':
      return '发';
    case 'rx':
      return event.is_nlos ? 'N收' : '收';
    case 'drop':
      return '丢';
    case 'mac_backoff':
      return '退';
    case 'mac_wait_slot':
      return '槽';
    case 'mac_wait_poll':
      return '轮';
    case 'route_forward':
      return '转';
    case 'route_drop':
      return '路丢';
    case 'route_expand':
      return '扩';
    default:
      return '事';
  }
}

function formatEventReason(reason?: string) {
  switch (reason) {
    case 'periodic_report':
      return '周期上报';
    case 'sink_aggregator':
      return '汇聚轮询';
    case 'beacon':
      return '信标广播';
    case 'received':
      return '成功到达';
    case 'received_nlos':
      return 'NLOS 到达';
    case 'metrics_received':
      return '估算到达';
    case 'metrics_out_of_range':
      return '估算超距';
    case 'out_of_range':
      return '超出通信范围';
    case 'no_arrival':
      return '未形成可达路径';
    case 'aloha_wait':
      return 'ALOHA 等待';
    case 'aloha_contention':
      return 'ALOHA 竞争退避';
    case 'csma_wait':
      return 'CSMA 等待';
    case 'csma_channel_busy':
      return '信道忙';
    case 'tdma_wait_slot':
      return '等待本时隙';
    case 'tdma_next_frame':
      return '等待下一帧';
    case 'polling_wait_turn':
      return '等待轮询轮次';
    case 'polling_next_cycle':
      return '等待下一轮询周期';
    case 'static_next_hop':
      return '静态下一跳';
    case 'direct_neighbor':
      return '直连邻居转发';
    case 'flooding_broadcast':
      return '泛洪扩散';
    case 'route_discovery_required':
      return '需要路由发现';
    case 'aodv_cached_route':
      return 'AODV 缓存下一跳';
    case 'olsr_best_effort':
      return 'OLSR 尽力转发';
    case 'no_route':
      return '无路由';
    default:
      return reason ?? '';
  }
}

function describeEventBurst(event: CommunicationEvent) {
  const action = formatEventAction(event);
  if (event.pattern === 'local') {
    return `${formatEventLayerLabel(event.layer)} · ${action}`;
  }
  if (event.pattern === 'broadcast') {
    return `${action} x${event.groupSize}`;
  }
  if (event.pattern === 'many_to_one') {
    return `${action} x${event.groupSize}`;
  }
  return action;
}

function describeEventPair(event: CommunicationEvent) {
  const reasonLabel = formatEventReason(event.reason);
  const suffix = reasonLabel ? ` · ${reasonLabel}` : '';
  if (event.pattern === 'local') {
    return `节点 ${event.tx_id} · ${formatEventLayerLabel(event.layer)} · ${formatEventAction(event)}${suffix}`;
  }
  return `${event.tx_id}→${event.rx_id} · ${formatEventAction(event)}${suffix}`;
}

function formatRoleLabel(node: DemoNode) {
  if (node.role === 'sink') return '汇聚中心';
  if (node.role === 'relay') return `中继节点 ${node.id}`;
  if (node.role === 'anchor') return `锚节点 ${node.id}`;
  if (node.role === 'hil') return `半实物节点 ${node.id}`;
  return `探测传感器 ${node.id}`;
}

function formatFrameSeconds(value: number) {
  if (!Number.isFinite(value)) {
    return '0 s';
  }
  if (value >= 10) {
    return `${value.toFixed(1)} s`;
  }
  if (value >= 1) {
    return `${value.toFixed(2)} s`;
  }
  return `${value.toFixed(3)} s`;
}

export function StudioCanvas({
  nodes,
  metrics,
  selectedNodeId,
  selectedEdgeKey,
  focusedEdgeKeys,
  nodeLabels,
  edgeLabels,
  activeEdgeKeys,
  currentEvents,
  currentTimeLabel,
  frameMotionSeconds,
  motionEnabled,
  usesManualRangeLimit,
  bathymetry,
  environmentBounds,
  editable,
  onSelectNode,
  onSelectEdge,
  onSelectScene,
  onContextMenu,
  onMoveNode,
}: StudioCanvasProps) {
  const svgRef = useRef<SVGSVGElement | null>(null);
  const [draggingNodeId, setDraggingNodeId] = useState<number | null>(null);
  const [dragPosition, setDragPosition] = useState<[number, number, number] | null>(null);
  const bounds = useMemo(() => getBounds(nodes, environmentBounds), [environmentBounds, nodes]);
  const holdEventState = !motionEnabled && currentEvents.length > 0;
  const motionVars = useMemo(() => ({
    ['--studio-edge-motion-seconds' as const]: `${Math.max(frameMotionSeconds * 1.12, 0.2).toFixed(2)}s`,
    ['--studio-event-motion-seconds' as const]: `${Math.max(frameMotionSeconds * 1.22, 0.22).toFixed(2)}s`,
    ['--studio-burst-motion-seconds' as const]: `${Math.max(frameMotionSeconds * 1.3, 0.3).toFixed(2)}s`,
    ['--studio-motion-state' as const]: motionEnabled ? 'running' : 'paused',
  }) as CSSProperties, [frameMotionSeconds, motionEnabled]);
  const mappedNodes = useMemo(
    () => nodes
      .filter((node) => (
        Array.isArray(node.position)
        && node.position.length >= 3
        && node.position.every((value) => Number.isFinite(value))
      ))
      .map((node) => ({ ...node, svg: toSvg(node.position, bounds) })),
    [bounds, nodes],
  );
  const pathEvents = useMemo(
    () => currentEvents.filter((event) => event.pattern !== 'local'),
    [currentEvents],
  );
  const eventByEdgeKey = useMemo(
    () => new Map(pathEvents.map((event) => [event.edgeKey, event] as const)),
    [pathEvents],
  );
  const eventGroups = useMemo(() => {
    const groups = new Map<string, { key: string; label: string; anchorId: number; edgeKey: string; pattern: CommunicationEvent['pattern']; anchor: { x: number; y: number } | null; color: string; focused: boolean }>();

    currentEvents.forEach((event) => {
      const focused = event.pattern === 'local'
        ? selectedNodeId === event.anchorNodeId
        : selectedEdgeKey === event.edgeKey || focusedEdgeKeys.includes(event.edgeKey);
      const existing = groups.get(event.groupKey);
      if (existing) {
        existing.focused = existing.focused || focused;
        return;
      }

      const anchorNodeId = event.anchorNodeId;
      const anchorNode = mappedNodes.find((node) => node.id === anchorNodeId);
      groups.set(event.groupKey, {
        key: event.groupKey,
        label: describeEventBurst(event),
        anchorId: anchorNodeId,
        edgeKey: event.edgeKey,
        pattern: event.pattern,
        anchor: anchorNode ? anchorNode.svg : null,
        color: resolveEventColor(event),
        focused,
      });
    });

    const anchorSlots = new Map<number, number>();
    return Array.from(groups.values())
      .filter((group) => group.anchor !== null)
      .map((group) => {
        const slot = anchorSlots.get(group.anchorId) ?? 0;
        anchorSlots.set(group.anchorId, slot + 1);
        return {
          ...group,
          slot,
        };
      });
  }, [currentEvents, focusedEdgeKeys, mappedNodes, selectedEdgeKey, selectedNodeId]);
  const reachableEventCount = pathEvents.filter((event) => event.withinRange).length;
  const outOfRangeEventCount = pathEvents.length - reachableEventCount;
  const previewOutOfRangeCount = Math.max(0, metrics.length - activeEdgeKeys.length);
  const visibleReachableCount = pathEvents.length > 0 ? reachableEventCount : activeEdgeKeys.length;
  const visibleOutOfRangeCount = pathEvents.length > 0 ? outOfRangeEventCount : previewOutOfRangeCount;
  const eventSummaryItems = currentEvents.slice(0, 8);

  useEffect(() => {
    if (editable) return;
    setDraggingNodeId(null);
    setDragPosition(null);
  }, [editable]);

  useEffect(() => {
    function onPointerMove(event: PointerEvent) {
      if (!editable || draggingNodeId === null || !svgRef.current) return;
      const rect = svgRef.current.getBoundingClientRect();
      const svgX = ((event.clientX - rect.left) / rect.width) * VIEWBOX.width;
      const svgY = ((event.clientY - rect.top) / rect.height) * VIEWBOX.height;
      const clampedX = Math.min(VIEWBOX.width - VIEWBOX.right, Math.max(VIEWBOX.left, svgX));
      const clampedY = Math.min(VIEWBOX.height - VIEWBOX.bottom, Math.max(VIEWBOX.top, svgY));
      const sourceNode = nodes.find((node) => node.id === draggingNodeId);
      if (!sourceNode) return;
      const nextPosition = fromSvg(clampedX, clampedY, sourceNode.position[2], bounds);
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
  }, [bounds, draggingNodeId, editable, nodes, onMoveNode]);

  function handleCanvasCtx(event: React.MouseEvent) {
    event.preventDefault();
    onContextMenu({ type: 'canvas', x: event.clientX, y: event.clientY });
  }

  function handleNodeCtx(event: React.MouseEvent, nodeId: number) {
    event.preventDefault();
    event.stopPropagation();
    onContextMenu({ type: 'node', nodeId, x: event.clientX, y: event.clientY });
  }

  function startDrag(event: React.PointerEvent, nodeId: number) {
    if (!editable) return;
    event.preventDefault();
    event.stopPropagation();
    setDraggingNodeId(nodeId);
    onSelectNode(nodeId);
  }

  const xTicks = Array.from({ length: 5 }, (_, index) => bounds.minX + (bounds.width / 4) * index);
  const yTicks = Array.from({ length: 5 }, (_, index) => bounds.minY + (bounds.height / 4) * index);

  return (
    <div className="studio-canvas" style={motionVars}>
      <svg
        ref={svgRef}
        viewBox={`0 0 ${VIEWBOX.width} ${VIEWBOX.height}`}
        className="studio-canvas__svg"
        style={{ flex: 1, minHeight: 0 }}
        role="img"
        aria-label="模型编排画布"
        onContextMenu={handleCanvasCtx}
      >
        <defs>
          <linearGradient id="studioBg" x1="0%" x2="100%" y1="0%" y2="100%">
            <stop offset="0%" stopColor="rgba(13,27,47,0.95)" />
            <stop offset="100%" stopColor="rgba(5,18,32,0.92)" />
          </linearGradient>
          <marker id="studioArrowActive" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(34,211,238,0.92)" />
          </marker>
          <marker id="studioArrowTx" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(96,165,250,0.96)" />
          </marker>
          <marker id="studioArrowNlos" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(251,191,36,0.96)" />
          </marker>
          <marker id="studioArrowLost" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(248,113,113,0.96)" />
          </marker>
          <marker id="studioArrowRouting" viewBox="0 0 10 10" refX="8" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
            <path d="M 0 0 L 10 5 L 0 10 z" fill="rgba(196,181,253,0.96)" />
          </marker>
        </defs>
        <rect x="0" y="0" width={VIEWBOX.width} height={VIEWBOX.height} rx="30" fill="url(#studioBg)" onClick={onSelectScene} />

        {xTicks.map((tick) => {
          const svgTick = toSvg([tick, bounds.minY, 0], bounds);
          return (
            <g key={`x-${tick}`}>
              <line x1={svgTick.x} y1={VIEWBOX.top} x2={svgTick.x} y2={VIEWBOX.height - VIEWBOX.bottom} className="studio-canvas__grid" />
              <text x={svgTick.x} y={VIEWBOX.height - 46} className="studio-canvas__axis-label">{Math.round(tick)}</text>
            </g>
          );
        })}

        {yTicks.map((tick) => {
          const svgTick = toSvg([bounds.minX, tick, 0], bounds);
          return (
            <g key={`y-${tick}`}>
              <line x1={VIEWBOX.left} y1={svgTick.y} x2={VIEWBOX.width - VIEWBOX.right} y2={svgTick.y} className="studio-canvas__grid" />
              <text x={44} y={svgTick.y + 4} className="studio-canvas__axis-label">{Math.round(tick)}</text>
            </g>
          );
        })}

        <line x1={VIEWBOX.left} y1={VIEWBOX.height - VIEWBOX.bottom} x2={VIEWBOX.width - VIEWBOX.right} y2={VIEWBOX.height - VIEWBOX.bottom} className="studio-canvas__axis" />
        <line x1={VIEWBOX.left} y1={VIEWBOX.top} x2={VIEWBOX.left} y2={VIEWBOX.height - VIEWBOX.bottom} className="studio-canvas__axis" />
        <text x={VIEWBOX.width - VIEWBOX.right} y={VIEWBOX.height - 18} className="studio-canvas__axis-title">X 坐标 / m</text>
        <text x={24} y={VIEWBOX.top - 12} className="studio-canvas__axis-title">Y 坐标 / m</text>

        {/* 海底地形：在俯视图中显示山脊为竖直色带（垂直于 X 轴） */}
        {bathymetry && bathymetry.range_m.length >= 2 && (() => {
          const maxDepth = Math.max(...bathymetry.depth_m);
          const plotLeft = VIEWBOX.left;
          const plotRight = VIEWBOX.width - VIEWBOX.right;
          const plotWidth = plotRight - plotLeft;
          const plotTop = VIEWBOX.top;
          const plotBottom = VIEWBOX.height - VIEWBOX.bottom;
          const plotH = plotBottom - plotTop;

          // For each segment between consecutive bathymetry points, draw a
          // vertical band whose opacity reflects shallowness (ridge = shallow = opaque).
          const bands: React.ReactNode[] = [];
          for (let i = 0; i < bathymetry.range_m.length - 1; i++) {
            const r0 = bathymetry.range_m[i];
            const r1 = bathymetry.range_m[i + 1];
            const d0 = bathymetry.depth_m[i];
            const d1 = bathymetry.depth_m[i + 1];
            const avgDepth = (d0 + d1) / 2;
            // Shallowness ratio: 0 = max depth (flat seabed), 1 = surface
            const shallowness = 1 - avgDepth / maxDepth;
            if (shallowness < 0.08) continue; // skip nearly-flat segments
            const x0 = plotLeft + ((r0 - bounds.minX) / bounds.width) * plotWidth;
            const x1 = plotLeft + ((r1 - bounds.minX) / bounds.width) * plotWidth;
            const w = Math.max(x1 - x0, 1);
            bands.push(
              <rect key={`ridge-${i}`}
                x={x0} y={plotTop} width={w} height={plotH}
                fill={`rgba(160, 110, 50, ${Math.min(shallowness * 0.55, 0.5)})`}
                stroke="none"
              />,
            );
          }

          // Also find the shallowest point for a label
          let minDI = 0;
          for (let i = 1; i < bathymetry.depth_m.length; i++) {
            if (bathymetry.depth_m[i] < bathymetry.depth_m[minDI]) minDI = i;
          }
          const peakX = plotLeft + ((bathymetry.range_m[minDI] - bounds.minX) / bounds.width) * plotWidth;

          return (
            <g className="studio-canvas__terrain">
              {bands}
              {/* Ridge peak label */}
              <line x1={peakX} y1={plotTop} x2={peakX} y2={plotBottom}
                stroke="rgba(210,160,90,0.6)" strokeWidth="1.5" strokeDasharray="6,4" />
              <text
                x={peakX + 6} y={plotTop + 16}
                className="studio-canvas__terrain-label"
                fill="rgba(210,160,90,0.85)"
                fontSize="11"
              >
                {`▲ 山脊 (${bathymetry.depth_m[minDI]}m)`}
              </text>
            </g>
          );
        })()}

        {usesManualRangeLimit && mappedNodes
          .filter((node) => node.role === 'sink' || node.id === selectedNodeId)
          .map((node) => (
            <circle
              key={`range-${node.id}`}
              cx={node.svg.x}
              cy={node.svg.y}
              r={toRadiusSvg(node.communication_range_m ?? 1800, bounds)}
              className={node.role === 'sink' ? 'studio-canvas__range studio-canvas__range--sink' : 'studio-canvas__range'}
            />
          ))}

        {metrics.map((metric) => {
          const tx = mappedNodes.find((node) => node.id === metric.tx_id);
          const rx = mappedNodes.find((node) => node.id === metric.rx_id);
          if (!tx || !rx) return null;

          const edgeKey = `${metric.tx_id}-${metric.rx_id}`;
          const active = activeEdgeKeys.includes(edgeKey);
          const selectedEdge = selectedEdgeKey === edgeKey;
          const focusedEdge = focusedEdgeKeys.includes(edgeKey);
          const event = eventByEdgeKey.get(edgeKey);
          const rangeLimit = usesManualRangeLimit
            ? Math.min(tx.communication_range_m ?? 1800, rx.communication_range_m ?? 1800)
            : null;
          const outOfRange = event
            ? !event.withinRange
            : rangeLimit !== null && metric.pseudo_range_m > rangeLimit;
          const nlos = metric.is_nlos === 1;
          const edgeClasses = ['studio-canvas__edge'];
          if (selectedEdge) {
            edgeClasses.push('studio-canvas__edge--selected');
          } else if (focusedEdge) {
            edgeClasses.push('studio-canvas__edge--focused');
          } else if (outOfRange) {
            edgeClasses.push('studio-canvas__edge--out-of-range', 'studio-canvas__edge--out-of-range-quiet');
          } else if (nlos) {
            edgeClasses.push('studio-canvas__edge--nlos');
          } else if (active) {
            edgeClasses.push('studio-canvas__edge--active');
          }
          const edgeLabel = outOfRange && rangeLimit !== null
            ? `超距 ${metric.pseudo_range_m.toFixed(0)}/${rangeLimit.toFixed(0)} m`
            : edgeLabels[edgeKey] ?? `${metric.delay_s.toFixed(3)} s`;
          return (
            <g key={edgeKey} onClick={(event) => { event.stopPropagation(); onSelectEdge(edgeKey); }}>
              <line
                x1={tx.svg.x} y1={tx.svg.y} x2={rx.svg.x} y2={rx.svg.y}
                className={edgeClasses.join(' ')}
              />
              {nlos && (
                <g className="studio-canvas__nlos-marker">
                  <circle cx={(tx.svg.x + rx.svg.x) / 2} cy={(tx.svg.y + rx.svg.y) / 2 + 6} r={8} />
                  <text x={(tx.svg.x + rx.svg.x) / 2} y={(tx.svg.y + rx.svg.y) / 2 + 10}>✕</text>
                </g>
              )}
              <text
                x={(tx.svg.x + rx.svg.x) / 2}
                y={(tx.svg.y + rx.svg.y) / 2 - 8}
                className={outOfRange ? 'studio-canvas__edge-label studio-canvas__edge-label--out-of-range' : 'studio-canvas__edge-label'}
              >
                {edgeLabel}
              </text>
            </g>
          );
        })}

        {eventGroups.map((group) => {
          if (!group.anchor) return null;
          const radius = (group.focused ? 14 : 11) + group.slot * 6;
          return (
            <g
              key={group.key}
              className="studio-canvas__event-burst"
              onClick={() => {
                if (group.pattern === 'local') {
                  onSelectNode(group.anchorId);
                  return;
                }
                onSelectEdge(group.edgeKey);
              }}
            >
              {motionEnabled && (
                <>
                  <circle cx={group.anchor.x} cy={group.anchor.y} r={radius} className="studio-canvas__event-burst-ring" stroke={group.color} style={{ animationDelay: `${group.slot * 0.08}s` }} />
                  <circle cx={group.anchor.x} cy={group.anchor.y} r={radius} className="studio-canvas__event-burst-ring" stroke={group.color} style={{ animationDelay: `${0.42 + group.slot * 0.08}s` }} />
                </>
              )}
              {holdEventState && (
                <circle
                  cx={group.anchor.x}
                  cy={group.anchor.y}
                  r={radius}
                  className="studio-canvas__event-burst-ring studio-canvas__event-burst-ring--held"
                  stroke={group.color}
                />
              )}
              <circle cx={group.anchor.x} cy={group.anchor.y} r={radius * 0.42} fill="rgba(5,18,32,0.86)" stroke={group.color} strokeWidth="1.2" />
              <text x={group.anchor.x} y={group.anchor.y + radius + 16 + group.slot * 2} className="studio-canvas__event-group-label">
                {group.label}
              </text>
            </g>
          );
        })}

        {pathEvents.map((event) => {
          const tx = mappedNodes.find((node) => node.id === event.tx_id);
          const rx = mappedNodes.find((node) => node.id === event.rx_id);
          if (!tx || !rx) return null;

          const eventCurve = buildEventCurve(tx.svg, rx.svg, event.groupIndex);
          const focused = selectedEdgeKey === event.edgeKey || focusedEdgeKeys.includes(event.edgeKey);
          const color = resolveEventColor(event);
          const showNodeLabel = event.pattern === 'one_to_one' || focused || !event.withinRange || pathEvents.length <= 4;
          const eventLabel = `${formatEventShortAction(event)} ${event.tx_id}→${event.rx_id}`;
          const pathClasses = [
            'studio-canvas__event-path',
            `studio-canvas__event-path--${event.pattern.replace(/_/g, '-')}`,
            focused ? 'studio-canvas__event-path--focused' : '',
            !event.withinRange ? 'studio-canvas__event-path--out-of-range' : '',
            event.eventCode === 'tx' ? 'studio-canvas__event-path--tx' : '',
            event.layer === 'routing' ? 'studio-canvas__event-path--routing' : '',
            event.is_nlos && event.withinRange ? 'studio-canvas__event-path--nlos' : '',
          ].filter(Boolean).join(' ');
          const markerId = !event.withinRange || event.eventCode === 'drop' || event.eventCode === 'route_drop'
            ? 'studioArrowLost'
            : event.layer === 'routing'
              ? 'studioArrowRouting'
            : event.eventCode === 'tx'
              ? 'studioArrowTx'
              : event.is_nlos
                ? 'studioArrowNlos'
                : 'studioArrowActive';

          return (
            <g key={event.id} onClick={(current) => { current.stopPropagation(); onSelectEdge(event.edgeKey); }}>
              <path
                d={eventCurve.path}
                fill="none"
                stroke={color}
                strokeWidth={!event.withinRange ? (focused ? 3.6 : 3) : focused ? 3.4 : 2.8}
                opacity={0.96}
                className={pathClasses}
                markerEnd={`url(#${markerId})`}
              />
              {showNodeLabel && (
                <>
                  <circle cx={eventCurve.labelX} cy={eventCurve.labelY} r={focused ? 13 : 11} fill="rgba(5,18,32,0.86)" stroke={color} strokeWidth="1.2" />
                  <text x={eventCurve.labelX} y={eventCurve.labelY + 3} className="studio-canvas__event-label">
                    {eventLabel}
                  </text>
                </>
              )}
            </g>
          );
        })}

        {mappedNodes.map((node) => {
          const selected = selectedNodeId === node.id;
          // Role-based colors — stable regardless of active frame
          const nodeClass = [
            'studio-canvas__node',
            `studio-canvas__node--role-${node.role}`,
            selected ? 'studio-canvas__node--selected' : '',
          ].filter(Boolean).join(' ');

          const r = node.role === 'sink' ? 20 : 14;
          const displayPosition = draggingNodeId === node.id && dragPosition ? dragPosition : node.position;

          return (
            <g
              key={node.id}
              onClick={(event) => { event.stopPropagation(); onSelectNode(node.id); }}
              onContextMenu={(event) => handleNodeCtx(event, node.id)}
              className="studio-canvas__node-hitbox"
            >
              <circle cx={node.svg.x} cy={node.svg.y} r={r} className={nodeClass} onPointerDown={(event) => startDrag(event, node.id)} />
              <text x={node.svg.x} y={node.svg.y + r + 14} className="studio-canvas__node-name">
                {formatRoleLabel(node)}
              </text>
              <text x={node.svg.x} y={node.svg.y + r + 26} className="studio-canvas__node-role">
                {`${Math.round(displayPosition[0])}, ${Math.round(displayPosition[1])}, ${Math.round(displayPosition[2])}`}
              </text>
              {nodeLabels[node.id]?.slice(0, 1).map((label) => (
                <text key={label} x={node.svg.x} y={node.svg.y + r + 40} className="studio-canvas__node-badge">
                  {label}
                </text>
              ))}
              {(selected || draggingNodeId === node.id) && (
                <text x={node.svg.x} y={node.svg.y - r - 12} className="studio-canvas__drag-label">
                  {`(${Math.round(displayPosition[0])}, ${Math.round(displayPosition[1])}, ${Math.round(displayPosition[2])})`}
                </text>
              )}
            </g>
          );
        })}
      </svg>

      <div style={{ display: 'flex', gap: 8, flexShrink: 0, flexWrap: 'wrap' }}>
        <span className="chip" style={{ fontSize: 11, padding: '3px 9px' }}>节点 · {nodes.length}</span>
        <span className="chip" style={{ fontSize: 11, padding: '3px 9px' }}>链路 · {metrics.length}</span>
        <span className="chip" style={{ fontSize: 11, padding: '3px 9px' }}>当前帧 · {currentTimeLabel}</span>
        <span className="chip" style={{ fontSize: 11, padding: '3px 9px' }}>{motionEnabled ? `节奏 · ${formatFrameSeconds(frameMotionSeconds)}/帧` : `帧窗 · ${formatFrameSeconds(frameMotionSeconds)}`}</span>
        <span className="chip chip--active" style={{ fontSize: 11, padding: '3px 9px' }}>成功 · {visibleReachableCount}</span>
        {visibleOutOfRangeCount > 0 && (
          <span className="chip chip--out-of-range" style={{ fontSize: 11, padding: '3px 9px' }}>越界 · {visibleOutOfRangeCount}</span>
        )}
        <span className="chip" style={{ fontSize: 11, padding: '3px 9px' }}>事件 · {currentEvents.length}</span>
        {!editable && (
          <span className="chip" style={{ fontSize: 11, padding: '3px 9px', borderColor: 'rgba(251,191,36,0.35)', color: '#fde68a' }}>回放锁定</span>
        )}
        {metrics.some((m) => m.is_nlos === 1) && (
          <span className="chip chip--nlos" style={{ fontSize: 11, padding: '3px 9px' }}>NLOS · {metrics.filter((m) => m.is_nlos === 1).length}</span>
        )}
      </div>

      {currentEvents.length > 0 ? (
        <div className="studio-canvas__event-summary">
          <span className="studio-canvas__event-summary-title">当前帧事件</span>
          {eventSummaryItems.map((event) => (
            <button
              key={`summary-${event.id}`}
              type="button"
              className={[
                'studio-canvas__event-pill',
                event.layer === 'mac' ? 'studio-canvas__event-pill--mac' : '',
                event.layer === 'routing' ? 'studio-canvas__event-pill--routing' : '',
                event.eventCode === 'tx' ? 'studio-canvas__event-pill--tx' : '',
                !event.withinRange ? 'studio-canvas__event-pill--out-of-range' : '',
                event.is_nlos && event.withinRange ? 'studio-canvas__event-pill--nlos' : '',
              ].filter(Boolean).join(' ')}
              onClick={() => {
                if (event.pattern === 'local') {
                  onSelectNode(event.anchorNodeId);
                  return;
                }
                onSelectEdge(event.edgeKey);
              }}
            >
              {describeEventPair(event)}
            </button>
          ))}
          {eventSummaryItems.length < currentEvents.length && (
            <span className="studio-canvas__event-summary-more">+{currentEvents.length - eventSummaryItems.length}</span>
          )}
        </div>
      ) : metrics.length > 0 ? (
        <div className="studio-canvas__event-summary studio-canvas__event-summary--hint">
          <span className="studio-canvas__event-summary-title">当前帧事件</span>
          <span className="studio-canvas__event-summary-more">当前为链路预估态，尚未加载逐帧事件。</span>
        </div>
      ) : null}
    </div>
  );
}