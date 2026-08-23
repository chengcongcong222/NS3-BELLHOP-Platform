import { useCallback, useEffect, useMemo, useRef, useState } from 'react';

import { MiniTrend } from '../components/MiniTrend';
import { ProfileView } from '../components/ProfileView';
import { PropertyInspector } from '../components/PropertyInspector';
import { ScenarioWizard } from '../components/ScenarioWizard';
import Scene3DView from '../components/Scene3DView';
import { StudioCanvas } from '../components/StudioCanvas';
import type { CanvasContextTarget } from '../components/StudioCanvas';
import { createNodeTemplate, deleteNodeTemplate, deriveScenario, fetchBathymetry, fetchComponents, fetchDataFiles, fetchEnvironmentDatabases, fetchEvents, fetchExperimentArchiveDetail, fetchExperimentArchives, fetchNodeTemplates, fetchRays, fetchResults, saveScenario, startRun, updateNodeTemplate } from '../services/api';
import type { ExperimentArchiveDetail, ExperimentArchiveSummary, SseEvent } from '../services/api';
import { useStudioRuntimeStore, type LogLevel } from '../stores/studioRuntimeStore';
import type {
  BathymetryData,
  CommunicationEvent,
  DemoDataset,
  DemoNode,
  DemoScenario,
  EnvironmentDatabase,
  LinkMetric,
  LinkRays,
  ModelLibraryAsset,
  ModelLibrarySection,
  NodeTemplate,
  StudioEnvironmentBounds,
  StudioBindingsSnapshot,
  StudioEdgeBinding,
  StudioNodeBinding,
  StudioSceneBinding,
  TraceEventRecord,
} from '../types';

interface StudioPageProps {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
  notifyRunDone: (metrics: LinkMetric[]) => void;
  initialTab?: WorkbenchTab;
}

type WorkbenchTab = 'config' | 'run' | 'results';

const LATEST_RESULTS_SOURCE = '__latest__';

interface NodeTemplateSaveDialogState {
  nodeId: number;
  name: string;
  description: string;
  saving: boolean;
  error: string | null;
}

interface NodeTemplateLibraryDraftState {
  id: string;
  name: string;
  description: string;
  builtIn: boolean;
  saving: boolean;
  error: string | null;
}

const ROLE_DEFAULTS: Record<string, { app: string; mac: string; routing: string; base?: string; model?: string }> = {
  sink: { app: 'sink_aggregator', mac: 'mac-polling', routing: 'routing-static', base: 'node-sink-core' },
  sensor: { app: 'periodic_report', mac: 'mac-aloha', routing: 'routing-flooding', model: 'node-sensor-v1' },
  relay: { app: 'null', mac: 'mac-csma', routing: 'routing-aodv' },
  anchor: { app: 'beacon', mac: 'mac-tdma', routing: 'routing-static' },
  hil: { app: 'periodic_report', mac: 'mac-csma', routing: 'routing-static', base: 'node-hil' },
};

const DEFAULT_NODE_TEMPLATE_ID_BY_ROLE: Record<string, string> = {
  sensor: 'tpl-sensor-periodic',
  sink: 'tpl-sink-aggregator',
  relay: 'tpl-relay-forward',
  anchor: 'tpl-anchor-beacon',
  hil: 'tpl-hil-bridge',
};

const MAC_ASSET_TO_PROTOCOL: Record<string, string> = {
  'mac-aloha': 'aloha',
  'mac-csma': 'csma',
  'mac-tdma': 'tdma',
  'mac-polling': 'polling',
  'mac-no-mac': 'no_mac',
};

const ROUTING_ASSET_TO_PROTOCOL: Record<string, string> = {
  'routing-static': 'static',
  'routing-flooding': 'flooding',
  'routing-aodv': 'aodv',
  'routing-olsr': 'olsr',
};

const PROTOCOL_TO_MAC_ASSET = Object.fromEntries(Object.entries(MAC_ASSET_TO_PROTOCOL).map(([assetId, protocol]) => [protocol, assetId]));
const PROTOCOL_TO_ROUTING_ASSET = Object.fromEntries(Object.entries(ROUTING_ASSET_TO_PROTOCOL).map(([assetId, protocol]) => [protocol, assetId]));

const BUILTIN_NODE_TEMPLATES: NodeTemplate[] = [
  {
    id: 'tpl-sensor-periodic',
    name: '传感器 · 周期上报',
    description: '标准传感模板，周期上报 + ALOHA + Flooding。',
    builtIn: true,
    config: {
      role: 'sensor',
      communication_range_m: 1800,
      tx_power_db: 170,
      center_frequency_hz: 12000,
      mobility: { model: 'static' },
      application: { type: 'periodic_report', period_seconds: 10, packet_size: 512 },
      mac: { protocol: 'aloha' },
      routing: { protocol: 'flooding', ttl: 10 },
      binding: { nodeModelAssetId: 'node-sensor-v1', macProtocolAssetId: 'mac-aloha', routingProtocolAssetId: 'routing-flooding' },
    },
  },
  {
    id: 'tpl-sink-aggregator',
    name: '汇聚节点 · 轮询汇聚',
    description: '中心汇聚模板，Polling + 静态路由。',
    builtIn: true,
    config: {
      role: 'sink',
      communication_range_m: 2500,
      tx_power_db: 180,
      center_frequency_hz: 12000,
      mobility: { model: 'static' },
      application: { type: 'sink_aggregator' },
      mac: { protocol: 'polling', poll_interval_s: 2.0 },
      routing: { protocol: 'static' },
      binding: { nodeBaseAssetId: 'node-sink-core', macProtocolAssetId: 'mac-polling', routingProtocolAssetId: 'routing-static' },
    },
  },
  {
    id: 'tpl-relay-forward',
    name: '中继节点 · 转发增强',
    description: '中继模板，不产生业务流量，负责转发。',
    builtIn: true,
    config: {
      role: 'relay',
      communication_range_m: 2200,
      tx_power_db: 178,
      center_frequency_hz: 12000,
      mobility: { model: 'static' },
      application: { type: 'null' },
      mac: { protocol: 'csma', max_retries: 6 },
      routing: { protocol: 'aodv', hello_interval_s: 1.0, route_timeout_s: 3.0 },
      binding: { macProtocolAssetId: 'mac-csma', routingProtocolAssetId: 'routing-aodv' },
    },
  },
  {
    id: 'tpl-anchor-beacon',
    name: '锚节点 · 定位信标',
    description: '固定锚节点模板，周期信标广播。',
    builtIn: true,
    config: {
      role: 'anchor',
      communication_range_m: 2000,
      tx_power_db: 176,
      center_frequency_hz: 12000,
      mobility: { model: 'static' },
      application: { type: 'beacon', beacon_interval_s: 3, packet_size: 64 },
      mac: { protocol: 'tdma', slot_duration_ms: 100, guard_time_ms: 10 },
      routing: { protocol: 'static' },
      binding: { macProtocolAssetId: 'mac-tdma', routingProtocolAssetId: 'routing-static' },
    },
  },
  {
    id: 'tpl-hil-bridge',
    name: 'HIL 节点 · 桥接接口',
    description: '硬件在环模板，适合外设接入与联调。',
    builtIn: true,
    config: {
      role: 'hil',
      communication_range_m: 1800,
      tx_power_db: 170,
      center_frequency_hz: 12000,
      mobility: { model: 'static' },
      application: { type: 'periodic_report', period_seconds: 10, packet_size: 512 },
      mac: { protocol: 'csma', max_retries: 5 },
      routing: { protocol: 'static' },
      bridge: { type: 'container' },
      binding: { nodeBaseAssetId: 'node-hil', macProtocolAssetId: 'mac-csma', routingProtocolAssetId: 'routing-static' },
    },
  },
];

function buildFallbackNodeTemplates() {
  return cloneScenario(BUILTIN_NODE_TEMPLATES);
}

function buildNodeFromTemplate(template: NodeTemplate, nodeId: number, position: [number, number, number]) {
  const config = cloneScenario(template.config);
  const role = config.role ?? 'sensor';
  const normalizedNode = normalizeNodeForRole({
    id: nodeId,
    role,
    position,
    communication_range_m: config.communication_range_m,
    tx_power_db: config.tx_power_db,
    center_frequency_hz: config.center_frequency_hz,
    mobility: config.mobility ?? { model: 'static' },
    application: config.application,
    mac: config.mac,
    routing: config.routing,
    bridge: config.bridge,
  }, role);

  const binding = sanitizeNodeBindingForRole(role, {
    nodeId,
    nodeTemplateId: template.id,
    nodeBaseAssetId: config.binding?.nodeBaseAssetId,
    nodeModelAssetId: config.binding?.nodeModelAssetId,
    macProtocolAssetId: config.binding?.macProtocolAssetId ?? (config.mac?.protocol ? PROTOCOL_TO_MAC_ASSET[config.mac.protocol] : undefined),
    routingProtocolAssetId: config.binding?.routingProtocolAssetId ?? (config.routing?.protocol ? PROTOCOL_TO_ROUTING_ASSET[config.routing.protocol] : undefined),
    overridesByAssetId: {},
  });

  return { node: normalizedNode, binding };
}

function buildTemplatePayloadFromNode(name: string, description: string, node: DemoNode, binding: StudioNodeBinding): Omit<NodeTemplate, 'id'> {
  return {
    name,
    description,
    builtIn: false,
    config: {
      role: node.role,
      communication_range_m: node.communication_range_m,
      tx_power_db: node.tx_power_db,
      center_frequency_hz: node.center_frequency_hz,
      mobility: cloneScenario(node.mobility),
      application: node.application ? cloneScenario(node.application) : undefined,
      mac: node.mac ? cloneScenario(node.mac) : undefined,
      routing: node.routing ? cloneScenario(node.routing) : undefined,
      bridge: node.bridge ? cloneScenario(node.bridge) : undefined,
      binding: {
        nodeBaseAssetId: binding.nodeBaseAssetId,
        nodeModelAssetId: binding.nodeModelAssetId,
        macProtocolAssetId: binding.macProtocolAssetId,
        routingProtocolAssetId: binding.routingProtocolAssetId,
      },
    },
  };
}

function cloneScenario<T>(value: T): T {
  return JSON.parse(JSON.stringify(value)) as T;
}

function listScenarioEdges(scenario: DemoScenario): Array<[number, number]> {
  if (scenario.topology.deployment_type === 'p2p') {
    if (scenario.topology.pairs && scenario.topology.pairs.length > 0) {
      return scenario.topology.pairs;
    }
    return scenario.nodes.length >= 2 ? [[scenario.nodes[0].id, scenario.nodes[1].id]] : [];
  }

  const centerId = scenario.topology.center ?? scenario.nodes.find((node) => node.role === 'sink')?.id ?? scenario.nodes[0]?.id;
  if (centerId === undefined) {
    return [];
  }
  return scenario.nodes.filter((node) => node.id !== centerId).map((node) => [centerId, node.id]);
}

function computeDistance(a: DemoNode, b: DemoNode) {
  const dx = a.position[0] - b.position[0];
  const dy = a.position[1] - b.position[1];
  const dz = a.position[2] - b.position[2];
  return Math.sqrt(dx * dx + dy * dy + dz * dz);
}

function parseFiniteNumber(value: unknown): number | null {
  if (typeof value === 'number') {
    return Number.isFinite(value) ? value : null;
  }
  if (typeof value === 'string' && value.trim()) {
    const parsed = Number(value);
    return Number.isFinite(parsed) ? parsed : null;
  }
  return null;
}

function clampNumber(value: number, min: number, max: number): number {
  return Math.min(max, Math.max(min, value));
}

function sampleBathymetryDepthAtRange(bathymetry: BathymetryData, range: number): number {
  const { range_m, depth_m } = bathymetry;
  if (range_m.length === 0) {
    return 0;
  }
  if (range <= range_m[0]) {
    return depth_m[0];
  }
  if (range >= range_m[range_m.length - 1]) {
    return depth_m[depth_m.length - 1];
  }

  for (let index = 1; index < range_m.length; index++) {
    if (range <= range_m[index]) {
      const r0 = range_m[index - 1];
      const r1 = range_m[index];
      const d0 = depth_m[index - 1];
      const d1 = depth_m[index];
      const ratio = r1 === r0 ? 0 : (range - r0) / (r1 - r0);
      return d0 + (d1 - d0) * ratio;
    }
  }

  return depth_m[depth_m.length - 1];
}

function buildStudioEnvironmentBounds(
  params: Record<string, unknown>,
  selectedEnvironmentDatabase: EnvironmentDatabase | null,
  bathymetry: BathymetryData | undefined,
  nodes: DemoNode[],
): StudioEnvironmentBounds {
  const rawRangeMax = parseFiniteNumber(params.range_max_m);
  const rawDepthMax = parseFiniteNumber(params.depth_max_m);
  const rawWaterDepth = parseFiniteNumber(params.water_depth_m);
  const recommendedRangeMax = parseFiniteNumber(selectedEnvironmentDatabase?.metadata?.recommendations?.bathymetry_range_max_m);
  const recommendedDepthMax = parseFiniteNumber(selectedEnvironmentDatabase?.metadata?.recommendations?.recommended_depth_max_m);
  const bathymetryRangeMax = bathymetry?.range_m.length ? Math.max(...bathymetry.range_m) : recommendedRangeMax;
  const bathymetryDepthMax = bathymetry?.depth_m.length ? Math.max(...bathymetry.depth_m) : recommendedDepthMax;
  const sampledSourceDepths = selectedEnvironmentDatabase?.build?.source_depths?.length
    ? selectedEnvironmentDatabase.build.source_depths
    : (typeof selectedEnvironmentDatabase?.build?.source_depth_m === 'number'
      ? [selectedEnvironmentDatabase.build.source_depth_m]
      : []);
  const sampledReceiverDepths = selectedEnvironmentDatabase?.build?.receiver_depths ?? [];
  const sampledSourceDepthMax = sampledSourceDepths.length > 0 ? Math.max(...sampledSourceDepths) : null;
  const sampledReceiverDepthMax = sampledReceiverDepths.length > 0 ? Math.max(...sampledReceiverDepths) : null;
  const sampledDepthMax = sampledSourceDepthMax != null && sampledReceiverDepthMax != null
    ? Math.min(sampledSourceDepthMax, sampledReceiverDepthMax)
    : (sampledSourceDepthMax ?? sampledReceiverDepthMax);
  const nodeMaxX = nodes.length > 0 ? Math.max(...nodes.map((node) => Math.max(0, node.position[0]))) : 0;
  const nodeMaxY = nodes.length > 0 ? Math.max(...nodes.map((node) => Math.max(0, node.position[1]))) : 0;
  const nodeMaxDepth = nodes.length > 0 ? Math.max(...nodes.map((node) => Math.abs(node.position[2]))) : 0;

  const rangeMax = Math.max(
    1000,
    bathymetryRangeMax ?? rawRangeMax ?? Math.ceil(Math.max(nodeMaxX, nodeMaxY, 1000) / 100) * 100,
  );
  const depthMax = Math.max(
    50,
    bathymetryDepthMax ?? rawDepthMax ?? rawWaterDepth ?? Math.ceil(Math.max(nodeMaxDepth, 100) / 10) * 10,
  );
  const waterDepth = Math.max(1, Math.min(bathymetryDepthMax ?? rawWaterDepth ?? depthMax, depthMax));
  const editableDepthMax = Math.max(1, Math.min(depthMax, sampledDepthMax ?? depthMax));
  const depthConstraintReason = sampledDepthMax != null && sampledDepthMax < depthMax
    ? (sampledSourceDepthMax != null && sampledReceiverDepthMax != null
      ? `当前环境库 Bellhop 采样仅覆盖 source 0-${Math.round(sampledSourceDepthMax)} m、receiver 0-${Math.round(sampledReceiverDepthMax)} m，节点编辑深度按两者共同范围限制。`
      : `当前环境库 Bellhop 采样深度仅覆盖到 ${Math.round(sampledDepthMax)} m，节点编辑深度已自动收紧。`)
    : null;

  return {
    minX: 0,
    maxX: rangeMax,
    minY: 0,
    maxY: rangeMax,
    rangeMax,
    depthMax,
    waterDepth,
    editableDepthMax,
    depthConstraintReason,
  };
}

function clipBathymetryToEnvironment(
  bathymetry: BathymetryData | undefined,
  bounds: StudioEnvironmentBounds,
): BathymetryData | undefined {
  if (!bathymetry || bathymetry.range_m.length === 0 || bathymetry.depth_m.length === 0) {
    return undefined;
  }

  const maxRange = bounds.rangeMax;
  const maxDepth = Math.min(bounds.depthMax, bounds.waterDepth);
  const clippedRanges: number[] = [0];
  const clippedDepths: number[] = [Math.min(sampleBathymetryDepthAtRange(bathymetry, 0), maxDepth)];

  for (let index = 0; index < bathymetry.range_m.length; index++) {
    const range = bathymetry.range_m[index];
    const depth = bathymetry.depth_m[index];
    if (!Number.isFinite(range) || !Number.isFinite(depth)) {
      continue;
    }
    if (range <= 0 || range >= maxRange) {
      continue;
    }
    clippedRanges.push(range);
    clippedDepths.push(Math.min(depth, maxDepth));
  }

  if (maxRange > 0) {
    clippedRanges.push(maxRange);
    clippedDepths.push(Math.min(sampleBathymetryDepthAtRange(bathymetry, maxRange), maxDepth));
  }

  return {
    range_m: clippedRanges,
    depth_m: clippedDepths,
  };
}

function clampNodePositionToEnvironment(
  position: [number, number, number],
  bounds: StudioEnvironmentBounds,
): [number, number, number] {
  return [
    Math.round(clampNumber(position[0], bounds.minX, bounds.maxX)),
    Math.round(clampNumber(position[1], bounds.minY, bounds.maxY)),
    Math.round(clampNumber(Math.abs(position[2]), 0, bounds.editableDepthMax)),
  ];
}

function buildFlatBathymetryPreview(nodes: DemoNode[], waterDepth: number, rangeMax?: number): BathymetryData {
  const nodeMaxX = nodes.length > 0 ? Math.max(...nodes.map((node) => Math.max(0, node.position[0]))) : 1000;
  const maxRange = Math.max(rangeMax ?? 0, 1000, Math.ceil(nodeMaxX / 100) * 100 + 200);
  return {
    range_m: [0, maxRange],
    depth_m: [waterDepth, waterDepth],
  };
}

function buildP2PPairs(nodes: DemoNode[]): Array<[number, number]> {
  const sinks = nodes.filter((node) => node.role === 'sink');
  if (sinks.length >= 2) {
    return sinks.map((node, index) => [node.id, sinks[(index + 1) % sinks.length].id] as [number, number]);
  }
  const pairs: Array<[number, number]> = [];
  for (let index = 0; index + 1 < nodes.length; index += 2) {
    pairs.push([nodes[index].id, nodes[index + 1].id]);
  }
  if (pairs.length === 0 && nodes.length >= 2) {
    pairs.push([nodes[0].id, nodes[1].id]);
  }
  return pairs;
}

function buildFullMeshPairs(nodes: DemoNode[]): Array<[number, number]> {
  const pairs: Array<[number, number]> = [];
  for (let i = 0; i < nodes.length; i++) {
    for (let j = i + 1; j < nodes.length; j++) {
      pairs.push([nodes[i].id, nodes[j].id]);
    }
  }
  return pairs;
}

function buildLibrary(dataset: DemoDataset): ModelLibrarySection[] {
  const scenarioId = dataset.scenario.scenario_metadata.scenario_id;
  return [
    {
      id: 'topology',
      title: '拓扑与场景模板',
      eyebrow: 'Factory · Structure',
      assets: [
        {
          id: 'topology-star',
          name: 'Star Template',
          kind: 'topology',
          summary: '中心式拓扑模板，适合 broadcast / many-to-one。',
          parameters: ['center node', 'radius', 'fanout'],
          replaceableScopes: ['SceneTemplate', 'TopologyStructure'],
        },
        {
          id: 'topology-p2p',
          name: 'P2P Template',
          kind: 'topology',
          summary: '点对点结构模板，适合 one-to-one 通信。',
          parameters: ['pair count', 'link distance'],
          replaceableScopes: ['SceneTemplate', 'ScenarioVariant'],
        },
        {
          id: 'topology-full-mesh',
          name: 'P2P Full-Mesh',
          kind: 'topology',
          summary: '全连接对等网络，所有节点两两互测。适合 P2P 协作定位。',
          parameters: ['node count', 'edge count'],
          replaceableScopes: ['SceneTemplate', 'ScenarioVariant'],
        },
      ],
    },
    {
      id: 'nodes',
      title: '节点模型库',
      eyebrow: 'Factory · Node Model',
      assets: [
        {
          id: 'node-sensor-v1',
          name: 'Sensor.Localization.V1',
          kind: 'node-model',
          summary: `当前场景 ${scenarioId} 的默认传感节点模型。`,
          parameters: ['application', 'measurement', 'mobility'],
          replaceableScopes: ['NodeBaseType', 'NodeComposition', 'NodeProductModel'],
        },
        {
          id: 'node-sink-core',
          name: 'Sink.Aggregator.Core',
          kind: 'node-base',
          summary: '汇聚中心底座，适合中心式广播与汇聚调度。',
          parameters: ['uplink capacity', 'broadcast window'],
          replaceableScopes: ['NodeBaseType', 'NodeProductModel'],
        },
        {
          id: 'node-hil',
          name: 'Node.HIL',
          kind: 'node-base',
          summary: '半实物接口节点，桥接真实硬件与仿真环境。',
          parameters: ['bridge adapter', 'sync mode'],
          replaceableScopes: ['NodeBaseType', 'NodeProductModel'],
        },
      ],
    },
    {
      id: 'links',
      title: '链路画像库',
      eyebrow: 'Factory · Link Profile',
      assets: [
        {
          id: 'link-bellhop-edge',
          name: 'Bellhop.Edge.Profile',
          kind: 'link-profile',
          summary: '按边挂载 Bellhop 传播模型的链路画像。',
          parameters: ['propagation model', 'noise model', 'edge binding'],
          replaceableScopes: ['LinkProfile', 'PropagationModel'],
        },
        {
          id: 'link-simple-edge',
          name: 'Simple.Edge.Profile',
          kind: 'link-profile',
          summary: '快速评估链路，适合演示和大规模拓扑。',
          parameters: ['delay', 'path loss'],
          replaceableScopes: ['LinkProfile', 'NoiseModel'],
        },
        {
          id: 'link-ssp-ray-edge',
          name: 'SSP.Ray.Profile',
          kind: 'link-profile',
          summary: 'SSP 等梯度声线弧长模型，适合定位算法数据生成。',
          parameters: ['c0', 'tau', 'source level'],
          replaceableScopes: ['LinkProfile', 'PropagationModel'],
        },
      ],
    },
    {
      id: 'protocols',
      title: '协议与通信技术库',
      eyebrow: 'Factory · Technique',
      assets: [
        {
          id: 'tech-broadcast',
          name: 'Center.Broadcast',
          kind: 'communication-technique',
          summary: '中心节点 1 对多下发，适用于 star 结构。',
          parameters: ['fanout', 'frame size'],
          replaceableScopes: ['CommunicationTechnique', 'SceneTemplate'],
        },
        {
          id: 'tech-many-to-one',
          name: 'Many.To.One',
          kind: 'communication-technique',
          summary: '多节点向汇聚中心回传，适用于数据上行。',
          parameters: ['uplink concurrency', 'aggregation window'],
          replaceableScopes: ['CommunicationTechnique', 'MacProtocol'],
        },
        {
          id: 'tech-one-to-one',
          name: 'One.To.One',
          kind: 'communication-technique',
          summary: 'P2P 网络中的点对点通信技术。',
          parameters: ['pair selection', 'frame size'],
          replaceableScopes: ['CommunicationTechnique', 'SceneTemplate'],
        },
        {
          id: 'mac-polling',
          name: 'MAC.Polling',
          kind: 'mac-protocol',
          summary: '轮询调度协议，用于中心式控制与回传。',
          parameters: ['slot', 'order', 'timeout'],
          replaceableScopes: ['MacProtocol', 'NodeComposition'],
        },
        {
          id: 'mac-aloha',
          name: 'MAC.Aloha',
          kind: 'mac-protocol',
          summary: '竞争式接入协议，适合轻量 P2P 片段与快速试验。',
          parameters: ['backoff', 'retry'],
          replaceableScopes: ['MacProtocol', 'NodeComposition'],
        },
        {
          id: 'mac-csma',
          name: 'MAC.CSMA',
          kind: 'mac-protocol',
          summary: '载波侦听接入协议，适合中低密度业务。',
          parameters: ['sense window', 'retry'],
          replaceableScopes: ['MacProtocol', 'NodeComposition'],
        },
        {
          id: 'mac-tdma',
          name: 'MAC.TDMA',
          kind: 'mac-protocol',
          summary: '时隙调度协议，适合可预测的周期上报。',
          parameters: ['slot count', 'frame period'],
          replaceableScopes: ['MacProtocol', 'NodeComposition'],
        },
        {
          id: 'mac-no-mac',
          name: 'MAC.NoMac',
          kind: 'mac-protocol',
          summary: '无 MAC 直通配置，用于极简链路验证。',
          parameters: ['passthrough'],
          replaceableScopes: ['MacProtocol', 'NodeComposition'],
        },
        {
          id: 'routing-static',
          name: 'Routing.Static',
          kind: 'routing-protocol',
          summary: '静态路由，适合拓扑固定的简单场景。',
          parameters: ['route table'],
          replaceableScopes: ['RoutingProtocol', 'NodeComposition'],
        },
        {
          id: 'routing-flooding',
          name: 'Routing.Flooding',
          kind: 'routing-protocol',
          summary: '泛洪路由，适合稀疏网络与快速发现。',
          parameters: ['ttl'],
          replaceableScopes: ['RoutingProtocol', 'NodeComposition'],
        },
        {
          id: 'routing-aodv',
          name: 'Routing.AODV',
          kind: 'routing-protocol',
          summary: '按需路由，适合较大规模动态网络。',
          parameters: ['hello interval'],
          replaceableScopes: ['RoutingProtocol', 'NodeComposition'],
        },
        {
          id: 'routing-olsr',
          name: 'Routing.OLSR',
          kind: 'routing-protocol',
          summary: '主动链路状态路由，适合中心节点协调。',
          parameters: ['topology refresh'],
          replaceableScopes: ['RoutingProtocol', 'NodeComposition'],
        },
      ],
    },
  ];
}

function getAllowedMacAssets(role: string) {
  if (role === 'sink') return ['mac-polling', 'mac-tdma', 'mac-csma', 'mac-no-mac'];
  if (role === 'hil') return ['mac-csma', 'mac-tdma', 'mac-no-mac'];
  if (role === 'anchor') return ['mac-tdma', 'mac-no-mac'];
  if (role === 'relay') return ['mac-aloha', 'mac-csma', 'mac-tdma', 'mac-no-mac'];
  return ['mac-aloha', 'mac-csma', 'mac-tdma', 'mac-no-mac'];
}

function getAllowedRoutingAssets(role: string) {
  if (role === 'sink') return ['routing-static', 'routing-olsr'];
  if (role === 'hil') return ['routing-static', 'routing-olsr'];
  if (role === 'anchor') return ['routing-static'];
  if (role === 'relay') return ['routing-static', 'routing-flooding', 'routing-aodv'];
  return ['routing-static', 'routing-flooding', 'routing-aodv', 'routing-olsr'];
}

function sanitizeNodeBindingForRole(role: string, binding: StudioNodeBinding): StudioNodeBinding {
  const defaults = ROLE_DEFAULTS[role] ?? ROLE_DEFAULTS.sensor;
  const allowedMac = getAllowedMacAssets(role);
  const allowedRouting = getAllowedRoutingAssets(role);

  const next: StudioNodeBinding = {
    ...binding,
    nodeId: binding.nodeId,
    nodeTemplateId: binding.nodeTemplateId,
    nodeBaseAssetId: role === 'sensor' ? undefined : defaults.base,
    nodeModelAssetId: role === 'sensor' ? (binding.nodeModelAssetId ?? defaults.model) : binding.nodeModelAssetId,
    macProtocolAssetId: binding.macProtocolAssetId && allowedMac.includes(binding.macProtocolAssetId) ? binding.macProtocolAssetId : defaults.mac,
    routingProtocolAssetId: binding.routingProtocolAssetId && allowedRouting.includes(binding.routingProtocolAssetId) ? binding.routingProtocolAssetId : defaults.routing,
    overridesByAssetId: binding.overridesByAssetId ?? {},
  };

  if (role === 'sensor') {
    next.nodeBaseAssetId = undefined;
    next.nodeModelAssetId = next.nodeModelAssetId ?? defaults.model;
  }
  if (role === 'sink') {
    next.nodeBaseAssetId = 'node-sink-core';
  }
  if (role === 'hil') {
    next.nodeBaseAssetId = 'node-hil';
  }
  return next;
}

function normalizeNodeForRole(node: DemoNode, role: string): DemoNode {
  const defaults = ROLE_DEFAULTS[role] ?? ROLE_DEFAULTS.sensor;
  const next: DemoNode = { ...node, role };
  if (role === 'sink') {
    if (!next.application || !['sink_aggregator', 'null'].includes(next.application.type)) {
      next.application = { type: defaults.app };
    }
    return next;
  }

  if (role === 'relay') {
    if (!next.application || next.application.type === 'sink_aggregator') {
      next.application = { type: 'null' };
    }
    return next;
  }

  if (role === 'anchor') {
    if (!next.application || ['sink_aggregator', 'periodic_report'].includes(next.application.type)) {
      next.application = { type: 'beacon', beacon_interval_s: 3, packet_size: 64 };
    }
    return next;
  }

  if (!next.application || next.application.type === 'sink_aggregator') {
    next.application = { type: defaults.app, period_seconds: 10, packet_size: 512 };
  }
  return next;
}

function buildInitialNodeBindings(dataset: DemoDataset): Record<number, StudioNodeBinding> {
  const saved = dataset.scenario.ui.studio_bindings?.nodes;
  if (saved) {
    return Object.fromEntries(
      Object.entries(saved).map(([nodeId, binding]) => {
        const role = dataset.scenario.nodes.find((node) => node.id === Number(nodeId))?.role ?? 'sensor';
        return [Number(nodeId), sanitizeNodeBindingForRole(role, { ...binding, nodeId: Number(nodeId), nodeTemplateId: binding.nodeTemplateId ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[role] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor, overridesByAssetId: binding.overridesByAssetId ?? {} })];
      }),
    );
  }

  return Object.fromEntries(
    dataset.scenario.nodes.map((node) => {
      const defaults = ROLE_DEFAULTS[node.role] ?? ROLE_DEFAULTS.sensor;
      return [
        node.id,
        sanitizeNodeBindingForRole(node.role, {
          nodeId: node.id,
          nodeTemplateId: DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[node.role] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor,
          nodeBaseAssetId: node.role === 'sink' ? 'node-sink-core' : node.role === 'hil' ? 'node-hil' : undefined,
          nodeModelAssetId: node.role === 'sensor' ? 'node-sensor-v1' : undefined,
          macProtocolAssetId: PROTOCOL_TO_MAC_ASSET[node.mac?.protocol ?? ''] ?? defaults.mac,
          routingProtocolAssetId: PROTOCOL_TO_ROUTING_ASSET[node.routing?.protocol ?? ''] ?? defaults.routing,
          overridesByAssetId: {},
        }),
      ];
    }),
  );
}

function buildInitialEdgeBindings(dataset: DemoDataset): Record<string, StudioEdgeBinding> {
  const saved = dataset.scenario.ui.studio_bindings?.edges;
  if (saved) {
    return saved;
  }
  return Object.fromEntries(
    listScenarioEdges(dataset.scenario).map(([txId, rxId]) => {
      const edgeKey = `${txId}-${rxId}`;
      const txType = dataset.scenario.transmission.type;
      const assetId = txType === 'bellhop' ? 'link-bellhop-edge' : txType === 'ssp_ray' ? 'link-ssp-ray-edge' : 'link-simple-edge';
      return [
        edgeKey,
        {
          edgeKey,
          linkProfileAssetId: assetId,
          overridesByAssetId: {},
        },
      ];
    }),
  );
}

function buildInitialSceneBinding(dataset: DemoDataset): StudioSceneBinding {
  const saved = dataset.scenario.ui.studio_bindings?.scene;
  if (saved) {
    return saved;
  }
  const isP2P = dataset.scenario.topology.deployment_type === 'p2p';
  const isFullMesh = isP2P && dataset.scenario.topology.logical_type === 'full_mesh';
  return {
    topologyAssetId: isFullMesh ? 'topology-full-mesh' : isP2P ? 'topology-p2p' : 'topology-star',

    communicationTechniqueAssetId: isP2P ? 'tech-one-to-one' : 'tech-many-to-one',
    overridesByAssetId: {},
  };
}

function normalizeBindingsSnapshot(snapshot: StudioBindingsSnapshot): StudioBindingsSnapshot {
  return {
    scene: snapshot.scene,
    nodes: Object.fromEntries(Object.entries(snapshot.nodes).map(([key, value]) => [Number(key), value])),
    edges: snapshot.edges,
  };
}

function createDefaultEdgeBinding(scenario: DemoScenario, edgeKey: string): StudioEdgeBinding {
  return {
    edgeKey,
    linkProfileAssetId: scenario.transmission.type === 'bellhop' ? 'link-bellhop-edge' : 'link-simple-edge',
    overridesByAssetId: {},
  };
}

function syncEdgeBindingsForScenario(scenario: DemoScenario, prev: Record<string, StudioEdgeBinding>): Record<string, StudioEdgeBinding> {
  return Object.fromEntries(
    listScenarioEdges(scenario).map(([txId, rxId]) => {
      const edgeKey = `${txId}-${rxId}`;
      return [edgeKey, prev[edgeKey] ?? createDefaultEdgeBinding(scenario, edgeKey)];
    }),
  );
}

function buildPreviewMetrics(scenario: DemoScenario, baseline: LinkMetric[], edgeBindings: Record<string, StudioEdgeBinding>): LinkMetric[] {
  return listScenarioEdges(scenario).map(([txId, rxId]) => {
    const existing = baseline.find((metric) => metric.tx_id === txId && metric.rx_id === rxId)
      ?? baseline.find((metric) => metric.tx_id === rxId && metric.rx_id === txId);
    if (existing) {
      return { ...existing, tx_id: txId, rx_id: rxId };
    }

    const tx = scenario.nodes.find((node) => node.id === txId);
    const rx = scenario.nodes.find((node) => node.id === rxId);
    const distance = tx && rx ? computeDistance(tx, rx) : 1000;
    const binding = edgeBindings[`${txId}-${rxId}`]?.linkProfileAssetId;
    const isBellhop = binding === 'link-bellhop-edge';
    const delay = distance / 1500;
    const power = Math.max(78, 176 - distance / 38);

    return {
      tx_id: txId,
      rx_id: rxId,
      delay_s: delay,
      receive_power_db: power,
      first_arrival_delay_s: delay,
      received_level_db: power,
      pseudo_range_m: distance,
      multipath_count: isBellhop ? 3 : 1,
    };
  });
}

interface PlaybackFrame {
  index: number;
  time_s: number | null;
  metrics: LinkMetric[];
}

interface NodeMotionSample {
  time_s: number;
  position: [number, number, number];
}

const TIME_PRECISION_DIGITS = 6;
const COMPRESSED_PLAYBACK_BASE_SECONDS = 0.65;

function usesManualRangeLimit(transmissionType?: string | null): boolean {
  return transmissionType !== 'bellhop';
}

function toEdgeKey(txId: number, rxId: number) {
  return `${txId}-${rxId}`;
}

function normalizeTimeKey(value: number) {
  return Number(value.toFixed(TIME_PRECISION_DIGITS));
}

function resolveMetricRangeState(
  metric: LinkMetric,
  nodeMap: Map<number, DemoNode>,
  useManualRangeLimit: boolean,
) {
  if (!useManualRangeLimit) {
    return {
      withinRange: true,
      rangeLimit: null,
    };
  }
  const tx = nodeMap.get(metric.tx_id);
  const rx = nodeMap.get(metric.rx_id);
  const txRange = tx?.communication_range_m ?? 1800;
  const rxRange = rx?.communication_range_m ?? 1800;
  const rangeLimit = Math.min(txRange, rxRange);
  return {
    withinRange: metric.pseudo_range_m <= rangeLimit,
    rangeLimit,
  };
}

function buildPlaybackFrames(metrics: LinkMetric[], timeHints?: number[]): PlaybackFrame[] {
  const timed = metrics.filter((metric) => typeof metric.time_s === 'number');
  if (timed.length === 0) {
    return [{ index: 0, time_s: null, metrics }];
  }

  const grouped = new Map<number, LinkMetric[]>();
  timed.forEach((metric) => {
    const time = normalizeTimeKey(metric.time_s ?? 0);
    grouped.set(time, [...(grouped.get(time) ?? []), metric]);
  });

  if (!timeHints || timeHints.length === 0) {
    return Array.from(grouped.entries())
      .sort((a, b) => a[0] - b[0])
      .map(([time_s, frameMetrics], index) => ({ index, time_s, metrics: frameMetrics }));
  }

  const orderedMetricTimes = Array.from(grouped.keys()).sort((a, b) => a - b);
  const orderedTimeHints = Array.from(new Set(timeHints.map((value) => normalizeTimeKey(value)))).sort((a, b) => a - b);

  return orderedTimeHints.map((time_s, index) => {
    const exact = grouped.get(time_s);
    if (exact) {
      return { index, time_s, metrics: exact };
    }

    const nearestTime = orderedMetricTimes.reduce((best, current) => (
      Math.abs(current - time_s) < Math.abs(best - time_s) ? current : best
    ), orderedMetricTimes[0]);

    return {
      index,
      time_s,
      metrics: grouped.get(nearestTime) ?? [],
    };
  });
}

function finalizeCommunicationEventGroups(events: CommunicationEvent[]): CommunicationEvent[] {
  const buildEventGroupKey = (event: CommunicationEvent) => {
    if (event.pattern === 'local') {
      return `local:${event.layer}:${event.eventCode}:${event.frameIndex}:${event.anchorNodeId}`;
    }
    if (event.pattern === 'broadcast') {
      return `broadcast:${event.layer}:${event.eventCode}:${event.frameIndex}:${event.tx_id}`;
    }
    if (event.pattern === 'many_to_one') {
      return `many_to_one:${event.layer}:${event.eventCode}:${event.frameIndex}:${event.rx_id}`;
    }
    return `one_to_one:${event.layer}:${event.eventCode}:${event.frameIndex}:${event.edgeKey}`;
  };

  const groupCounts = new Map<string, number>();
  events.forEach((event) => {
    const groupKey = buildEventGroupKey(event);
    groupCounts.set(groupKey, (groupCounts.get(groupKey) ?? 0) + 1);
  });

  const groupCounters = new Map<string, number>();
  return events.map((event) => {
    const groupKey = buildEventGroupKey(event);
    const groupIndex = groupCounters.get(groupKey) ?? 0;
    groupCounters.set(groupKey, groupIndex + 1);
    return {
      ...event,
      groupKey,
      groupSize: groupCounts.get(groupKey) ?? 1,
      groupIndex,
    };
  });
}

function buildCommunicationEventsFromMetrics(
  frames: PlaybackFrame[],
  nodes: DemoNode[],
  useManualRangeLimit: boolean,
): CommunicationEvent[] {
  const nodeMap = new Map(nodes.map((node) => [node.id, node]));
  return frames.flatMap((frame) => {
    const txCounts = new Map<number, number>();
    const rxCounts = new Map<number, number>();

    frame.metrics.forEach((metric) => {
      txCounts.set(metric.tx_id, (txCounts.get(metric.tx_id) ?? 0) + 1);
      rxCounts.set(metric.rx_id, (rxCounts.get(metric.rx_id) ?? 0) + 1);
    });

    const drafts = frame.metrics.map((metric, metricIndex) => {
      const edgeKey = toEdgeKey(metric.tx_id, metric.rx_id);
      const txFanout = txCounts.get(metric.tx_id) ?? 1;
      const rxFanIn = rxCounts.get(metric.rx_id) ?? 1;
      const { withinRange } = resolveMetricRangeState(metric, nodeMap, useManualRangeLimit);
      const eventCode: CommunicationEvent['eventCode'] = withinRange ? 'rx' : 'drop';
      const sourceType: CommunicationEvent['sourceType'] = 'metrics';
      const pattern: CommunicationEvent['pattern'] = txFanout > 1
        ? 'broadcast'
        : rxFanIn > 1
          ? 'many_to_one'
          : 'one_to_one';

      return {
        id: `${frame.index}-${metric.tx_id}-${metric.rx_id}-${metricIndex}`,
        frameIndex: frame.index,
        time_s: typeof frame.time_s === 'number' ? frame.time_s : null,
        tx_id: metric.tx_id,
        rx_id: metric.rx_id,
        anchorNodeId: pattern === 'many_to_one' ? metric.rx_id : metric.tx_id,
        edgeKey,
        label: `${metric.tx_id}→${metric.rx_id}`,
        eventCode,
        layer: 'phy',
        sourceType,
        pattern,
        groupKey: '',
        groupSize: 1,
        groupIndex: 0,
        withinRange,
        reason: withinRange ? 'metrics_received' : 'metrics_out_of_range',
        tx_time_s: typeof frame.time_s === 'number' ? frame.time_s : null,
        rx_time_s: typeof frame.time_s === 'number' ? frame.time_s + metric.delay_s : null,
        delay_s: metric.delay_s,
        received_level_db: metric.received_level_db,
        pseudo_range_m: metric.pseudo_range_m,
        snr_db: metric.snr_db,
        is_nlos: metric.is_nlos === 1,
      };
    });
    return finalizeCommunicationEventGroups(drafts);
  });
}

function normalizeTraceEventCode(value: string): CommunicationEvent['eventCode'] {
  const normalized = (value || '').trim().toLowerCase();
  if (normalized === 'tx' || normalized === '+') {
    return 'tx';
  }
  if (normalized === 'rx' || normalized === 'r') {
    return 'rx';
  }
  if (normalized === 'drop' || normalized === 'd' || normalized === '-') {
    return 'drop';
  }
  return normalized || 'rx';
}

function normalizeTraceEventLayer(value?: string): CommunicationEvent['layer'] {
  const normalized = (value || '').trim().toLowerCase();
  return normalized || 'phy';
}

function isLocalTraceEvent(
  row: TraceEventRecord,
  layer: CommunicationEvent['layer'],
  eventCode: CommunicationEvent['eventCode'],
): boolean {
  if (layer === 'mac' && !['tx', 'rx', 'drop'].includes(eventCode)) {
    return true;
  }
  if (row.tx_id === row.rx_id) {
    return true;
  }
  const hasSamePoint = row.tx_x !== undefined
    && row.rx_x !== undefined
    && row.tx_y !== undefined
    && row.rx_y !== undefined
    && row.tx_z !== undefined
    && row.rx_z !== undefined
    && row.tx_x === row.rx_x
    && row.tx_y === row.rx_y
    && row.tx_z === row.rx_z;
  return hasSamePoint;
}

function inferTraceEventWithinRange(
  row: TraceEventRecord,
  eventCode: CommunicationEvent['eventCode'],
): boolean {
  if (typeof row.within_range === 'number') {
    return row.within_range === 1;
  }
  return eventCode !== 'drop' && eventCode !== 'route_drop';
}

function formatEventLayerLabel(layer: string): string {
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
      return layer || '未知层';
  }
}

function formatEventCodeLabel(eventCode: string): string {
  switch (eventCode) {
    case 'tx':
      return '发送';
    case 'rx':
      return '接收';
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
      return eventCode.replace(/_/g, ' ');
  }
}

function buildCommunicationEventsFromTrace(rows: TraceEventRecord[]): CommunicationEvent[] {
  if (rows.length === 0) {
    return [];
  }

  const grouped = new Map<number, TraceEventRecord[]>();
  rows.forEach((row) => {
    const time = normalizeTimeKey(row.time_s ?? row.tx_time_s ?? row.rx_time_s ?? 0);
    grouped.set(time, [...(grouped.get(time) ?? []), row]);
  });

  return Array.from(grouped.entries())
    .sort((a, b) => a[0] - b[0])
    .flatMap(([time_s, eventRows], frameIndex) => {
      const txCounts = new Map<number, number>();
      const rxCounts = new Map<number, number>();

      eventRows.forEach((row) => {
        txCounts.set(row.tx_id, (txCounts.get(row.tx_id) ?? 0) + 1);
        rxCounts.set(row.rx_id, (rxCounts.get(row.rx_id) ?? 0) + 1);
      });

      const drafts = eventRows.map((row, rowIndex) => {
        const eventCode = normalizeTraceEventCode(row.eventCode);
        const layer = normalizeTraceEventLayer(row.layer);
        const txFanout = txCounts.get(row.tx_id) ?? 1;
        const rxFanIn = rxCounts.get(row.rx_id) ?? 1;
        const localEvent = isLocalTraceEvent(row, layer, eventCode);
        const pattern: CommunicationEvent['pattern'] = localEvent
          ? 'local'
          : txFanout > 1
            ? 'broadcast'
            : rxFanIn > 1
              ? 'many_to_one'
              : 'one_to_one';
        const normalizedRxId = localEvent ? row.tx_id : row.rx_id;
        const edgeKey = localEvent ? `local-${row.tx_id}` : toEdgeKey(row.tx_id, normalizedRxId);
        const withinRange = inferTraceEventWithinRange(row, eventCode);
        return {
          id: `trace-${frameIndex}-${row.tx_id}-${row.rx_id}-${rowIndex}-${row.eventCode}`,
          frameIndex,
          time_s,
          tx_id: row.tx_id,
          rx_id: normalizedRxId,
          anchorNodeId: pattern === 'many_to_one' ? normalizedRxId : row.tx_id,
          edgeKey,
          label: localEvent ? `节点 ${row.tx_id}` : `${row.tx_id}→${normalizedRxId}`,
          eventCode,
          layer,
          sourceType: 'trace' as const,
          pattern,
          groupKey: '',
          groupSize: 1,
          groupIndex: 0,
          withinRange,
          reason: row.reason,
          tx_time_s: row.tx_time_s ?? time_s,
          rx_time_s: row.rx_time_s ?? null,
          packet_size: row.packet_size,
          sequence: row.sequence,
          delay_s: row.delay_s ?? Math.max(0, (row.rx_time_s ?? time_s) - (row.tx_time_s ?? time_s)),
          received_level_db: row.received_level_db ?? 0,
          pseudo_range_m: row.pseudo_range_m ?? 0,
          snr_db: row.snr_db,
          is_nlos: row.is_nlos === 1,
        };
      });

      return finalizeCommunicationEventGroups(drafts);
    });
}

function buildTracePlaybackTimes(rows: TraceEventRecord[]): number[] {
  return Array.from(new Set(rows
    .map((row) => row.time_s)
    .filter((value): value is number => typeof value === 'number' && Number.isFinite(value))
    .map((value) => normalizeTimeKey(value))))
    .sort((a, b) => a - b);
}

function getPlaybackStepSeconds(frames: PlaybackFrame[]): number {
  if (frames.length < 2) {
    return 0;
  }

  const deltas: number[] = [];
  for (let index = 1; index < frames.length; index += 1) {
    const previous = frames[index - 1].time_s;
    const current = frames[index].time_s;
    if (typeof previous === 'number' && typeof current === 'number' && current > previous) {
      deltas.push(current - previous);
    }
  }

  if (deltas.length === 0) {
    return 0;
  }
  return Math.min(...deltas);
}

function getPlaybackFrameSeconds(frames: PlaybackFrame[], frameIndex: number, fallbackStepSeconds: number): number {
  if (frames.length === 0) {
    return Math.max(fallbackStepSeconds || 0.1, 0.1);
  }

  const current = frames[frameIndex];
  const currentTime = current?.time_s;
  if (typeof currentTime === 'number') {
    const nextTime = frames[frameIndex + 1]?.time_s;
    if (typeof nextTime === 'number' && nextTime > currentTime) {
      return nextTime - currentTime;
    }

    const previousTime = frames[frameIndex - 1]?.time_s;
    if (typeof previousTime === 'number' && currentTime > previousTime) {
      return currentTime - previousTime;
    }
  }

  return Math.max(fallbackStepSeconds || 0.1, 0.1);
}

function resolveCompressedFrameSeconds(playbackSpeed: number): number {
  return Math.min(1.2, Math.max(0.12, COMPRESSED_PLAYBACK_BASE_SECONDS / Math.max(playbackSpeed, 0.5)));
}

function clampPlaybackTime(value: number, minValue: number, maxValue: number): number {
  if (!Number.isFinite(value)) {
    return minValue;
  }
  if (maxValue < minValue) {
    return minValue;
  }
  return Math.min(maxValue, Math.max(minValue, value));
}

function findPlaybackFrameIndexByTime(frames: PlaybackFrame[], playbackTimeSeconds: number): number {
  if (frames.length === 0) {
    return 0;
  }

  const normalizedTime = normalizeTimeKey(playbackTimeSeconds);
  let activeIndex = 0;
  for (let index = 0; index < frames.length; index += 1) {
    const frameTime = frames[index]?.time_s;
    if (typeof frameTime !== 'number') {
      continue;
    }
    if (frameTime <= normalizedTime + 1e-9) {
      activeIndex = index;
      continue;
    }
    break;
  }
  return activeIndex;
}

function resolveVisibleFrameSeconds(
  frameSeconds: number,
  playbackSpeed: number,
  mode: 'real' | 'compressed',
) {
  if (mode === 'real') {
    return Math.max(0.1, frameSeconds / Math.max(playbackSpeed, 0.5));
  }
  return resolveCompressedFrameSeconds(playbackSpeed);
}

function buildFrameNodes(baseNodes: DemoNode[], frameMetrics: LinkMetric[]): DemoNode[] {
  const positions = new Map<number, [number, number, number]>();
  frameMetrics.forEach((metric) => {
    if (typeof metric.tx_x === 'number' && typeof metric.tx_y === 'number' && typeof metric.tx_z === 'number') {
      positions.set(metric.tx_id, [metric.tx_x, metric.tx_y, metric.tx_z]);
    }
    if (typeof metric.rx_x === 'number' && typeof metric.rx_y === 'number' && typeof metric.rx_z === 'number') {
      positions.set(metric.rx_id, [metric.rx_x, metric.rx_y, metric.rx_z]);
    }
  });
  return baseNodes.map((node) => (positions.has(node.id) ? { ...node, position: positions.get(node.id)! } : node));
}

function buildNodeMotionTracks(metrics: LinkMetric[]): Map<number, NodeMotionSample[]> {
  const buckets = new Map<number, Map<number, { sumX: number; sumY: number; sumZ: number; count: number }>>();

  function record(nodeId: number, time_s: number, x?: number, y?: number, z?: number) {
    if (![x, y, z].every((value) => typeof value === 'number' && Number.isFinite(value))) {
      return;
    }
    const normalizedTime = normalizeTimeKey(time_s);
    const nodeBuckets = buckets.get(nodeId) ?? new Map<number, { sumX: number; sumY: number; sumZ: number; count: number }>();
    const entry = nodeBuckets.get(normalizedTime) ?? { sumX: 0, sumY: 0, sumZ: 0, count: 0 };
    entry.sumX += x as number;
    entry.sumY += y as number;
    entry.sumZ += z as number;
    entry.count += 1;
    nodeBuckets.set(normalizedTime, entry);
    buckets.set(nodeId, nodeBuckets);
  }

  metrics.forEach((metric) => {
    if (typeof metric.time_s !== 'number' || !Number.isFinite(metric.time_s)) {
      return;
    }
    record(metric.tx_id, metric.time_s, metric.tx_x, metric.tx_y, metric.tx_z);
    record(metric.rx_id, metric.time_s, metric.rx_x, metric.rx_y, metric.rx_z);
  });

  return new Map(Array.from(buckets.entries()).map(([nodeId, samplesByTime]) => [
    nodeId,
    Array.from(samplesByTime.entries())
      .sort((left, right) => left[0] - right[0])
      .map(([time_s, value]) => ({
        time_s,
        position: [
          value.sumX / value.count,
          value.sumY / value.count,
          value.sumZ / value.count,
        ] as [number, number, number],
      })),
  ]));
}

function interpolateNodePosition(samples: NodeMotionSample[] | undefined, playbackTimeSeconds: number): [number, number, number] | null {
  if (!samples || samples.length === 0) {
    return null;
  }

  const normalizedTime = normalizeTimeKey(playbackTimeSeconds);
  if (normalizedTime <= samples[0].time_s) {
    return samples[0].position;
  }

  const lastSample = samples[samples.length - 1];
  if (normalizedTime >= lastSample.time_s) {
    return lastSample.position;
  }

  for (let index = 1; index < samples.length; index += 1) {
    const previous = samples[index - 1];
    const next = samples[index];
    if (normalizedTime > next.time_s) {
      continue;
    }
    const span = next.time_s - previous.time_s;
    if (span <= 1e-9) {
      return next.position;
    }
    const ratio = (normalizedTime - previous.time_s) / span;
    return [
      previous.position[0] + (next.position[0] - previous.position[0]) * ratio,
      previous.position[1] + (next.position[1] - previous.position[1]) * ratio,
      previous.position[2] + (next.position[2] - previous.position[2]) * ratio,
    ];
  }

  return lastSample.position;
}

function buildInterpolatedNodes(
  baseNodes: DemoNode[],
  tracks: Map<number, NodeMotionSample[]>,
  playbackTimeSeconds: number,
): DemoNode[] {
  return baseNodes.map((node) => {
    const position = interpolateNodePosition(tracks.get(node.id), playbackTimeSeconds);
    return position ? { ...node, position } : node;
  });
}

function getActiveEdgeKeys(metrics: LinkMetric[], nodes: DemoNode[], useManualRangeLimit: boolean): string[] {
  const nodeMap = new Map(nodes.map((node) => [node.id, node]));
  return metrics
    .filter((metric) => resolveMetricRangeState(metric, nodeMap, useManualRangeLimit).withinRange)
    .map((metric) => toEdgeKey(metric.tx_id, metric.rx_id));
}

function collectIncidentEdgeKeys(nodeId: number, metrics: LinkMetric[], rays: LinkRays[]): string[] {
  const keys = new Set<string>();

  metrics.forEach((metric) => {
    if (metric.tx_id === nodeId || metric.rx_id === nodeId) {
      keys.add(toEdgeKey(metric.tx_id, metric.rx_id));
    }
  });

  rays.forEach((ray) => {
    if (ray.tx_id === nodeId || ray.rx_id === nodeId) {
      keys.add(toEdgeKey(ray.tx_id, ray.rx_id));
    }
  });

  return Array.from(keys);
}

function resolveFocusedEdgeKey(
  selectedEdgeKey: string | null,
  selectedNodeId: number | null,
  metrics: LinkMetric[],
  rays: LinkRays[],
): string | null {
  if (selectedEdgeKey) {
    return selectedEdgeKey;
  }

  if (selectedNodeId !== null) {
    const metricCandidate = metrics
      .filter((metric) => metric.tx_id === selectedNodeId || metric.rx_id === selectedNodeId)
      .sort((left, right) => (right.snr_db ?? right.received_level_db) - (left.snr_db ?? left.received_level_db))[0];
    if (metricCandidate) {
      return toEdgeKey(metricCandidate.tx_id, metricCandidate.rx_id);
    }

    const rayCandidate = rays
      .filter((ray) => ray.tx_id === selectedNodeId || ray.rx_id === selectedNodeId)
      .sort((left, right) => right.direct_ray_amplitude_db - left.direct_ray_amplitude_db)[0];
    if (rayCandidate) {
      return toEdgeKey(rayCandidate.tx_id, rayCandidate.rx_id);
    }
  }

  if (metrics.length === 1) {
    return toEdgeKey(metrics[0].tx_id, metrics[0].rx_id);
  }
  if (rays.length === 1) {
    return toEdgeKey(rays[0].tx_id, rays[0].rx_id);
  }
  return null;
}

function formatCurrentTimeLabel(
  playbackTimeSeconds: number | null,
  frame: PlaybackFrame | undefined,
  runElapsed: string,
): string {
  if (typeof playbackTimeSeconds === 'number' && Number.isFinite(playbackTimeSeconds)) {
    return formatSecondsLabel(playbackTimeSeconds);
  }
  if (!frame || typeof frame.time_s !== 'number') {
    return runElapsed;
  }
  return formatSecondsLabel(frame.time_s);
}

const ENV_DB_ARTIFACT_PARAM_KEYS = ['ssp_file', 'bathymetry_file', 'grid_file', 'arr_json_file'] as const;
const ENV_DB_COMMON_BUILD_PARAM_KEYS = ['frequency_hz', 'range_max_m', 'depth_max_m', 'water_depth_m', 'source_depth_m', 'source_level_db', 'run_type'] as const;
const ENV_DB_ANALYTICAL_BUILD_PARAM_KEYS = ['sound_speed_mps', 'spreading_factor', 'absorption_db_per_km'] as const;
const ENV_DB_BELLHOP_BUILD_PARAM_KEYS = ['max_bounces'] as const;

function getEnvironmentDatabaseBuildParamKeys(buildMode?: string | null) {
  return buildMode === 'analytical'
    ? [...ENV_DB_COMMON_BUILD_PARAM_KEYS, ...ENV_DB_ANALYTICAL_BUILD_PARAM_KEYS]
    : [...ENV_DB_COMMON_BUILD_PARAM_KEYS, ...ENV_DB_BELLHOP_BUILD_PARAM_KEYS];
}

function stripExpandedEnvironmentParams(params: Record<string, unknown>): Record<string, unknown> {
  const nextParams = { ...params };
  for (const key of ENV_DB_ARTIFACT_PARAM_KEYS) {
    delete nextParams[key];
  }
  return nextParams;
}

function stringifyEnvironmentBuildValue(value: unknown): string | null {
  if (value === null || value === undefined || value === '') {
    return null;
  }
  if (Array.isArray(value)) {
    return value.map((item) => String(item)).join(',');
  }
  return String(value);
}

function resolveEnvironmentDatabaseParams(
  scenario: DemoScenario,
  environmentDatabases: EnvironmentDatabase[],
): Record<string, unknown> {
  const params = (scenario.transmission?.params ?? {}) as Record<string, unknown>;
  const databaseId = typeof params.environment_database_id === 'string' ? params.environment_database_id : '';
  if (!databaseId) {
    return params;
  }

  const database = environmentDatabases.find((item) => item.id === databaseId);
  if (!database) {
    return params;
  }

  const resolvedParams: Record<string, unknown> = {
    ...params,
    ssp_file: database.artifacts.ssp_file ?? '',
    bathymetry_file: database.artifacts.bathymetry_file ?? '',
    grid_file: database.artifacts.grid_file,
    arr_json_file: database.artifacts.arr_json_file ?? '',
  };

  const build = database.build as unknown as Record<string, unknown>;
  for (const key of getEnvironmentDatabaseBuildParamKeys(database.metadata?.build_mode)) {
    const current = resolvedParams[key];
    if (typeof current === 'string' ? current.trim() : current) {
      continue;
    }
    const fallback = stringifyEnvironmentBuildValue(build[key]);
    if (fallback !== null) {
      resolvedParams[key] = fallback;
    }
  }

  return resolvedParams;
}

function applyEnvironmentDatabaseToScenario(
  scenario: DemoScenario,
  environmentDatabases: EnvironmentDatabase[],
): DemoScenario {
  if (scenario.transmission?.type !== 'bellhop') {
    return scenario;
  }

  const params = scenario.transmission.params ?? {};
  const databaseId = typeof params.environment_database_id === 'string' ? params.environment_database_id : '';
  if (!databaseId) {
    return scenario;
  }

  const database = environmentDatabases.find((item) => item.id === databaseId);
  if (!database) {
    return scenario;
  }

  return {
    ...scenario,
    transmission: {
      ...scenario.transmission,
      params: {
        ...stripExpandedEnvironmentParams(params as Record<string, unknown>),
        environment_database_id: database.id,
      },
    },
  };
}

function buildScenarioFromBindings(
  scenario: DemoScenario,
  sceneBinding: StudioSceneBinding,
  nodeBindings: Record<number, StudioNodeBinding>,
  edgeBindings: Record<string, StudioEdgeBinding>,
): DemoScenario {
  const nextNodes = scenario.nodes.map((node) => {
    const normalizedNode = normalizeNodeForRole(node, node.role);
    const binding = sanitizeNodeBindingForRole(node.role, nodeBindings[node.id] ?? { nodeId: node.id, overridesByAssetId: {} });
    return {
      ...normalizedNode,
      mac: binding.macProtocolAssetId ? { ...normalizedNode.mac, protocol: MAC_ASSET_TO_PROTOCOL[binding.macProtocolAssetId] ?? 'aloha' } : normalizedNode.mac,
      routing: binding.routingProtocolAssetId ? { ...normalizedNode.routing, protocol: ROUTING_ASSET_TO_PROTOCOL[binding.routingProtocolAssetId] ?? 'static' } : normalizedNode.routing,
    };
  });

  const nextTopology =
    sceneBinding.topologyAssetId === 'topology-full-mesh'
      ? {
          ...scenario.topology,
          deployment_type: 'p2p',
          logical_type: 'full_mesh',
          center: undefined,
          pairs: buildFullMeshPairs(nextNodes),
        }
      : sceneBinding.topologyAssetId === 'topology-p2p'
        ? {
            ...scenario.topology,
            deployment_type: 'p2p',
            logical_type: 'direct_pair',
            center: undefined,
            pairs: buildP2PPairs(nextNodes),
          }
        : {
            ...scenario.topology,
            deployment_type: 'star',
            logical_type: 'center_sink',
            center: nextNodes.find((node) => node.role === 'sink')?.id ?? nextNodes[0]?.id,
            pairs: [],
          };

  const nextSimulation = {
    ...scenario.simulation,
    scheduler: scenario.simulation.scheduler,
  };

  const linkAssetIds = Object.values(edgeBindings)
    .map((binding) => binding.linkProfileAssetId)
    .filter((value): value is string => Boolean(value));
  const allBellhop = linkAssetIds.length > 0 && linkAssetIds.every((value) => value === 'link-bellhop-edge');
  const allSimple = linkAssetIds.length > 0 && linkAssetIds.every((value) => value === 'link-simple-edge');
  const allSspRay = linkAssetIds.length > 0 && linkAssetIds.every((value) => value === 'link-ssp-ray-edge');
  const nextTransmission = allBellhop
    ? { ...scenario.transmission, type: 'bellhop' }
    : allSspRay
      ? { ...scenario.transmission, type: 'ssp_ray' }
      : allSimple
        ? { ...scenario.transmission, type: 'simple' }
        : scenario.transmission;

  return {
    ...scenario,
    nodes: nextNodes,
    simulation: nextSimulation,
    topology: nextTopology,
    transmission: nextTransmission,
    ui: {
      ...scenario.ui,
      default_dashboard: 'studio',
      studio_bindings: normalizeBindingsSnapshot({
        scene: sceneBinding,
        nodes: nodeBindings,
        edges: edgeBindings,
      }),
    },
  };
}

function serializeStudioState(
  scenario: DemoScenario,
  sceneBinding: StudioSceneBinding,
  nodeBindings: Record<number, StudioNodeBinding>,
  edgeBindings: Record<string, StudioEdgeBinding>,
): string {
  return JSON.stringify({
    scenario,
    bindings: normalizeBindingsSnapshot({ scene: sceneBinding, nodes: nodeBindings, edges: edgeBindings }),
  });
}

function detectLevel(msg: string, forceSuccess?: boolean): LogLevel {
  if (forceSuccess) return 'success';
  const upper = msg.toUpperCase();
  if (/\[ERR(OR)?\]/.test(upper)) return 'error';
  if (/\[WARN(ING)?\]/.test(upper)) return 'warn';
  if (/\[SUCCESS\]/.test(upper)) return 'success';
  return 'info';
}

function formatElapsed(startTs: number): string {
  const seconds = Math.round((Date.now() - startTs) / 1000);
  return `${seconds} s`;
}

function formatSecondsLabel(value: number): string {
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

function syntheticSeries(base: number, count = 30, noiseRatio = 0.06): number[] {
  if (!Number.isFinite(base) || base === 0) {
    return Array.from({ length: count }, (_, index) => index * 0.01);
  }

  let seed = Math.floor(base * 1e6) % 2147483647;
  function next(): number {
    seed = (seed * 1664525 + 1013904223) & 0x7fffffff;
    return seed / 0x7fffffff;
  }

  let previous = base;
  return Array.from({ length: count }, () => {
    const drift = (base - previous) * 0.25;
    const noise = (next() - 0.5) * 2 * Math.abs(base) * noiseRatio;
    previous = previous + drift + noise;
    return previous;
  });
}

export function StudioPage({ dataset, activeScenario, loadScenario, notifyRunDone, initialTab = 'config' }: StudioPageProps) {
  const {
    selection,
    setSelection,
    viewMode,
    setViewMode,
    runPhase,
    setRunPhase,
    runLogs,
    appendRunLog: pushRunLog,
    clearRunLogs,
    runStartTs,
    setRunStartTs,
    runElapsed,
    setRunElapsed,
    playbackSpeed,
    setPlaybackSpeed,
    playbackTempoMode,
    setPlaybackTempoMode,
    playbackPlaying,
    setPlaybackPlaying,
    playbackFrameIndex,
    setPlaybackFrameIndex,
    communicationEvents,
    setCommunicationEvents,
    hydrateScenarioRuntime,
  } = useStudioRuntimeStore();
  const [sections, setSections] = useState<ModelLibrarySection[]>(() => buildLibrary(dataset));
  const [nodeTemplates, setNodeTemplates] = useState<NodeTemplate[]>(() => buildFallbackNodeTemplates());
  const [defaultNodeTemplateId, setDefaultNodeTemplateId] = useState(DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor);
  const [nodeTemplatePickerOpen, setNodeTemplatePickerOpen] = useState(false);
  const [nodeTemplateSaveDialog, setNodeTemplateSaveDialog] = useState<NodeTemplateSaveDialogState | null>(null);
  const [templateLibraryOpen, setTemplateLibraryOpen] = useState(false);
  const [templateLibrarySelectionId, setTemplateLibrarySelectionId] = useState<string | null>(null);
  const [templateLibraryDraft, setTemplateLibraryDraft] = useState<NodeTemplateLibraryDraftState | null>(null);
  const [templateDeleteArmId, setTemplateDeleteArmId] = useState<string | null>(null);
  const [workingScenario, setWorkingScenario] = useState<DemoScenario>(() => cloneScenario(dataset.scenario));
  const [nodeBindings, setNodeBindings] = useState<Record<number, StudioNodeBinding>>(() => buildInitialNodeBindings(dataset));
  const [edgeBindings, setEdgeBindings] = useState<Record<string, StudioEdgeBinding>>(() => buildInitialEdgeBindings(dataset));
  const [sceneBinding, setSceneBinding] = useState<StudioSceneBinding>(() => buildInitialSceneBinding(dataset));
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState<string | null>(null);
  const [saveNote, setSaveNote] = useState<string | null>(null);
  const [deriveName, setDeriveName] = useState(`${dataset.scenario.scenario_metadata.scenario_id}_variant`);
  const [ctxMenu, setCtxMenu] = useState<{ x: number; y: number; nodeId?: number } | null>(null);
  const [clipboardNode, setClipboardNode] = useState<DemoNode | null>(null);
  const [bathymetry, setBathymetry] = useState<BathymetryData | undefined>(undefined);
  const [gridFiles, setGridFiles] = useState<string[]>([]);
  const [bathymetryFiles, setBathymetryFiles] = useState<string[]>([]);
  const [environmentDatabases, setEnvironmentDatabases] = useState<EnvironmentDatabase[]>([]);
  const [wizardOpen, setWizardOpen] = useState(false);
  const [rays, setRays] = useState<LinkRays[]>([]);
  const [traceEvents, setTraceEvents] = useState<TraceEventRecord[]>([]);
  const [liveMetrics, setLiveMetrics] = useState<LinkMetric[] | null>(dataset.metrics.length > 0 ? dataset.metrics : null);
  const [resultFile, setResultFile] = useState<string | null>(null);
  const [eventFile, setEventFile] = useState<string | null>(null);
  const [resultArchives, setResultArchives] = useState<ExperimentArchiveSummary[]>([]);
  const [selectedResultSource, setSelectedResultSource] = useState<string>(LATEST_RESULTS_SOURCE);
  const [selectedArchiveDetail, setSelectedArchiveDetail] = useState<ExperimentArchiveDetail | null>(null);
  const [archiveLoading, setArchiveLoading] = useState(false);
  const [eventLayerFilter, setEventLayerFilter] = useState<string>('all');
  const [eventCodeFilter, setEventCodeFilter] = useState<string>('all');
  const [resultError, setResultError] = useState<string | null>(null);
  const [resultsOpen, setResultsOpen] = useState(initialTab === 'results');
  const [logsOpen, setLogsOpen] = useState(initialTab === 'run');
  const [playbackCursorSeconds, setPlaybackCursorSeconds] = useState<number | null>(null);
  const resolvedTransmissionParams = useMemo(
    () => resolveEnvironmentDatabaseParams(workingScenario, environmentDatabases),
    [workingScenario, environmentDatabases],
  );
  const selectedEnvironmentDatabase = useMemo(() => {
    const params = (workingScenario.transmission?.params ?? {}) as Record<string, unknown>;
    const databaseId = typeof params.environment_database_id === 'string' ? params.environment_database_id : '';
    return environmentDatabases.find((item) => item.id === databaseId) ?? null;
  }, [environmentDatabases, workingScenario.transmission?.params]);
  const studioEnvironmentBounds = useMemo(
    () => buildStudioEnvironmentBounds(resolvedTransmissionParams, selectedEnvironmentDatabase, bathymetry, workingScenario.nodes),
    [bathymetry, resolvedTransmissionParams, selectedEnvironmentDatabase, workingScenario.nodes],
  );

  const ctxMenuRef = useRef<HTMLDivElement>(null);
  const logEndRef = useRef<HTMLDivElement | null>(null);
  const abortRef = useRef<AbortController | null>(null);
  const playbackCursorRef = useRef<number | null>(null);

  useEffect(() => {
    playbackCursorRef.current = playbackCursorSeconds;
  }, [playbackCursorSeconds]);

  useEffect(() => {
    fetchComponents()
      .then((data) => {
        if (data.length > 0) {
          setSections(data);
        }
      })
      .catch(() => {
        // use fallback registry
      });
    fetchDataFiles('grid').then(setGridFiles).catch(() => {});
    fetchDataFiles('bathymetry').then(setBathymetryFiles).catch(() => {});
    fetchEnvironmentDatabases().then(setEnvironmentDatabases).catch(() => {});
    fetchNodeTemplates().then((data) => {
      if (data.length > 0) {
        setNodeTemplates(data);
      }
    }).catch(() => {});
  }, []);

  useEffect(() => {
    if (nodeTemplates.length === 0) return;
    if (!nodeTemplates.some((template) => template.id === defaultNodeTemplateId)) {
      setDefaultNodeTemplateId(nodeTemplates[0].id);
    }
  }, [defaultNodeTemplateId, nodeTemplates]);

  function openTemplateLibrary(initialTemplateId?: string) {
    const selected = nodeTemplates.find((template) => template.id === initialTemplateId)
      ?? nodeTemplates.find((template) => template.id === defaultNodeTemplateId)
      ?? nodeTemplates[0]
      ?? null;
    if (!selected) return;
    setTemplateLibraryOpen(true);
    setTemplateLibrarySelectionId(selected.id);
    setTemplateLibraryDraft({
      id: selected.id,
      name: selected.name,
      description: selected.description ?? '',
      builtIn: Boolean(selected.builtIn),
      saving: false,
      error: null,
    });
    setTemplateDeleteArmId(null);
  }

  // 加载海底地形数据（若场景配置了 bathymetry_file）
  useEffect(() => {
    const bathPath = resolvedTransmissionParams.bathymetry_file;
    if (typeof bathPath === 'string' && bathPath) {
      fetchBathymetry(bathPath).then((data) => {
        if (data && data.range_m && data.depth_m) {
          setBathymetry(data);
        } else {
          setBathymetry(undefined);
        }
      });
    } else {
      setBathymetry(undefined);
    }
  }, [resolvedTransmissionParams.bathymetry_file]);

  // 加载声线数据（从上次仿真结果）
  useEffect(() => {
    fetchRays(activeScenario).then((r) => setRays(r)).catch(() => setRays([]));
  }, [activeScenario]);

  useEffect(() => {
    fetchEvents(activeScenario)
      .then(({ file, rows }) => {
        setEventFile(file);
        setTraceEvents(rows);
      })
      .catch(() => {
        setEventFile(null);
        setTraceEvents([]);
      });
  }, [activeScenario]);

  useEffect(() => {
    setSelectedResultSource(LATEST_RESULTS_SOURCE);
    setSelectedArchiveDetail(null);
  }, [activeScenario]);

  useEffect(() => {
    const nextScenario = cloneScenario(dataset.scenario);
    setWorkingScenario(nextScenario);
    setNodeBindings(buildInitialNodeBindings(dataset));
    setEdgeBindings(buildInitialEdgeBindings(dataset));
    setSceneBinding(buildInitialSceneBinding(dataset));
    setSelection({ scope: 'scene' });
    setSaveError(null);
    setSaveNote(null);
    setDeriveName(`${dataset.scenario.scenario_metadata.scenario_id}_variant`);
    hydrateScenarioRuntime(dataset.scenario.ui.playback_speed ?? 1);
    setLiveMetrics(dataset.metrics.length > 0 ? dataset.metrics : null);
    setTraceEvents([]);
    setEventFile(null);
  }, [activeScenario, dataset.scenario, hydrateScenarioRuntime, setSelection]);

  useEffect(() => {
    setWorkingScenario((prev) => applyEnvironmentDatabaseToScenario(prev, environmentDatabases));
  }, [environmentDatabases]);

  useEffect(() => {
    if (initialTab === 'results') {
      setResultsOpen(true);
    }
    if (initialTab === 'run') {
      setLogsOpen(true);
    }
  }, [initialTab]);

  const baselineMetrics = liveMetrics ?? dataset.metrics;
  const scenarioArchives = useMemo(
    () => resultArchives.filter((archive) => archive.scenario === activeScenario),
    [activeScenario, resultArchives],
  );
  const selectedArchiveSummary = useMemo(
    () => scenarioArchives.find((archive) => archive.id === selectedResultSource) ?? null,
    [scenarioArchives, selectedResultSource],
  );
  const archiveResultActive = selectedResultSource !== LATEST_RESULTS_SOURCE;
  const previewMetrics = useMemo(
    () => buildPreviewMetrics(workingScenario, baselineMetrics, edgeBindings),
    [baselineMetrics, edgeBindings, workingScenario],
  );

  const persistedSnapshot = useMemo(
    () => serializeStudioState(dataset.scenario, buildInitialSceneBinding(dataset), buildInitialNodeBindings(dataset), buildInitialEdgeBindings(dataset)),
    [dataset],
  );
  const [committedSnapshot, setCommittedSnapshot] = useState(persistedSnapshot);
  const currentSnapshot = useMemo(
    () => serializeStudioState(workingScenario, sceneBinding, nodeBindings, edgeBindings),
    [edgeBindings, nodeBindings, sceneBinding, workingScenario],
  );
  const isDirty = currentSnapshot !== committedSnapshot;

  useEffect(() => {
    setCommittedSnapshot(persistedSnapshot);
  }, [persistedSnapshot]);

  const selectedEdgeKey = selection.scope === 'edge' ? selection.edgeKey : null;
  const selectedNodeId = selection.scope === 'node' ? selection.nodeId : null;
  const displayTitle = workingScenario.scenario_metadata.name || workingScenario.scenario_metadata.scenario_id;

  const allAssets = useMemo(() => sections.flatMap((section) => section.assets), [sections]);
  const nodeLabels = useMemo(
    () => Object.fromEntries(
      workingScenario.nodes.map((node) => {
        const binding = nodeBindings[node.id];
        const labels = [binding?.nodeBaseAssetId, binding?.nodeModelAssetId, binding?.macProtocolAssetId, binding?.routingProtocolAssetId]
          .filter(Boolean)
          .map((assetId) => allAssets.find((asset) => asset.id === assetId)?.name ?? assetId as string);
        return [node.id, labels];
      }),
    ),
    [allAssets, nodeBindings, workingScenario.nodes],
  );

  const edgeLabels = useMemo<Record<string, string>>(
    () => Object.fromEntries(
      previewMetrics
        .map((metric) => {
          const edgeKey = `${metric.tx_id}-${metric.rx_id}`;
          const binding = edgeBindings[edgeKey];
          if (!binding?.linkProfileAssetId) {
            return null;
          }
          const label = allAssets.find((asset) => asset.id === binding.linkProfileAssetId)?.name ?? binding.linkProfileAssetId;
          return [edgeKey, label] as const;
        })
        .filter((entry): entry is readonly [string, string] => entry !== null),
    ),
    [allAssets, edgeBindings, previewMetrics],
  );

  const displayMetrics = archiveResultActive ? (selectedArchiveDetail?.metrics ?? []) : baselineMetrics;
  const displayResultFile = archiveResultActive
    ? (selectedArchiveDetail?.files.results_csv ?? selectedArchiveDetail?.files.manifest ?? null)
    : resultFile;
  const displayEventFile = archiveResultActive
    ? (selectedArchiveDetail?.files.events_json ?? null)
    : eventFile;
  const displayRayCount = archiveResultActive
    ? (selectedArchiveDetail?.rays.length ?? 0)
    : rays.length;
  const displayEventCount = archiveResultActive
    ? (selectedArchiveDetail?.events.length ?? 0)
    : traceEvents.length;
  const resultMetricEdgeKeys = useMemo(
    () => Array.from(new Set(baselineMetrics.map((metric) => toEdgeKey(metric.tx_id, metric.rx_id)))),
    [baselineMetrics],
  );
  const tracePlaybackTimes = useMemo(
    () => buildTracePlaybackTimes(traceEvents),
    [traceEvents],
  );
  const metricCommunicationFrames = useMemo(
    () => buildPlaybackFrames(displayMetrics),
    [displayMetrics],
  );
  const useManualLinkRange = useMemo(
    () => usesManualRangeLimit(workingScenario.transmission?.type),
    [workingScenario.transmission?.type],
  );
  const derivedMetricEvents = useMemo(
    () => buildCommunicationEventsFromMetrics(metricCommunicationFrames, workingScenario.nodes, useManualLinkRange),
    [metricCommunicationFrames, useManualLinkRange, workingScenario.nodes],
  );
  const traceCommunicationEvents = useMemo(
    () => buildCommunicationEventsFromTrace(traceEvents),
    [traceEvents],
  );
  const usingTraceEvents = traceCommunicationEvents.length > 0;
  const playbackSourceMetrics = displayMetrics.length > 0 ? displayMetrics : previewMetrics;
  const metricPlaybackFrames = useMemo(
    () => buildPlaybackFrames(playbackSourceMetrics),
    [playbackSourceMetrics],
  );
  const nodeMotionTracks = useMemo(
    () => buildNodeMotionTracks(playbackSourceMetrics),
    [playbackSourceMetrics],
  );
  const playbackFrames = useMemo(
    () => buildPlaybackFrames(
      playbackSourceMetrics,
      tracePlaybackTimes.length > 0 ? tracePlaybackTimes : undefined,
    ),
    [playbackSourceMetrics, tracePlaybackTimes],
  );
  const storedPlaybackFrameIndex = Math.min(playbackFrameIndex, Math.max(0, playbackFrames.length - 1));
  const playbackTimelineBounds = useMemo(() => {
    const timedFrames = playbackFrames.filter(
      (frame): frame is PlaybackFrame & { time_s: number } => typeof frame.time_s === 'number',
    );
    if (timedFrames.length === 0) {
      return { start: 0, end: 0 };
    }
    return {
      start: timedFrames[0].time_s,
      end: timedFrames[timedFrames.length - 1].time_s,
    };
  }, [playbackFrames]);
  const playbackTimelineStartSeconds = playbackTimelineBounds.start;
  const playbackTimelineEndSeconds = playbackTimelineBounds.end;
  const activePlaybackTimeSeconds = useMemo(() => {
    if (playbackTempoMode === 'real' && typeof playbackCursorSeconds === 'number') {
      return clampPlaybackTime(
        playbackCursorSeconds,
        playbackTimelineStartSeconds,
        playbackTimelineEndSeconds,
      );
    }
    const frameTime = playbackFrames[storedPlaybackFrameIndex]?.time_s;
    return typeof frameTime === 'number' ? frameTime : null;
  }, [
    playbackCursorSeconds,
    playbackFrames,
    playbackTempoMode,
    playbackTimelineEndSeconds,
    playbackTimelineStartSeconds,
    storedPlaybackFrameIndex,
  ]);
  const activePlaybackFrameIndex = useMemo(() => {
    if (playbackTempoMode === 'real' && typeof activePlaybackTimeSeconds === 'number') {
      return findPlaybackFrameIndexByTime(playbackFrames, activePlaybackTimeSeconds);
    }
    return storedPlaybackFrameIndex;
  }, [activePlaybackTimeSeconds, playbackFrames, playbackTempoMode, storedPlaybackFrameIndex]);
  const currentPlaybackFrame = playbackFrames[activePlaybackFrameIndex];
  const activeMetricFrameIndex = useMemo(() => {
    if (metricPlaybackFrames.length === 0) {
      return 0;
    }
    if (typeof activePlaybackTimeSeconds === 'number') {
      return findPlaybackFrameIndexByTime(metricPlaybackFrames, activePlaybackTimeSeconds);
    }
    const frameTime = currentPlaybackFrame?.time_s;
    if (typeof frameTime === 'number') {
      return findPlaybackFrameIndexByTime(metricPlaybackFrames, frameTime);
    }
    return Math.min(storedPlaybackFrameIndex, Math.max(0, metricPlaybackFrames.length - 1));
  }, [activePlaybackTimeSeconds, currentPlaybackFrame?.time_s, metricPlaybackFrames, storedPlaybackFrameIndex]);
  const frameMetrics = useMemo(
    () => {
      if (playbackTempoMode === 'compressed') {
        return currentPlaybackFrame?.metrics ?? metricPlaybackFrames[activeMetricFrameIndex]?.metrics ?? [];
      }
      return metricPlaybackFrames[activeMetricFrameIndex]?.metrics ?? currentPlaybackFrame?.metrics ?? [];
    },
    [activeMetricFrameIndex, currentPlaybackFrame, metricPlaybackFrames, playbackTempoMode],
  );
  const canvasNodes = useMemo(
    () => {
      if (runPhase === 'idle') {
        return workingScenario.nodes;
      }
      if (playbackTempoMode === 'real' && typeof activePlaybackTimeSeconds === 'number') {
        return buildInterpolatedNodes(workingScenario.nodes, nodeMotionTracks, activePlaybackTimeSeconds);
      }
      return buildFrameNodes(workingScenario.nodes, frameMetrics);
    },
    [activePlaybackTimeSeconds, frameMetrics, nodeMotionTracks, playbackTempoMode, runPhase, workingScenario.nodes],
  );
  const activeEdgeKeys = useMemo(
    () => getActiveEdgeKeys(frameMetrics, canvasNodes, useManualLinkRange),
    [canvasNodes, frameMetrics, useManualLinkRange],
  );
  const focusedEdgeKey = useMemo(
    () => resolveFocusedEdgeKey(selectedEdgeKey, selectedNodeId, frameMetrics, rays),
    [frameMetrics, rays, selectedEdgeKey, selectedNodeId],
  );
  const focusedEdgeKeys = useMemo(() => {
    if (selectedNodeId !== null) {
      const incident = collectIncidentEdgeKeys(selectedNodeId, frameMetrics, rays);
      return focusedEdgeKey ? Array.from(new Set([...incident, focusedEdgeKey])) : incident;
    }
    return focusedEdgeKey ? [focusedEdgeKey] : [];
  }, [focusedEdgeKey, frameMetrics, rays, selectedNodeId]);
  const currentFrameEvents = useMemo(
    () => communicationEvents.filter((event) => (
      event.frameIndex === activePlaybackFrameIndex
      && (eventLayerFilter === 'all' || event.layer === eventLayerFilter)
      && (eventCodeFilter === 'all' || event.eventCode === eventCodeFilter)
    )),
    [activePlaybackFrameIndex, communicationEvents, eventCodeFilter, eventLayerFilter],
  );
  const currentFrameEventCount = useMemo(
    () => communicationEvents.filter((event) => event.frameIndex === activePlaybackFrameIndex).length,
    [activePlaybackFrameIndex, communicationEvents],
  );
  const focusedEvent = useMemo(() => {
    if (focusedEdgeKey) {
      return currentFrameEvents.find((event) => event.edgeKey === focusedEdgeKey) ?? null;
    }
    if (selectedNodeId !== null) {
      return currentFrameEvents.find((event) => event.tx_id === selectedNodeId || event.rx_id === selectedNodeId) ?? null;
    }
    return currentFrameEvents[0] ?? null;
  }, [currentFrameEvents, focusedEdgeKey, selectedNodeId]);
  const currentTimeLabel = useMemo(
    () => formatCurrentTimeLabel(activePlaybackTimeSeconds, currentPlaybackFrame, runElapsed),
    [activePlaybackTimeSeconds, currentPlaybackFrame, runElapsed],
  );
  const playbackStepSeconds = useMemo(
    () => getPlaybackStepSeconds(playbackFrames),
    [playbackFrames],
  );
  const currentFrameStepSeconds = useMemo(
    () => getPlaybackFrameSeconds(playbackFrames, activePlaybackFrameIndex, playbackStepSeconds),
    [activePlaybackFrameIndex, playbackFrames, playbackStepSeconds],
  );
  const currentFrameVisibleSeconds = useMemo(
    () => resolveVisibleFrameSeconds(currentFrameStepSeconds, playbackSpeed, playbackTempoMode),
    [currentFrameStepSeconds, playbackSpeed, playbackTempoMode],
  );
  const hasPlaybackTimeline = playbackFrames.length > 1 && typeof playbackFrames[0]?.time_s === 'number';
  const playbackVisibleDurationSeconds = useMemo(() => {
    if (playbackFrames.length === 0) {
      return 0.1;
    }
    if (playbackTempoMode === 'real' && hasPlaybackTimeline) {
      const timelineSpan = Math.max(0, playbackTimelineEndSeconds - playbackTimelineStartSeconds);
      return Math.max(0.1, timelineSpan / Math.max(playbackSpeed, 0.5));
    }
    return playbackFrames.reduce((sum, _frame, index) => (
      sum + resolveVisibleFrameSeconds(
        getPlaybackFrameSeconds(playbackFrames, index, playbackStepSeconds),
        playbackSpeed,
        playbackTempoMode,
      )
    ), 0);
  }, [
    hasPlaybackTimeline,
    playbackFrames,
    playbackSpeed,
    playbackStepSeconds,
    playbackTempoMode,
    playbackTimelineEndSeconds,
    playbackTimelineStartSeconds,
  ]);
  const playbackWallClock = useMemo(
    () => formatSecondsLabel(Math.max(0.1, playbackVisibleDurationSeconds)),
    [playbackVisibleDurationSeconds],
  );
  const playbackProgressLabel = useMemo(() => {
    if (playbackTempoMode === 'real' && hasPlaybackTimeline) {
      const currentTime = typeof activePlaybackTimeSeconds === 'number'
        ? activePlaybackTimeSeconds
        : playbackTimelineStartSeconds;
      return `${formatSecondsLabel(currentTime)}/${formatSecondsLabel(playbackTimelineEndSeconds)}`;
    }
    return `${activePlaybackFrameIndex + 1}/${Math.max(1, playbackFrames.length)}`;
  }, [
    activePlaybackFrameIndex,
    activePlaybackTimeSeconds,
    hasPlaybackTimeline,
    playbackFrames.length,
    playbackTempoMode,
    playbackTimelineEndSeconds,
    playbackTimelineStartSeconds,
  ]);
  const playbackSliderMin = playbackTempoMode === 'real' && hasPlaybackTimeline
    ? playbackTimelineStartSeconds
    : 0;
  const playbackSliderMax = playbackTempoMode === 'real' && hasPlaybackTimeline
    ? playbackTimelineEndSeconds
    : Math.max(0, playbackFrames.length - 1);
  const playbackSliderStep = playbackTempoMode === 'real' && hasPlaybackTimeline
    ? Math.max(0.01, playbackStepSeconds > 0 ? playbackStepSeconds / 10 : 0.01)
    : 1;
  const playbackSliderValue = playbackTempoMode === 'real' && hasPlaybackTimeline
    ? (typeof activePlaybackTimeSeconds === 'number' ? activePlaybackTimeSeconds : playbackTimelineStartSeconds)
    : activePlaybackFrameIndex;
  const canPlaybackStep = hasPlaybackTimeline && activePlaybackFrameIndex < playbackFrames.length - 1;
  const displayBathymetry = useMemo(
    () => clipBathymetryToEnvironment(
      bathymetry ?? buildFlatBathymetryPreview(workingScenario.nodes, studioEnvironmentBounds.waterDepth, studioEnvironmentBounds.rangeMax),
      studioEnvironmentBounds,
    ) ?? buildFlatBathymetryPreview(workingScenario.nodes, studioEnvironmentBounds.waterDepth, studioEnvironmentBounds.rangeMax),
    [bathymetry, studioEnvironmentBounds, workingScenario.nodes],
  );
  const timeSeries = useMemo(() => {
    if (displayMetrics.length === 0) {
      return {
        delay: syntheticSeries(0.5),
        power: syntheticSeries(100),
        range: syntheticSeries(1000),
      };
    }
    const avgDelay = displayMetrics.reduce((sum, metric) => sum + metric.delay_s, 0) / displayMetrics.length;
    const avgPower = displayMetrics.reduce((sum, metric) => sum + metric.received_level_db, 0) / displayMetrics.length;
    const avgRange = displayMetrics.reduce((sum, metric) => sum + metric.pseudo_range_m, 0) / displayMetrics.length;
    const hasNoise = displayMetrics.some((m) => m.noise_level_db !== undefined);
    const avgNoise = hasNoise ? displayMetrics.reduce((sum, m) => sum + (m.noise_level_db ?? 0), 0) / displayMetrics.length : 0;
    const avgSnr = hasNoise ? displayMetrics.reduce((sum, m) => sum + (m.snr_db ?? 0), 0) / displayMetrics.length : 0;
    return {
      delay: syntheticSeries(avgDelay, 30, 0.07),
      power: syntheticSeries(avgPower, 30, 0.03),
      range: syntheticSeries(avgRange, 30, 0.05),
      noise: hasNoise ? syntheticSeries(avgNoise, 30, 0.02) : null,
      snr: hasNoise ? syntheticSeries(avgSnr, 30, 0.04) : null,
    };
  }, [displayMetrics]);

  const appendRunLog = useCallback((msg: string, success = false) => {
    pushRunLog({ msg, level: detectLevel(msg, success) });
  }, [pushRunLog]);

  const refreshLatestResults = useCallback(() => {
    fetchResults(activeScenario)
      .then(({ file, rows }) => {
        setLiveMetrics(rows.length > 0 ? rows : null);
        setResultFile(file);
        setResultError(null);
      })
      .catch((error: Error) => setResultError(error.message));
  }, [activeScenario]);

  const refreshResultArchives = useCallback(() => {
    fetchExperimentArchives()
      .then((rows) => setResultArchives(rows))
      .catch((error: Error) => setResultError(error.message));
  }, []);

  const refreshLatestEvents = useCallback(() => {
    fetchEvents(activeScenario)
      .then(({ file, rows }) => {
        setEventFile(file);
        setTraceEvents(rows);
      })
      .catch(() => {
        setEventFile(null);
        setTraceEvents([]);
      });
  }, [activeScenario]);

  useEffect(() => {
    refreshLatestResults();
    refreshLatestEvents();
  }, [dataset.metrics, refreshLatestEvents, refreshLatestResults]);

  useEffect(() => {
    if (!resultsOpen) {
      return;
    }
    refreshResultArchives();
  }, [activeScenario, refreshResultArchives, resultsOpen]);

  useEffect(() => {
    if (!resultsOpen || selectedResultSource === LATEST_RESULTS_SOURCE) {
      setSelectedArchiveDetail(null);
      setArchiveLoading(false);
      return;
    }

    setArchiveLoading(true);
    fetchExperimentArchiveDetail(selectedResultSource)
      .then((detail) => {
        setSelectedArchiveDetail(detail);
        setResultError(null);
      })
      .catch((error: Error) => {
        setSelectedArchiveDetail(null);
        setResultError(error.message);
      })
      .finally(() => setArchiveLoading(false));
  }, [resultsOpen, selectedResultSource]);

  useEffect(() => {
    if (selectedResultSource === LATEST_RESULTS_SOURCE) {
      return;
    }
    if (!scenarioArchives.some((archive) => archive.id === selectedResultSource)) {
      setSelectedResultSource(LATEST_RESULTS_SOURCE);
      setSelectedArchiveDetail(null);
    }
  }, [scenarioArchives, selectedResultSource]);

  useEffect(() => {
    setCommunicationEvents(usingTraceEvents ? traceCommunicationEvents : derivedMetricEvents);
  }, [derivedMetricEvents, setCommunicationEvents, traceCommunicationEvents, usingTraceEvents]);

  useEffect(() => {
    if (eventLayerFilter !== 'all' && !communicationEvents.some((event) => event.layer === eventLayerFilter)) {
      setEventLayerFilter('all');
    }
    if (eventCodeFilter !== 'all' && !communicationEvents.some((event) => event.eventCode === eventCodeFilter)) {
      setEventCodeFilter('all');
    }
  }, [communicationEvents, eventCodeFilter, eventLayerFilter]);

  const availableEventLayers = useMemo(
    () => Array.from(new Set(communicationEvents.map((event) => event.layer).filter(Boolean))).sort(),
    [communicationEvents],
  );
  const availableEventCodes = useMemo(
    () => Array.from(new Set(communicationEvents.map((event) => event.eventCode).filter(Boolean))).sort(),
    [communicationEvents],
  );

  useEffect(() => {
    if (playbackFrames.length === 0) {
      setPlaybackCursorSeconds(null);
      return;
    }
    if (playbackTempoMode === 'real' && playbackPlaying) {
      return;
    }
    const frameTime = playbackFrames[storedPlaybackFrameIndex]?.time_s;
    setPlaybackCursorSeconds(
      typeof frameTime === 'number' ? frameTime : playbackTimelineStartSeconds,
    );
  }, [
    playbackFrames,
    playbackPlaying,
    playbackTempoMode,
    playbackTimelineStartSeconds,
    storedPlaybackFrameIndex,
  ]);

  useEffect(() => {
    if (activePlaybackFrameIndex !== storedPlaybackFrameIndex) {
      setPlaybackFrameIndex(activePlaybackFrameIndex);
    }
  }, [activePlaybackFrameIndex, setPlaybackFrameIndex, storedPlaybackFrameIndex]);

  useEffect(() => {
    if (!playbackPlaying || !hasPlaybackTimeline || playbackTempoMode !== 'real') {
      return undefined;
    }

    const startCursor = clampPlaybackTime(
      playbackCursorRef.current
        ?? playbackFrames[storedPlaybackFrameIndex]?.time_s
        ?? playbackTimelineStartSeconds,
      playbackTimelineStartSeconds,
      playbackTimelineEndSeconds,
    );
    if (startCursor >= playbackTimelineEndSeconds - 1e-9) {
      setPlaybackFrameIndex(Math.max(0, playbackFrames.length - 1));
      setPlaybackPlaying(false);
      return undefined;
    }

    let rafId = 0;
    let cursor = startCursor;
    let previousStamp: number | null = null;
    const speed = Math.max(playbackSpeed, 0.5);

    const tick = (stamp: number) => {
      if (previousStamp === null) {
        previousStamp = stamp;
        setPlaybackCursorSeconds(cursor);
        rafId = window.requestAnimationFrame(tick);
        return;
      }

      const deltaSeconds = Math.max(0, (stamp - previousStamp) / 1000);
      previousStamp = stamp;
      cursor = clampPlaybackTime(
        cursor + deltaSeconds * speed,
        playbackTimelineStartSeconds,
        playbackTimelineEndSeconds,
      );
      setPlaybackCursorSeconds(cursor);

      if (cursor >= playbackTimelineEndSeconds - 1e-6) {
        setPlaybackFrameIndex(Math.max(0, playbackFrames.length - 1));
        setPlaybackPlaying(false);
        return;
      }

      rafId = window.requestAnimationFrame(tick);
    };

    rafId = window.requestAnimationFrame(tick);
    return () => window.cancelAnimationFrame(rafId);
  }, [
    hasPlaybackTimeline,
    playbackFrames,
    playbackPlaying,
    playbackSpeed,
    playbackTempoMode,
    playbackTimelineEndSeconds,
    playbackTimelineStartSeconds,
    setPlaybackFrameIndex,
    setPlaybackPlaying,
    storedPlaybackFrameIndex,
  ]);

  useEffect(() => {
    if (!playbackPlaying || !hasPlaybackTimeline) {
      return undefined;
    }
    if (playbackTempoMode === 'real') {
      return undefined;
    }
    if (storedPlaybackFrameIndex >= playbackFrames.length - 1) {
      setPlaybackPlaying(false);
      return undefined;
    }
    const timeoutMs = Math.max(60, Math.round(currentFrameVisibleSeconds * 1000));
    const timerId = window.setTimeout(() => {
      setPlaybackFrameIndex((prev) => Math.min(prev + 1, playbackFrames.length - 1));
    }, timeoutMs);
    return () => window.clearTimeout(timerId);
  }, [
    currentFrameVisibleSeconds,
    hasPlaybackTimeline,
    playbackFrames.length,
    playbackPlaying,
    playbackTempoMode,
    setPlaybackPlaying,
    setPlaybackFrameIndex,
    storedPlaybackFrameIndex,
  ]);

  useEffect(() => {
    if (runPhase !== 'running' || runStartTs === null) return;
    const timerId = setInterval(() => setRunElapsed(formatElapsed(runStartTs)), 500);
    return () => clearInterval(timerId);
  }, [runPhase, runStartTs]);

  useEffect(() => {
    logEndRef.current?.scrollIntoView({ behavior: 'smooth' });
  }, [runLogs]);

  function handleUpdateScenarioMeta(field: keyof DemoScenario['scenario_metadata'], value: string) {
    setWorkingScenario((prev) => ({
      ...prev,
      scenario_metadata: { ...prev.scenario_metadata, [field]: value },
    }));
  }

  function handleUpdateSimulation(field: 'scheduler' | 'duration' | 'seed', value: string | number) {
    setWorkingScenario((prev) => ({
      ...prev,
      simulation: { ...prev.simulation, [field]: typeof value === 'string' ? value : Number(value) },
    }));
  }

  function handleUpdateMeasurement(field: 'engine_name' | 'noise_std' | 'dr_noise_std', value: string | number) {
    setWorkingScenario((prev) => ({
      ...prev,
      measurement: {
        ...prev.measurement,
        [field]: field === 'engine_name' ? String(value) : Number(value),
      },
    }));
  }

  function handlePlaybackSpeed(value: number) {
    setWorkingScenario((prev) => ({
      ...prev,
      ui: { ...prev.ui, playback_speed: value },
    }));
    setPlaybackSpeed(value);
  }

  function handlePlaybackToggle() {
    if (!hasPlaybackTimeline) {
      return;
    }
    const reachedEnd = playbackTempoMode === 'real'
      ? (typeof activePlaybackTimeSeconds === 'number'
        ? activePlaybackTimeSeconds >= playbackTimelineEndSeconds - 1e-6
        : false)
      : activePlaybackFrameIndex >= playbackFrames.length - 1;
    if (reachedEnd) {
      setPlaybackFrameIndex(0);
      setPlaybackCursorSeconds(playbackTimelineStartSeconds);
      setPlaybackPlaying(true);
      return;
    }
    setPlaybackPlaying((prev) => !prev);
  }

  function handlePlaybackRestart() {
    setPlaybackFrameIndex(0);
    setPlaybackCursorSeconds(playbackTimelineStartSeconds);
    setPlaybackPlaying(hasPlaybackTimeline);
  }

  function handlePlaybackStep() {
    if (!hasPlaybackTimeline || playbackFrames.length <= 1) {
      return;
    }

    setPlaybackPlaying(false);
    const nextIndex = Math.min(activePlaybackFrameIndex + 1, playbackFrames.length - 1);
    const nextTime = playbackFrames[nextIndex]?.time_s;
    setPlaybackFrameIndex(nextIndex);
    if (typeof nextTime === 'number') {
      setPlaybackCursorSeconds(nextTime);
    }
  }

  function handlePlaybackSeek(value: number) {
    if (playbackTempoMode === 'real' && hasPlaybackTimeline) {
      const nextTime = clampPlaybackTime(value, playbackTimelineStartSeconds, playbackTimelineEndSeconds);
      setPlaybackCursorSeconds(nextTime);
      setPlaybackFrameIndex(findPlaybackFrameIndexByTime(playbackFrames, nextTime));
      setPlaybackPlaying(false);
      return;
    }
    setPlaybackFrameIndex(Math.round(value));
    setPlaybackPlaying(false);
  }

  function handleUpdateNode(nodeId: number, updates: Partial<DemoNode>) {
    const currentNode = workingScenario.nodes.find((node) => node.id === nodeId);
    if (!currentNode) return;

    const nextUpdates = { ...updates };
    if (nextUpdates.position) {
      nextUpdates.position = clampNodePositionToEnvironment(nextUpdates.position, studioEnvironmentBounds);
    }

    const nextRole = nextUpdates.role ?? currentNode.role;
    const nextScenario = cloneScenario(workingScenario);
    nextScenario.nodes = nextScenario.nodes.map((node) => {
      if (node.id === nodeId) {
        return normalizeNodeForRole({ ...node, ...nextUpdates }, nextRole);
      }
      if (nextUpdates.role === 'sink' && nextScenario.topology.deployment_type === 'star' && node.id !== nodeId && node.role === 'sink') {
        return normalizeNodeForRole({ ...node, role: 'sensor' }, 'sensor');
      }
      return node;
    });
    if (nextUpdates.role === 'sink' && nextScenario.topology.deployment_type === 'star') {
      nextScenario.topology.center = nodeId;
    }
    if (nextScenario.topology.deployment_type === 'p2p') {
      nextScenario.topology.pairs = nextScenario.topology.logical_type === 'full_mesh' ? buildFullMeshPairs(nextScenario.nodes) : buildP2PPairs(nextScenario.nodes);
    }
    setWorkingScenario(nextScenario);
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
    if (nextUpdates.role) {
      setNodeBindings((prev) => {
        const next = { ...prev };
        next[nodeId] = sanitizeNodeBindingForRole(nextRole, {
          ...(prev[nodeId] ?? { nodeId, overridesByAssetId: {} }),
          nodeId,
          nodeTemplateId: nextUpdates.role ? (DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[nextRole] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor) : prev[nodeId]?.nodeTemplateId,
          overridesByAssetId: prev[nodeId]?.overridesByAssetId ?? {},
        });
        if (nextUpdates.role === 'sink' && nextScenario.topology.deployment_type === 'star') {
          nextScenario.nodes.forEach((node) => {
            if (node.id !== nodeId) {
              next[node.id] = sanitizeNodeBindingForRole(node.role, {
                ...(prev[node.id] ?? { nodeId: node.id, overridesByAssetId: {} }),
                nodeId: node.id,
                nodeTemplateId: DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[node.role] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor,
                overridesByAssetId: prev[node.id]?.overridesByAssetId ?? {},
              });
            }
          });
        }
        return next;
      });
    }
  }

  function handleUpdateNodeApp(nodeId: number, field: string, value: string | number) {
    const nextScenario = cloneScenario(workingScenario);
    nextScenario.nodes = nextScenario.nodes.map((node) => {
      if (node.id !== nodeId) return node;
      const currentApp = node.application ?? { type: 'periodic_report', period_seconds: 10, packet_size: 512 };
      const nextApp = { ...currentApp, [field]: value };
      if (nextApp.type === 'sink_aggregator') {
        delete nextApp.period_seconds;
        delete nextApp.packet_size;
      }
      return { ...node, application: nextApp };
    });
    setWorkingScenario(nextScenario);
  }

  function handleUpdateNodeBinding(nodeId: number, updates: Partial<StudioNodeBinding>) {
    const role = workingScenario.nodes.find((node) => node.id === nodeId)?.role ?? 'sensor';
    setNodeBindings((prev) => {
      const current = prev[nodeId] ?? { nodeId, overridesByAssetId: {} };
      const next = sanitizeNodeBindingForRole(role, {
        ...current,
        ...updates,
        nodeId,
        overridesByAssetId: current.overridesByAssetId ?? {},
      });
      return { ...prev, [nodeId]: next };
    });
  }

  function handleUpdateEdgeBinding(edgeKey: string, updates: Partial<StudioEdgeBinding>) {
    setEdgeBindings((prev) => {
      const current = prev[edgeKey] ?? createDefaultEdgeBinding(workingScenario, edgeKey);
      return {
        ...prev,
        [edgeKey]: { ...current, ...updates, edgeKey, overridesByAssetId: current.overridesByAssetId ?? {} },
      };
    });
  }

  function handleSetAllEdgeLinkProfile(assetId: string) {
    setEdgeBindings((prev) => {
      const next: Record<string, StudioEdgeBinding> = {};
      for (const [edgeKey, binding] of Object.entries(prev)) {
        next[edgeKey] = { ...binding, linkProfileAssetId: assetId };
      }
      return next;
    });
    // 立即同步 workingScenario.transmission.type，使 UI（如 BellhopParamsPanel）即时响应
    const nextType = assetId === 'link-bellhop-edge' ? 'bellhop'
      : assetId === 'link-ssp-ray-edge' ? 'ssp_ray'
      : assetId === 'link-simple-edge' ? 'simple'
      : undefined;
    if (nextType) {
      setWorkingScenario((prev) => ({
        ...prev,
        transmission: { ...prev.transmission, type: nextType },
      }));
    }
  }

  function handleUpdateNoiseComposition(asset: ModelLibraryAsset) {
    if (!asset.defaults) return;
    const entry = asset.defaults as Record<string, unknown>;
    setWorkingScenario((prev) => ({
      ...prev,
      noise: { composition: [entry as DemoScenario['noise']['composition'][0]] },
    }));
  }

  function handleUpdateNoiseParam(field: string, value: number) {
    setWorkingScenario((prev) => {
      const comp = prev.noise.composition[0];
      if (!comp) return prev;
      return {
        ...prev,
        noise: { composition: [{ ...comp, [field]: value }] },
      };
    });
  }

  const handleUpdateTransmissionParam = useCallback((field: string, value: string) => {
    setWorkingScenario((prev) => {
      if (String(prev.transmission.params[field] ?? '') === value) {
        return prev;
      }

      return {
        ...prev,
        transmission: {
          ...prev.transmission,
          params: { ...prev.transmission.params, [field]: value },
        },
      };
    });
  }, []);

  const handleSelectEnvironmentDatabase = useCallback((databaseId: string) => {
    setWorkingScenario((prev) => {
      const nextId = String(databaseId ?? '');
      const currentId = String(prev.transmission.params.environment_database_id ?? '');
      const nextParams = { ...prev.transmission.params };
      if (!nextId) {
        if (!currentId) {
          return prev;
        }
        delete nextParams.environment_database_id;
        return {
          ...prev,
          transmission: {
            ...prev.transmission,
            params: nextParams,
          },
        };
      }

      if (currentId === nextId && prev.transmission.type === 'bellhop') {
        return prev;
      }

      const database = environmentDatabases.find((item) => item.id === nextId);
      if (!database) {
        return prev;
      }

      return {
        ...prev,
        transmission: {
          ...prev.transmission,
          type: 'bellhop',
          params: {
            ...stripExpandedEnvironmentParams(nextParams),
            environment_database_id: database.id,
          },
        },
      };
    });
  }, [environmentDatabases]);

  const handleRefreshDataFiles = useCallback(() => {
    fetchDataFiles('grid').then(setGridFiles).catch(() => {});
    fetchDataFiles('bathymetry').then(setBathymetryFiles).catch(() => {});
    fetchEnvironmentDatabases().then(setEnvironmentDatabases).catch(() => {});
  }, []);

  function handleMoveNode(nodeId: number, position: [number, number, number]) {
    handleUpdateNode(nodeId, { position: clampNodePositionToEnvironment(position, studioEnvironmentBounds) });
  }

  function instantiateNodeFromTemplate(templateId: string) {
    const centerNode = workingScenario.nodes.find((node) => node.id === (workingScenario.topology.center ?? workingScenario.nodes[0]?.id)) ?? workingScenario.nodes[0];
    const nextId = Math.max(...workingScenario.nodes.map((node) => node.id)) + 1;
    const angle = (workingScenario.nodes.length * Math.PI * 2) / Math.max(6, workingScenario.nodes.length + 1);
    const radius = 900;
    const position = clampNodePositionToEnvironment([
      centerNode.position[0] + Math.cos(angle) * radius,
      centerNode.position[1] + Math.sin(angle) * radius,
      centerNode.position[2],
    ], studioEnvironmentBounds);
    const template = nodeTemplates.find((item) => item.id === templateId) ?? nodeTemplates[0] ?? buildFallbackNodeTemplates()[0];
    const { node: newNode, binding: newBinding } = buildNodeFromTemplate(template, nextId, position);
    const nextNodes = newNode.role === 'sink' && workingScenario.topology.deployment_type === 'star'
      ? [...workingScenario.nodes.map((node) => (node.role === 'sink' ? normalizeNodeForRole({ ...node, role: 'sensor' }, 'sensor') : node)), newNode]
      : [...workingScenario.nodes, newNode];
    const nextScenario: DemoScenario = {
      ...workingScenario,
      nodes: nextNodes,
      topology: workingScenario.topology.deployment_type === 'p2p'
        ? { ...workingScenario.topology, pairs: workingScenario.topology.logical_type === 'full_mesh' ? buildFullMeshPairs(nextNodes) : buildP2PPairs(nextNodes) }
        : { ...workingScenario.topology, center: newNode.role === 'sink' ? newNode.id : workingScenario.topology.center },
    };
    setWorkingScenario(nextScenario);
    setNodeBindings((prev) => {
      const next = { ...prev, [nextId]: newBinding };
      if (newNode.role === 'sink' && workingScenario.topology.deployment_type === 'star') {
        workingScenario.nodes.forEach((node) => {
          if (node.role === 'sink') {
            next[node.id] = sanitizeNodeBindingForRole('sensor', {
              ...(prev[node.id] ?? { nodeId: node.id, overridesByAssetId: {} }),
              nodeId: node.id,
              nodeTemplateId: DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor,
              overridesByAssetId: prev[node.id]?.overridesByAssetId ?? {},
            });
          }
        });
      }
      return next;
    });
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
    setSelection({ scope: 'node', nodeId: nextId });
  }

  function handleAddNode() {
    const templateId = nodeTemplates.some((item) => item.id === defaultNodeTemplateId)
      ? defaultNodeTemplateId
      : nodeTemplates[0]?.id ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor;
    setDefaultNodeTemplateId(templateId);
    setNodeTemplatePickerOpen(true);
  }

  function handleConfirmAddNodeFromTemplate() {
    if (!defaultNodeTemplateId) return;
    instantiateNodeFromTemplate(defaultNodeTemplateId);
    setNodeTemplatePickerOpen(false);
  }

  function handleApplyNodeTemplate(nodeId: number, templateId: string) {
    const template = nodeTemplates.find((item) => item.id === templateId);
    const currentNode = workingScenario.nodes.find((item) => item.id === nodeId);
    if (!template || !currentNode) return;

    const { node: templatedNode, binding: templatedBinding } = buildNodeFromTemplate(template, nodeId, currentNode.position);
    const nextRole = templatedNode.role;
    const nextScenario = cloneScenario(workingScenario);
    nextScenario.nodes = nextScenario.nodes.map((node) => {
      if (node.id === nodeId) {
        return templatedNode;
      }
      if (nextRole === 'sink' && nextScenario.topology.deployment_type === 'star' && node.id !== nodeId && node.role === 'sink') {
        return normalizeNodeForRole({ ...node, role: 'sensor' }, 'sensor');
      }
      return node;
    });
    if (nextRole === 'sink' && nextScenario.topology.deployment_type === 'star') {
      nextScenario.topology.center = nodeId;
    }
    if (nextScenario.topology.deployment_type === 'p2p') {
      nextScenario.topology.pairs = nextScenario.topology.logical_type === 'full_mesh' ? buildFullMeshPairs(nextScenario.nodes) : buildP2PPairs(nextScenario.nodes);
    }

    setWorkingScenario(nextScenario);
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
    setNodeBindings((prev) => {
      const next = { ...prev, [nodeId]: templatedBinding };
      if (nextRole === 'sink' && nextScenario.topology.deployment_type === 'star') {
        nextScenario.nodes.forEach((node) => {
          if (node.id !== nodeId) {
            next[node.id] = sanitizeNodeBindingForRole(node.role, {
              ...(prev[node.id] ?? { nodeId: node.id, overridesByAssetId: {} }),
              nodeId: node.id,
              nodeTemplateId: DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[node.role] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor,
              overridesByAssetId: prev[node.id]?.overridesByAssetId ?? {},
            });
          }
        });
      }
      return next;
    });
    setSaveNote(`已应用节点模板：${template.name}`);
  }

  function handleSaveNodeAsTemplate(nodeId: number) {
    const node = workingScenario.nodes.find((item) => item.id === nodeId);
    const binding = nodeBindings[nodeId] ?? { nodeId, overridesByAssetId: {} };
    if (!node) return;

    const currentTemplate = nodeTemplates.find((item) => item.id === binding.nodeTemplateId);
    setNodeTemplateSaveDialog({
      nodeId,
      name: currentTemplate ? `${currentTemplate.name} 副本` : `${node.role}-${node.id}`,
      description: currentTemplate?.description ?? `${node.role} 节点的当前装配快照`,
      saving: false,
      error: null,
    });
  }

  async function handleConfirmSaveNodeTemplate() {
    if (!nodeTemplateSaveDialog) return;

    const node = workingScenario.nodes.find((item) => item.id === nodeTemplateSaveDialog.nodeId);
    const binding = nodeBindings[nodeTemplateSaveDialog.nodeId] ?? { nodeId: nodeTemplateSaveDialog.nodeId, overridesByAssetId: {} };
    if (!node) return;

    setNodeTemplateSaveDialog((prev) => (prev ? { ...prev, saving: true, error: null } : prev));
    try {
      const saved = await createNodeTemplate(buildTemplatePayloadFromNode(nodeTemplateSaveDialog.name.trim(), nodeTemplateSaveDialog.description.trim(), node, binding));
      setNodeTemplates((prev) => [...prev, saved]);
      setDefaultNodeTemplateId(saved.id);
      setNodeBindings((prev) => ({
        ...prev,
        [node.id]: { ...(prev[node.id] ?? binding), nodeId: node.id, nodeTemplateId: saved.id, overridesByAssetId: prev[node.id]?.overridesByAssetId ?? binding.overridesByAssetId ?? {} },
      }));
      setSaveNote(`已保存节点模板：${saved.name}`);
      setNodeTemplateSaveDialog(null);
    } catch (error) {
      setNodeTemplateSaveDialog((prev) => (prev ? { ...prev, saving: false, error: error instanceof Error ? error.message : '保存节点模板失败' } : prev));
    }
  }

  async function handleSaveTemplateLibraryDraft() {
    if (!templateLibraryDraft || templateLibraryDraft.builtIn) return;
    setTemplateLibraryDraft((prev) => (prev ? { ...prev, saving: true, error: null } : prev));
    try {
      const updated = await updateNodeTemplate(templateLibraryDraft.id, {
        name: templateLibraryDraft.name.trim(),
        description: templateLibraryDraft.description.trim(),
      });
      setNodeTemplates((prev) => prev.map((template) => (template.id === updated.id ? updated : template)));
      setTemplateLibraryDraft({
        id: updated.id,
        name: updated.name,
        description: updated.description ?? '',
        builtIn: Boolean(updated.builtIn),
        saving: false,
        error: null,
      });
      setSaveNote(`已更新模板：${updated.name}`);
    } catch (error) {
      setTemplateLibraryDraft((prev) => (prev ? { ...prev, saving: false, error: error instanceof Error ? error.message : '更新模板失败' } : prev));
    }
  }

  async function handleDeleteTemplateFromLibrary() {
    if (!templateLibraryDraft || templateLibraryDraft.builtIn) return;

    if (templateDeleteArmId !== templateLibraryDraft.id) {
      setTemplateDeleteArmId(templateLibraryDraft.id);
      return;
    }

    try {
      await deleteNodeTemplate(templateLibraryDraft.id);
      const deletedId = templateLibraryDraft.id;
      const nextTemplates = nodeTemplates.filter((template) => template.id !== deletedId);
      const fallbackTemplate = nextTemplates.find((template) => template.id === defaultNodeTemplateId) ?? nextTemplates[0] ?? null;
      setNodeTemplates(nextTemplates);
      if (!nextTemplates.some((template) => template.id === defaultNodeTemplateId) && fallbackTemplate) {
        setDefaultNodeTemplateId(fallbackTemplate.id);
      }
      setNodeBindings((prev) => Object.fromEntries(Object.entries(prev).map(([key, binding]) => {
        if (binding.nodeTemplateId !== deletedId) {
          return [Number(key), binding];
        }
        const nodeId = Number(key);
        const role = workingScenario.nodes.find((node) => node.id === nodeId)?.role ?? 'sensor';
        return [nodeId, { ...binding, nodeTemplateId: DEFAULT_NODE_TEMPLATE_ID_BY_ROLE[role] ?? DEFAULT_NODE_TEMPLATE_ID_BY_ROLE.sensor }];
      })));
      setTemplateDeleteArmId(null);
      if (fallbackTemplate) {
        setTemplateLibrarySelectionId(fallbackTemplate.id);
        setTemplateLibraryDraft({
          id: fallbackTemplate.id,
          name: fallbackTemplate.name,
          description: fallbackTemplate.description ?? '',
          builtIn: Boolean(fallbackTemplate.builtIn),
          saving: false,
          error: null,
        });
      } else {
        setTemplateLibraryOpen(false);
        setTemplateLibraryDraft(null);
      }
      setSaveNote('已删除自定义模板');
    } catch (error) {
      setTemplateLibraryDraft((prev) => (prev ? { ...prev, error: error instanceof Error ? error.message : '删除模板失败' } : prev));
    }
  }

  function removeNodeById(nodeId: number) {
    const remainingNodes = workingScenario.nodes.filter((node) => node.id !== nodeId);
    if (remainingNodes.length === 0) return;
    const removedWasCenter = workingScenario.topology.center === nodeId;
    const nextNodes = removedWasCenter
      ? remainingNodes.map((node, index) => (index === 0 ? normalizeNodeForRole({ ...node, role: 'sink' }, 'sink') : node))
      : remainingNodes;
    const nextScenario: DemoScenario = {
      ...workingScenario,
      nodes: nextNodes,
      topology: workingScenario.topology.deployment_type === 'p2p'
        ? { ...workingScenario.topology, pairs: workingScenario.topology.logical_type === 'full_mesh' ? buildFullMeshPairs(nextNodes) : buildP2PPairs(nextNodes), center: undefined }
        : { ...workingScenario.topology, center: nextNodes.find((node) => node.role === 'sink')?.id ?? nextNodes[0]?.id },
    };
    setWorkingScenario(nextScenario);
    setNodeBindings((prev) => {
      const next = { ...prev };
      delete next[nodeId];
      return next;
    });
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
    setSelection({ scope: 'scene' });
  }

  function handleRemoveSelectedNode() {
    if (selection.scope !== 'node') return;
    removeNodeById(selection.nodeId);
  }

  function handleSwitchTopology(kind: 'star' | 'p2p' | 'full_mesh') {
    setSceneBinding((prev) => ({
      ...prev,
      topologyAssetId: kind === 'full_mesh' ? 'topology-full-mesh' : kind === 'p2p' ? 'topology-p2p' : 'topology-star',
      communicationTechniqueAssetId: kind === 'star' ? 'tech-broadcast' : 'tech-one-to-one',
    }));

    const nextNodes = cloneScenario(workingScenario.nodes);
    if (kind === 'p2p' || kind === 'full_mesh') {
      const sinkIds = nextNodes.filter((node) => node.role === 'sink').map((node) => node.id);
      if (kind === 'p2p' && sinkIds.length < 2) {
        nextNodes.forEach((node, index) => {
          if (index < 2) {
            node.role = 'sink';
            node.application = { type: 'sink_aggregator' };
          }
        });
      }
    } else {
      let leaderFound = false;
      nextNodes.forEach((node) => {
        if (node.role === 'sink' && !leaderFound) {
          leaderFound = true;
          return;
        }
        if (node.role === 'sink') {
          node.role = 'sensor';
          node.application = { type: 'periodic_report', period_seconds: 10, packet_size: 512 };
        }
      });
    }

    const nextScenario: DemoScenario = kind === 'full_mesh'
      ? {
          ...workingScenario,
          nodes: nextNodes,
          topology: {
            ...workingScenario.topology,
            deployment_type: 'p2p',
            logical_type: 'full_mesh',
            center: undefined,
            pairs: buildFullMeshPairs(nextNodes),
          },
        }
      : kind === 'p2p'
        ? {
            ...workingScenario,
            nodes: nextNodes,
            topology: {
              ...workingScenario.topology,
              deployment_type: 'p2p',
              logical_type: 'direct_pair',
              center: undefined,
              pairs: buildP2PPairs(nextNodes),
            },
          }
        : {
            ...workingScenario,
            nodes: nextNodes,
            topology: {
              ...workingScenario.topology,
              deployment_type: 'star',
              logical_type: 'center_sink',
              center: nextNodes.find((node) => node.role === 'sink')?.id ?? nextNodes[0]?.id,
              pairs: [],
            },
          };
    setWorkingScenario(nextScenario);
    setNodeBindings((prev) => Object.fromEntries(nextScenario.nodes.map((node) => {
      const current = prev[node.id] ?? { nodeId: node.id, overridesByAssetId: {} };
      return [node.id, sanitizeNodeBindingForRole(node.role, { ...current, nodeId: node.id, overridesByAssetId: current.overridesByAssetId ?? {} })];
    })));
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
  }

  async function persistCurrentScenario(targetName = activeScenario, scenarioOverride?: DemoScenario) {
    const sourceScenario = {
      ...(scenarioOverride ?? workingScenario),
      ui: { ...(scenarioOverride ?? workingScenario).ui, playback_speed: playbackSpeed },
    };
    const updated = applyEnvironmentDatabaseToScenario(
      buildScenarioFromBindings(sourceScenario, sceneBinding, nodeBindings, edgeBindings),
      environmentDatabases,
    );
    await saveScenario(targetName, updated);
    setWorkingScenario(cloneScenario(updated));
    setCommittedSnapshot(serializeStudioState(updated, sceneBinding, nodeBindings, edgeBindings));
    await loadScenario(targetName);
    return updated;
  }

  async function handleSaveStudio() {
    setSaving(true);
    setSaveError(null);
    setSaveNote(null);
    setPlaybackPlaying(false);
    setPlaybackFrameIndex(0);
    setPlaybackCursorSeconds(null);
    try {
      await persistCurrentScenario();
      setSaveNote('模型编排与场景主配置已保存，运行区将使用当前版本。');
    } catch (error) {
      setSaveError(error instanceof Error ? error.message : '保存失败，请检查后端连接。');
    } finally {
      setSaving(false);
    }
  }

  function handleResetStudio() {
    const snapshot = JSON.parse(committedSnapshot) as { scenario: DemoScenario; bindings: StudioBindingsSnapshot };
    const normalized = normalizeBindingsSnapshot(snapshot.bindings);
    setWorkingScenario(cloneScenario(snapshot.scenario));
    setNodeBindings(normalized.nodes);
    setEdgeBindings(normalized.edges);
    setSceneBinding(normalized.scene);
    setSaveError(null);
    setSaveNote('已恢复到最近一次提交的编排状态。');
  }

  async function handleSaveAsDerived() {
    const name = deriveName.trim();
    if (!name) {
      setSaveError('请先输入派生场景名称。');
      return;
    }
    setSaving(true);
    setSaveError(null);
    setSaveNote(null);
    try {
      await deriveScenario(activeScenario, name);
      const updatedScenario: DemoScenario = {
        ...workingScenario,
        scenario_metadata: { ...workingScenario.scenario_metadata, scenario_id: name, name },
        output: { ...workingScenario.output, stats_file: `results/${name}_summary.csv` },
      };
      await persistCurrentScenario(name, updatedScenario);
      setSaveNote(`已派生并切换到场景 ${name}。`);
    } catch (error) {
      setSaveError(error instanceof Error ? error.message : '派生保存失败，请检查名称或后端状态。');
    } finally {
      setSaving(false);
    }
  }

  async function handleWizardCreated(name: string) {
    await Promise.all([
      fetchDataFiles('grid').then(setGridFiles).catch(() => {}),
      fetchDataFiles('bathymetry').then(setBathymetryFiles).catch(() => {}),
    ]);
    await loadScenario(name);
    setSelection({ scope: 'scene' });
    setWizardOpen(false);
    setSaveError(null);
    setSaveNote(`已通过向导生成并载入场景 ${name}。`);
  }

  async function handleStartRun() {
    abortRef.current?.abort();
    setSaveError(null);
    setLogsOpen(true);
    setPlaybackPlaying(false);
    setPlaybackFrameIndex(0);

    try {
      if (isDirty) {
        appendRunLog('[INFO]    检测到未保存更改，正在自动保存当前场景...');
        setSaving(true);
        await persistCurrentScenario();
        setSaving(false);
      }
    } catch (error) {
      setSaving(false);
      const message = error instanceof Error ? error.message : '自动保存失败，已中止运行。';
      setRunPhase('error');
      appendRunLog(`[ERROR]   ${message}`);
      setSaveError(message);
      return;
    }

    const scenarioName = activeScenario;
    const startedAt = Date.now();
    setRunStartTs(startedAt);
    setRunElapsed('0 s');
    setRunPhase('running');
    clearRunLogs();
    appendRunLog(`[INFO]    启动仿真场景: ${scenarioName}`);

    abortRef.current = startRun(
      scenarioName,
      (event: SseEvent) => {
        switch (event.type) {
          case 'start':
            appendRunLog(`[INFO]    连接后端成功，场景: ${event.scenario}`);
            break;
          case 'log':
            appendRunLog(event.message);
            break;
          case 'done':
            setRunPhase('done');
            setLiveMetrics(event.results);
            setTraceEvents(event.events ?? []);
            setEventFile(null);
            setPlaybackFrameIndex(0);
            setPlaybackPlaying(
              (event.events?.some((item) => typeof item.time_s === 'number') ?? false)
              || event.results.some((metric) => typeof metric.time_s === 'number'),
            );
            notifyRunDone(event.results);
            void refreshLatestResults();
            void refreshLatestEvents();
            void refreshResultArchives();
            void fetchRays(scenarioName).then((r) => setRays(r));
            if (selectedResultSource !== LATEST_RESULTS_SOURCE && event.archive_id) {
              setSelectedResultSource(event.archive_id);
            }
            appendRunLog('[SUCCESS] 仿真完成，结果已归档。', true);
            break;
          case 'error':
            setRunPhase('error');
            appendRunLog(event.message ? `[ERROR]   ${event.message}` : `[ERROR]   仿真退出，exit_code=${event.exit_code}`);
            break;
          case 'cancelled':
            setRunPhase('idle');
            appendRunLog('[WARN]    仿真已取消。');
            break;
        }
      },
      () => {
        if (useStudioRuntimeStore.getState().runPhase === 'running') {
          setRunPhase('error');
        }
      },
    );
  }

  function handleStopRun() {
    abortRef.current?.abort();
    abortRef.current = null;
    setRunPhase('idle');
    setPlaybackPlaying(false);
    setPlaybackFrameIndex(0);
    setPlaybackCursorSeconds(null);
    setLiveMetrics(dataset.metrics.length > 0 ? dataset.metrics : null);
    setSelection({ scope: 'scene' });
    appendRunLog('[INFO]    已终止回放，恢复编辑模式。');
  }

  function handleExportResults() {
    if (displayMetrics.length === 0) return;
    const fieldOrder = ['time_s', 'tx_id', 'rx_id', 'delay_s', 'receive_power_db', 'first_arrival_delay_s', 'received_level_db', 'pseudo_range_m', 'multipath_count', 'tx_x', 'tx_y', 'tx_z', 'rx_x', 'rx_y', 'rx_z', 'noise_level_db', 'snr_db'];
    const exportFields = fieldOrder.filter((field) => displayMetrics.some((metric) => metric[field as keyof LinkMetric] !== undefined));
    const header = exportFields.join(',');
    const rows = displayMetrics.map(
      (metric) => exportFields
        .map((field) => String(metric[field as keyof LinkMetric] ?? ''))
        .join(','),
    );
    const blob = new Blob([[header, ...rows].join('\n')], { type: 'text/csv' });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = displayResultFile?.split('/').pop() ?? `${workingScenario.scenario_metadata.scenario_id}_summary.csv`;
    link.click();
    URL.revokeObjectURL(url);
  }

  function handleContextMenu(target: CanvasContextTarget) {
    if (target.type === 'node' && target.nodeId !== undefined) {
      setSelection({ scope: 'node', nodeId: target.nodeId });
    }
    setCtxMenu({ x: target.x, y: target.y, nodeId: target.nodeId });
  }

  function closeCtxMenu() {
    setCtxMenu(null);
  }

  function ctxCopyNode() {
    if (ctxMenu?.nodeId !== undefined) {
      const node = workingScenario.nodes.find((item) => item.id === ctxMenu.nodeId);
      if (node) setClipboardNode(cloneScenario(node));
    }
    closeCtxMenu();
  }

  function pasteNode(src: DemoNode) {
    const nextId = Math.max(...workingScenario.nodes.map((node) => node.id)) + 1;
    const newNode: DemoNode = {
      ...cloneScenario(src),
      id: nextId,
      role: src.role === 'sink' ? 'sensor' : src.role,
      position: clampNodePositionToEnvironment([src.position[0] + 200, src.position[1] + 200, src.position[2]], studioEnvironmentBounds),
    };
    const normalizedNode = normalizeNodeForRole(newNode, newNode.role);
    const nextScenario: DemoScenario = {
      ...workingScenario,
      nodes: [...workingScenario.nodes, normalizedNode],
      topology: workingScenario.topology.deployment_type === 'p2p'
        ? { ...workingScenario.topology, pairs: workingScenario.topology.logical_type === 'full_mesh' ? buildFullMeshPairs([...workingScenario.nodes, normalizedNode]) : buildP2PPairs([...workingScenario.nodes, normalizedNode]) }
        : workingScenario.topology,
    };
    setWorkingScenario(nextScenario);
    setNodeBindings((prev) => ({
      ...prev,
      [nextId]: sanitizeNodeBindingForRole(normalizedNode.role, { nodeId: nextId, nodeModelAssetId: 'node-sensor-v1', overridesByAssetId: {} }),
    }));
    setEdgeBindings((prev) => syncEdgeBindingsForScenario(nextScenario, prev));
    setSelection({ scope: 'node', nodeId: nextId });
  }

  function ctxPasteNode() {
    if (clipboardNode) pasteNode(clipboardNode);
    closeCtxMenu();
  }

  function ctxDeleteNode() {
    if (ctxMenu?.nodeId !== undefined) {
      removeNodeById(ctxMenu.nodeId);
    }
    closeCtxMenu();
  }

  function ctxSetAsSink() {
    if (ctxMenu?.nodeId !== undefined) handleUpdateNode(ctxMenu.nodeId, { role: 'sink' });
    closeCtxMenu();
  }

  useEffect(() => {
    function onKey(event: KeyboardEvent) {
      if (event.key === 'Escape') {
        setCtxMenu(null);
        setSelection({ scope: 'scene' });
        return;
      }
      if (event.ctrlKey && event.key === 'c' && selection.scope === 'node') {
        const node = workingScenario.nodes.find((item) => item.id === selection.nodeId);
        if (node) setClipboardNode(cloneScenario(node));
        return;
      }
      if (event.ctrlKey && event.key === 'v' && clipboardNode) {
        pasteNode(clipboardNode);
        return;
      }
      const tag = (event.target as HTMLElement).tagName;
      if (event.key === 'Delete' && tag !== 'INPUT' && tag !== 'TEXTAREA' && tag !== 'SELECT') {
        if (selection.scope === 'node') handleRemoveSelectedNode();
      }
    }
    document.addEventListener('keydown', onKey);
    return () => document.removeEventListener('keydown', onKey);
  }, [clipboardNode, selection, workingScenario]);

  useEffect(() => {
    if (!ctxMenu) return;
    function onClickOutside(event: MouseEvent) {
      if (ctxMenuRef.current && !ctxMenuRef.current.contains(event.target as Node)) {
        setCtxMenu(null);
      }
    }
    document.addEventListener('mousedown', onClickOutside);
    return () => document.removeEventListener('mousedown', onClickOutside);
  }, [ctxMenu]);

  const [saveMenuOpen, setSaveMenuOpen] = useState(false);
  const saveMenuRef = useRef<HTMLDivElement>(null);

  useEffect(() => {
    if (!saveMenuOpen) return;
    function onClickOutsideSave(event: MouseEvent) {
      if (saveMenuRef.current && !saveMenuRef.current.contains(event.target as Node)) {
        setSaveMenuOpen(false);
      }
    }
    document.addEventListener('mousedown', onClickOutsideSave);
    return () => document.removeEventListener('mousedown', onClickOutsideSave);
  }, [saveMenuOpen]);

  return (
    <div className="studio-page">
      {/* ── Row 1: Identity · View · Run · Save ── */}
      <div className="studio-topbar">
        <span style={{ fontWeight: 600, fontSize: 12, color: '#eff7ff', flexShrink: 0 }}>{displayTitle}</span>
        <span className={isDirty ? 'status-pill status-pill--warning' : 'status-pill status-pill--ready'} style={{ flexShrink: 0 }}>
          {isDirty ? '● 未保存' : '✓ 已同步'}
        </span>
        <span className="studio-topbar__divider" />
        <button type="button" className={viewMode === 'topology' ? 'primary-button' : 'ghost-button'} style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => setViewMode('topology')}>
          俯视拓扑
        </button>
        <button type="button" className={viewMode === 'profile' ? 'primary-button' : 'ghost-button'} style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => setViewMode('profile')}>
          纵剖面 + 声线
        </button>
        <button type="button" className={viewMode === 'scene3d' ? 'primary-button' : 'ghost-button'} style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => setViewMode('scene3d')}>
          3D 场景
        </button>
        <span className="studio-topbar__spacer" />
        <button type="button" className="primary-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} disabled={saving || runPhase === 'running'} onClick={() => void handleStartRun()}>
          {runPhase === 'running' ? '⏳ 运行中...' : '▶ 启动仿真'}
        </button>
        <button type="button" className="ghost-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => setLogsOpen(true)}>
          日志
        </button>
        <button type="button" className="ghost-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => setResultsOpen(true)}>
          结果
        </button>
        <button type="button" className="ghost-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} onClick={() => openTemplateLibrary()}>
          模板库
        </button>
        <span className="studio-topbar__divider" />
        <button type="button" className="ghost-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8, flexShrink: 0 }} disabled={!isDirty || saving} onClick={handleResetStudio}>
          恢复
        </button>
        <div style={{ position: 'relative', flexShrink: 0 }} ref={saveMenuRef}>
          <div style={{ display: 'flex', gap: 4 }}>
            <button type="button" className="primary-button" style={{ padding: '3px 10px', fontSize: 12, borderRadius: 8 }} disabled={saving} onClick={() => void handleSaveStudio()}>
              {saving ? '保存中...' : '保存当前'}
            </button>
            <button type="button" className="ghost-button" style={{ padding: '3px 8px', fontSize: 12, borderRadius: 8 }} disabled={saving} onClick={() => setSaveMenuOpen((prev) => !prev)}>
              更多 ▾
            </button>
          </div>
          {saveMenuOpen && (
            <div className="studio-topbar__dropdown">
              <button type="button" className="studio-topbar__dropdown-item" disabled={saving} onClick={() => { void handleSaveStudio(); setSaveMenuOpen(false); }}>
                💾 保存当前场景{!isDirty ? ' (已是最新)' : ''}
              </button>
              <button type="button" className="studio-topbar__dropdown-item" disabled={saving || runPhase === 'running'} onClick={() => { void handleStartRun(); setSaveMenuOpen(false); }}>
                ▶ 保存并运行
              </button>
              <div className="studio-topbar__dropdown-divider" />
              <div style={{ padding: '6px 10px' }}>
                <div style={{ fontSize: 11, color: 'var(--text-dim)', marginBottom: 4 }}>另存为派生场景</div>
                <input
                  value={deriveName}
                  onChange={(event) => setDeriveName(event.target.value)}
                  placeholder="派生场景名称"
                  style={{ width: '100%', border: '1px solid rgba(148,163,184,0.2)', borderRadius: 8, background: 'rgba(2,10,19,0.9)', color: '#fff', padding: '4px 8px', fontSize: 12, marginBottom: 4 }}
                />
                <button type="button" className="primary-button" style={{ width: '100%', padding: '4px 0', fontSize: 12, borderRadius: 8 }} disabled={saving} onClick={() => { void handleSaveAsDerived(); setSaveMenuOpen(false); }}>
                  另存派生
                </button>
              </div>
            </div>
          )}
        </div>
      </div>

      {/* ── Row 2: Playback controls ── */}
      <div className="studio-topbar studio-topbar--playback">
        <span className="studio-topbar__label">仿真</span>
        <input
          type="number"
          min="1"
          value={workingScenario.simulation.duration}
          onChange={(event) => handleUpdateSimulation('duration', Number(event.target.value) || 0)}
          style={{ width: 64, border: '1px solid rgba(148,163,184,0.2)', borderRadius: 8, background: 'rgba(2,10,19,0.9)', color: '#fff', padding: '2px 6px', fontSize: 11, flexShrink: 0 }}
        />
        <span className="studio-topbar__label">s</span>
        <span className="studio-topbar__divider" />
        <span className="studio-topbar__label">速度</span>
        <input
          type="range"
          min="0.5"
          max="4"
          step="0.5"
          value={playbackSpeed}
          onChange={(event) => handlePlaybackSpeed(Number(event.target.value))}
          style={{ width: 80, flexShrink: 0 }}
        />
        <span className="studio-topbar__label">{playbackSpeed}x</span>
        <span className="studio-topbar__divider" />
        <span className="studio-topbar__label">节奏</span>
        <button
          type="button"
          className={playbackTempoMode === 'real' ? 'primary-button' : 'ghost-button'}
          style={{ padding: '2px 8px', fontSize: 11, borderRadius: 8, flexShrink: 0 }}
          onClick={() => setPlaybackTempoMode('real')}
          title="真实时长"
        >
          真实
        </button>
        <button
          type="button"
          className={playbackTempoMode === 'compressed' ? 'primary-button' : 'ghost-button'}
          style={{ padding: '2px 8px', fontSize: 11, borderRadius: 8, flexShrink: 0 }}
          onClick={() => setPlaybackTempoMode('compressed')}
          title="压缩展示"
        >
          压缩
        </button>
        <span className="studio-topbar__divider" />
        <button type="button" className="ghost-button" style={{ padding: '2px 8px', fontSize: 11, borderRadius: 8, flexShrink: 0 }} disabled={!hasPlaybackTimeline || runPhase === 'running'} onClick={handlePlaybackToggle} title="回放控制仅作用于可视化时间轴">
          {playbackPlaying ? '⏸' : '⏵'}
        </button>
        <button type="button" className="ghost-button" style={{ padding: '2px 8px', fontSize: 11, borderRadius: 8, flexShrink: 0 }} disabled={playbackFrames.length <= 1 || runPhase === 'running'} onClick={handlePlaybackRestart}>
          ↺
        </button>
        <button
          type="button"
          className="ghost-button"
          style={{ padding: '2px 7px', fontSize: 11, borderRadius: 8, flexShrink: 0 }}
          disabled={!canPlaybackStep || runPhase === 'running'}
          onClick={handlePlaybackStep}
          title="切到下一事件并保持暂停"
        >
          步进
        </button>
        <button type="button" className="ghost-button" style={{ padding: '2px 8px', fontSize: 11, borderRadius: 8, flexShrink: 0 }} disabled={!liveMetrics && runPhase !== 'running'} onClick={handleStopRun}>
          ⏹
        </button>
        <span className="studio-topbar__divider" />
        <input
          type="range"
          min={playbackSliderMin}
          max={playbackSliderMax}
          step={playbackSliderStep}
          value={playbackSliderValue}
          onChange={(event) => handlePlaybackSeek(Number(event.target.value))}
          disabled={playbackFrames.length <= 1}
          style={{ flex: '1 1 100px', minWidth: 80, flexShrink: 1 }}
        />
        <span className="studio-topbar__label">{playbackProgressLabel}</span>
        <span className="status-pill" style={{ flexShrink: 0, fontSize: 10, padding: '1px 5px' }} title={usingTraceEvents ? `事件源 Trace${eventFile ? ' 已载入' : ''}` : '事件源 Metrics'}>{usingTraceEvents ? '源 Trace' : '源 Metrics'}</span>
        {communicationEvents.length > 0 && (
          <>
            <span className="studio-topbar__divider" />
            <span className="studio-topbar__label">筛选</span>
            <select
              value={eventLayerFilter}
              onChange={(event) => setEventLayerFilter(event.target.value)}
              style={{ border: '1px solid rgba(148,163,184,0.18)', borderRadius: 8, background: 'rgba(2,10,19,0.9)', color: '#fff', padding: '2px 8px', fontSize: 11, flexShrink: 0 }}
              title="按协议层过滤事件"
            >
              <option value="all">全部层</option>
              {availableEventLayers.map((layer) => (
                <option key={layer} value={layer}>{formatEventLayerLabel(layer)}</option>
              ))}
            </select>
            <select
              value={eventCodeFilter}
              onChange={(event) => setEventCodeFilter(event.target.value)}
              style={{ border: '1px solid rgba(148,163,184,0.18)', borderRadius: 8, background: 'rgba(2,10,19,0.9)', color: '#fff', padding: '2px 8px', fontSize: 11, flexShrink: 0, maxWidth: 150 }}
              title="按事件类型过滤事件"
            >
              <option value="all">全部事件</option>
              {availableEventCodes.map((code) => (
                <option key={code} value={code}>{formatEventCodeLabel(code)}</option>
              ))}
            </select>
            <span className="status-pill" style={{ flexShrink: 0, fontSize: 10, padding: '1px 5px' }} title="当前帧筛选后事件数 / 当前帧原始事件数">
              {`筛中 ${currentFrameEvents.length}/${currentFrameEventCount}`}
            </span>
          </>
        )}
        <span className="status-pill" style={{ flexShrink: 0, fontSize: 10, padding: '1px 5px' }} title={hasPlaybackTimeline ? `${usingTraceEvents ? '事件' : '采样'}步长 ${formatSecondsLabel(playbackStepSeconds)}` : '单帧'}>{hasPlaybackTimeline ? `步长 ${formatSecondsLabel(playbackStepSeconds)}` : '单帧'}</span>
        <span className="status-pill" style={{ flexShrink: 0, fontSize: 10, padding: '1px 6px' }}>≈{playbackWallClock}</span>
        <span className="status-pill" style={{ flexShrink: 0, fontSize: 10, padding: '1px 6px' }}>{currentTimeLabel}</span>
      </div>

      {(saveError || saveNote) && (
        <div className="studio-banner" style={{ color: saveError ? 'var(--danger)' : 'var(--success)', margin: '0 0 4px', flexShrink: 0 }}>
          {saveError ?? saveNote}
        </div>
      )}

      <div className="studio-layout studio-layout--compact">
        {viewMode === 'topology' ? (
          <StudioCanvas
            nodes={canvasNodes}
            metrics={frameMetrics}
            selectedNodeId={selectedNodeId}
            selectedEdgeKey={selectedEdgeKey}
            focusedEdgeKeys={focusedEdgeKeys}
            nodeLabels={nodeLabels}
            edgeLabels={edgeLabels}
            activeEdgeKeys={activeEdgeKeys}
            currentEvents={currentFrameEvents}
            currentTimeLabel={currentTimeLabel}
            frameMotionSeconds={currentFrameVisibleSeconds}
            motionEnabled={playbackPlaying && hasPlaybackTimeline}
            usesManualRangeLimit={useManualLinkRange}
            bathymetry={displayBathymetry}
            environmentBounds={studioEnvironmentBounds}
            editable={runPhase === 'idle'}
            onSelectNode={(nodeId) => setSelection({ scope: 'node', nodeId })}
            onSelectEdge={(edgeKey) => setSelection({ scope: 'edge', edgeKey })}
            onSelectScene={() => setSelection({ scope: 'scene' })}
            onContextMenu={handleContextMenu}
            onMoveNode={handleMoveNode}
          />
        ) : viewMode === 'profile' ? (
          <ProfileView
            nodes={canvasNodes}
            bathymetry={displayBathymetry}
            metrics={frameMetrics.length > 0 ? frameMetrics : previewMetrics}
            metricHistory={playbackSourceMetrics}
            rays={rays}
            previewArrivalsPath={typeof resolvedTransmissionParams.arr_json_file === 'string' && resolvedTransmissionParams.arr_json_file ? resolvedTransmissionParams.arr_json_file : null}
            selectedNodeId={selectedNodeId}
            focusedEdgeKey={focusedEdgeKey}
            currentTimeSeconds={activePlaybackTimeSeconds}
            currentEvent={focusedEvent}
            environmentBounds={studioEnvironmentBounds}
            editable={runPhase === 'idle'}
            onMoveNode={handleMoveNode}
            onSelectNode={(nodeId) => setSelection({ scope: 'node', nodeId })}
            onSelectEdge={(edgeKey) => setSelection({ scope: 'edge', edgeKey })}
          />
        ) : (
          <Scene3DView
            nodes={canvasNodes}
            bathymetry={displayBathymetry}
            rays={rays}
            waterDepth={Number(resolvedTransmissionParams.water_depth_m) || 100}
            environmentBounds={studioEnvironmentBounds}
            showCommunicationRange={useManualLinkRange}
            selectedNodeId={selectedNodeId}
            selectedEdgeKey={selectedEdgeKey}
            focusedEdgeKey={focusedEdgeKey}
            highlightedEdgeKeys={focusedEdgeKeys}
            currentEvents={currentFrameEvents}
            onSelectEdge={(edgeKey) => setSelection({ scope: 'edge', edgeKey })}
          />
        )}

        <PropertyInspector
          selection={selection}
          workingScenario={workingScenario}
          nodeTemplates={nodeTemplates}
          nodeBindings={nodeBindings}
          edgeBindings={edgeBindings}
          sceneBinding={sceneBinding}
          previewMetrics={previewMetrics}
          resultMetricEdgeKeys={resultMetricEdgeKeys}
          sections={sections}
          environmentBounds={studioEnvironmentBounds}
          onUpdateNode={handleUpdateNode}
          onUpdateNodeApp={(nodeId, field, value) => handleUpdateNodeApp(nodeId, field, value as string | number)}
          onUpdateNodeBinding={handleUpdateNodeBinding}
          onApplyNodeTemplate={handleApplyNodeTemplate}
          onSaveNodeAsTemplate={handleSaveNodeAsTemplate}
          onUpdateEdgeBinding={handleUpdateEdgeBinding}
          onUpdateScenarioMeta={(field, value) => handleUpdateScenarioMeta(field as keyof DemoScenario['scenario_metadata'], String(value))}
          onUpdateMeasurement={(field, value) => {
            if (field === 'engine_name') {
              handleUpdateMeasurement(field, String(value));
            } else if (field === 'noise_std' || field === 'dr_noise_std') {
              handleUpdateMeasurement(field, Number(value));
            }
          }}
          onSwitchTopology={(kind) => {
            if (kind === 'star' || kind === 'p2p' || kind === 'full_mesh') {
              handleSwitchTopology(kind);
            }
          }}
          onSetAllEdgeLinkProfile={handleSetAllEdgeLinkProfile}
          onUpdateNoiseComposition={handleUpdateNoiseComposition}
          onUpdateNoiseParam={(field, value) => handleUpdateNoiseParam(field, Number(value))}
          onUpdateTransmissionParam={handleUpdateTransmissionParam}
          gridFiles={gridFiles}
          bathymetryFiles={bathymetryFiles}
          onRefreshDataFiles={handleRefreshDataFiles}
          environmentDatabases={environmentDatabases}
          onSelectEnvironmentDatabase={handleSelectEnvironmentDatabase}
          onAddNode={handleAddNode}
          onRemoveSelectedNode={handleRemoveSelectedNode}
          onOpenWizard={() => setWizardOpen(true)}
        />
      </div>

      {nodeTemplatePickerOpen && (
        <div className="studio-modal__backdrop" onClick={() => setNodeTemplatePickerOpen(false)}>
          <div className="studio-modal studio-modal--template" onClick={(event) => event.stopPropagation()}>
            <div className="studio-modal__head">
              <div>
                <strong>选择节点模板</strong>
                <div className="wizard-subtitle">新增节点时从模板实例化，创建后仍可单独微调。</div>
              </div>
              <button type="button" className="ghost-button" onClick={() => setNodeTemplatePickerOpen(false)}>关闭</button>
            </div>
            <div className="template-grid">
              {nodeTemplates.map((template) => {
                const active = template.id === defaultNodeTemplateId;
                const summary = [template.config.role, template.config.application?.type, template.config.mac?.protocol, template.config.routing?.protocol].filter(Boolean).join(' · ');
                return (
                  <button
                    key={template.id}
                    type="button"
                    className={`template-card ${active ? 'template-card--active' : ''}`}
                    onClick={() => setDefaultNodeTemplateId(template.id)}
                  >
                    <div className="template-card__head">
                      <strong>{template.name}</strong>
                      {template.builtIn && <span className="status-pill">内置</span>}
                    </div>
                    <div className="template-card__summary">{template.description || '未填写模板说明'}</div>
                    <div className="template-card__meta">{summary || '仅保留角色与基础配置'}</div>
                  </button>
                );
              })}
            </div>
            <div className="scenario-wizard__actions">
              <button type="button" className="ghost-button" onClick={() => setNodeTemplatePickerOpen(false)}>取消</button>
              <button type="button" className="ghost-button" onClick={() => openTemplateLibrary(defaultNodeTemplateId)}>管理模板</button>
              <button type="button" className="primary-button" onClick={handleConfirmAddNodeFromTemplate} disabled={!defaultNodeTemplateId}>实例化节点</button>
            </div>
          </div>
        </div>
      )}

      {templateLibraryOpen && (() => {
        const selectedTemplate = nodeTemplates.find((template) => template.id === templateLibrarySelectionId) ?? nodeTemplates[0] ?? null;
        return (
          <div className="studio-modal__backdrop" onClick={() => setTemplateLibraryOpen(false)}>
            <div className="studio-modal studio-modal--wizard" onClick={(event) => event.stopPropagation()}>
              <div className="studio-modal__head">
                <div>
                  <strong>节点模板库</strong>
                  <div className="wizard-subtitle">先管理模板本身，再在节点面板里应用或另存当前节点为模板。</div>
                </div>
                <button type="button" className="ghost-button" onClick={() => setTemplateLibraryOpen(false)}>关闭</button>
              </div>
              <div className="template-library">
                <div className="template-library__list">
                  {nodeTemplates.map((template) => {
                    const active = template.id === selectedTemplate?.id;
                    return (
                      <button
                        key={template.id}
                        type="button"
                        className={`template-card ${active ? 'template-card--active' : ''}`}
                        onClick={() => {
                          setTemplateDeleteArmId(null);
                          setTemplateLibrarySelectionId(template.id);
                          setTemplateLibraryDraft({
                            id: template.id,
                            name: template.name,
                            description: template.description ?? '',
                            builtIn: Boolean(template.builtIn),
                            saving: false,
                            error: null,
                          });
                        }}
                      >
                        <div className="template-card__head">
                          <strong>{template.name}</strong>
                          {template.builtIn && <span className="status-pill">内置</span>}
                        </div>
                        <div className="template-card__summary">{template.description || '未填写模板说明'}</div>
                        <div className="template-card__meta">{[template.config.role, template.config.application?.type, template.config.mac?.protocol, template.config.routing?.protocol].filter(Boolean).join(' · ')}</div>
                      </button>
                    );
                  })}
                </div>

                <div className="template-library__detail">
                  {selectedTemplate && templateLibraryDraft ? (
                    <>
                      <div className="template-preview">
                        <div className="template-preview__head">模板预览</div>
                        <div className="template-preview__chips">
                          <span className="status-pill">角色 {selectedTemplate.config.role}</span>
                          <span className="status-pill">应用 {selectedTemplate.config.application?.type ?? 'null'}</span>
                          <span className="status-pill">MAC {selectedTemplate.config.mac?.protocol ?? '未设置'}</span>
                          <span className="status-pill">路由 {selectedTemplate.config.routing?.protocol ?? '未设置'}</span>
                          <span className="status-pill">频率 {selectedTemplate.config.center_frequency_hz ?? 12000} Hz</span>
                          <span className="status-pill">范围 {selectedTemplate.config.communication_range_m ?? 1800} m</span>
                        </div>
                      </div>

                      <div className="template-form">
                        <label className="template-form__field">
                          <span>模板名称</span>
                          <input
                            value={templateLibraryDraft.name}
                            onChange={(event) => setTemplateLibraryDraft((prev) => (prev ? { ...prev, name: event.target.value } : prev))}
                            disabled={templateLibraryDraft.builtIn}
                          />
                        </label>
                        <label className="template-form__field">
                          <span>模板说明</span>
                          <textarea
                            value={templateLibraryDraft.description}
                            onChange={(event) => setTemplateLibraryDraft((prev) => (prev ? { ...prev, description: event.target.value } : prev))}
                            rows={4}
                            disabled={templateLibraryDraft.builtIn}
                          />
                        </label>
                      </div>

                      {templateLibraryDraft.error && <div className="scenario-wizard__error">{templateLibraryDraft.error}</div>}

                      <div style={{ display: 'flex', gap: 10, flexWrap: 'wrap' }}>
                        <button type="button" className="ghost-button" onClick={() => setDefaultNodeTemplateId(selectedTemplate.id)}>
                          设为新增默认
                        </button>
                        {selection.scope === 'node' && (
                          <button type="button" className="ghost-button" onClick={() => handleApplyNodeTemplate(selection.nodeId, selectedTemplate.id)}>
                            应用到当前节点
                          </button>
                        )}
                        {!templateLibraryDraft.builtIn && (
                          <button type="button" className="primary-button" disabled={templateLibraryDraft.saving || !templateLibraryDraft.name.trim()} onClick={() => void handleSaveTemplateLibraryDraft()}>
                            {templateLibraryDraft.saving ? '保存中...' : '保存修改'}
                          </button>
                        )}
                        {!templateLibraryDraft.builtIn && (
                          <button type="button" className="ghost-button" style={{ color: templateDeleteArmId === templateLibraryDraft.id ? 'var(--danger)' : undefined, borderColor: templateDeleteArmId === templateLibraryDraft.id ? 'rgba(239,68,68,0.35)' : undefined }} onClick={() => void handleDeleteTemplateFromLibrary()}>
                            {templateDeleteArmId === templateLibraryDraft.id ? '确认删除模板' : '删除模板'}
                          </button>
                        )}
                      </div>

                      {templateLibraryDraft.builtIn && (
                        <div className="wizard-inline-note">内置模板可直接应用，但不能在模板库中修改或删除。需要变体时，请先把节点调好再使用“保存当前为模板”。</div>
                      )}
                    </>
                  ) : (
                    <div className="template-preview">
                      <div className="template-preview__head">暂无模板</div>
                      <div className="template-card__summary">当前模板库为空。</div>
                    </div>
                  )}
                </div>
              </div>
            </div>
          </div>
        );
      })()}

      {nodeTemplateSaveDialog && (() => {
        const node = workingScenario.nodes.find((item) => item.id === nodeTemplateSaveDialog.nodeId);
        const binding = nodeBindings[nodeTemplateSaveDialog.nodeId] ?? { nodeId: nodeTemplateSaveDialog.nodeId, overridesByAssetId: {} };
        return (
          <div className="studio-modal__backdrop" onClick={() => !nodeTemplateSaveDialog.saving && setNodeTemplateSaveDialog(null)}>
            <div className="studio-modal studio-modal--template" onClick={(event) => event.stopPropagation()}>
              <div className="studio-modal__head">
                <div>
                  <strong>保存当前为模板</strong>
                  <div className="wizard-subtitle">把当前节点的协议栈和参数快照保存为可复用模板。</div>
                </div>
                <button type="button" className="ghost-button" onClick={() => setNodeTemplateSaveDialog(null)} disabled={nodeTemplateSaveDialog.saving}>关闭</button>
              </div>
              <div className="template-form">
                <label className="template-form__field">
                  <span>模板名称</span>
                  <input
                    value={nodeTemplateSaveDialog.name}
                    onChange={(event) => setNodeTemplateSaveDialog((prev) => (prev ? { ...prev, name: event.target.value } : prev))}
                    placeholder="例如：深海传感器-CSMA"
                  />
                </label>
                <label className="template-form__field">
                  <span>模板说明</span>
                  <textarea
                    value={nodeTemplateSaveDialog.description}
                    onChange={(event) => setNodeTemplateSaveDialog((prev) => (prev ? { ...prev, description: event.target.value } : prev))}
                    rows={3}
                    placeholder="说明该模板适用的角色、协议与场景"
                  />
                </label>
              </div>
              {node && (
                <div className="template-preview">
                  <div className="template-preview__head">当前装配快照</div>
                  <div className="template-preview__chips">
                    <span className="status-pill">角色 {node.role}</span>
                    <span className="status-pill">应用 {node.application?.type ?? 'null'}</span>
                    <span className="status-pill">MAC {node.mac?.protocol ?? binding.macProtocolAssetId ?? '未设置'}</span>
                    <span className="status-pill">路由 {node.routing?.protocol ?? binding.routingProtocolAssetId ?? '未设置'}</span>
                    <span className="status-pill">频率 {node.center_frequency_hz ?? 12000} Hz</span>
                    <span className="status-pill">通信范围 {node.communication_range_m ?? 1800} m</span>
                  </div>
                </div>
              )}
              {nodeTemplateSaveDialog.error && <div className="scenario-wizard__error">{nodeTemplateSaveDialog.error}</div>}
              <div className="scenario-wizard__actions">
                <button type="button" className="ghost-button" onClick={() => setNodeTemplateSaveDialog(null)} disabled={nodeTemplateSaveDialog.saving}>取消</button>
                <button type="button" className="primary-button" disabled={nodeTemplateSaveDialog.saving || !nodeTemplateSaveDialog.name.trim()} onClick={() => void handleConfirmSaveNodeTemplate()}>
                  {nodeTemplateSaveDialog.saving ? '保存中...' : '保存模板'}
                </button>
              </div>
            </div>
          </div>
        );
      })()}

      {logsOpen && (
        <div className="studio-modal__backdrop" onClick={() => setLogsOpen(false)}>
          <div className="studio-modal studio-modal--log" onClick={(event) => event.stopPropagation()}>
            <div className="studio-modal__head">
              <strong>运行日志</strong>
              <button type="button" className="ghost-button" onClick={() => setLogsOpen(false)}>关闭</button>
            </div>
            <div className="log-list" style={{ minHeight: 320, maxHeight: '60vh' }}>
              {runLogs.length === 0 && <div className="log-list__item">等待仿真启动...</div>}
              {runLogs.map((entry, index) => (
                <div key={`${entry.msg}-${index}`} className={`log-list__item log-list__item--${entry.level}`}>{entry.msg}</div>
              ))}
              <div ref={logEndRef} />
            </div>
          </div>
        </div>
      )}

      {resultsOpen && (
        <div className="studio-modal__backdrop" onClick={() => setResultsOpen(false)}>
          <div className="studio-modal studio-modal--results" onClick={(event) => event.stopPropagation()}>
            <div className="studio-modal__head">
              <strong>结果分析</strong>
              <div style={{ display: 'flex', gap: 8 }}>
                <button type="button" className="ghost-button" onClick={handleExportResults} disabled={displayMetrics.length === 0}>导出 CSV</button>
                <button type="button" className="ghost-button" onClick={() => setResultsOpen(false)}>关闭</button>
              </div>
            </div>
            {resultError && <div style={{ marginBottom: 10, fontSize: 12, color: 'var(--danger)' }}>结果拉取失败：{resultError}</div>}
            <div style={{ display: 'flex', gap: 12, flexWrap: 'wrap', alignItems: 'center', marginBottom: 10 }}>
              <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontSize: 12 }}>
                <span style={{ color: 'var(--text-dim)' }}>数据源</span>
                <select
                  value={selectedResultSource}
                  onChange={(event) => setSelectedResultSource(event.target.value)}
                  style={{ minWidth: 320, padding: '7px 10px', borderRadius: 8 }}
                >
                  <option value={LATEST_RESULTS_SOURCE}>最新结果（当前场景）</option>
                  {scenarioArchives.map((archive) => (
                    <option key={archive.id} value={archive.id}>
                      {new Date(archive.created_at * 1000).toLocaleString('zh-CN')} · {archive.id}
                    </option>
                  ))}
                </select>
              </label>
              {archiveLoading ? <span className="status-pill">归档加载中...</span> : null}
              {archiveResultActive && selectedArchiveSummary ? (
                <span className={`status-pill ${selectedArchiveSummary.status === 'success' ? 'status-pill--ready' : 'status-pill--warning'}`}>
                  {selectedArchiveSummary.status === 'success' ? '归档结果' : `失败归档 · exit=${selectedArchiveSummary.exit_code}`}
                </span>
              ) : null}
              {scenarioArchives.length === 0 ? <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>当前场景暂无已归档运行。</span> : null}
            </div>
            <div className="studio-results__head">
              <div className="chip-list">
                <span className="status-pill status-pill--ready">{displayMetrics.length > 0 ? `结果条目 ${displayMetrics.length}` : '暂无真实结果'}</span>
                <span className="status-pill">{displayResultFile ?? '尚未生成 CSV'}</span>
                <span className="status-pill">{hasPlaybackTimeline ? `时间帧 ${playbackFrames.length}` : '未检测到时间轴'}</span>
                <span className="status-pill">{archiveResultActive ? `事件 ${displayEventCount} / 射线 ${displayRayCount}` : '最新实时视图'}</span>
                {displayEventFile ? <span className="status-pill">{displayEventFile}</span> : null}
              </div>
            </div>
            {archiveResultActive && selectedArchiveDetail ? (
              <div className="asset-card" style={{ marginTop: 12 }}>
                <strong>{selectedArchiveDetail.scenario_metadata.name || selectedArchiveDetail.scenario}</strong>
                <span style={{ color: 'var(--text-dim)' }}>
                  归档 ID {selectedArchiveDetail.id} · {new Date(selectedArchiveDetail.created_at * 1000).toLocaleString('zh-CN')}
                </span>
                <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                  环境库 {selectedArchiveDetail.environment_database_id || '—'} · 传输模型 {selectedArchiveDetail.transmission_type || '—'} · 输出模式 {selectedArchiveDetail.trace_mode || '—'}
                </span>
                <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                  平均时延 {selectedArchiveDetail.summary.avg_delay_s !== null ? `${selectedArchiveDetail.summary.avg_delay_s.toFixed(4)} s` : '—'} · 平均接收电平 {selectedArchiveDetail.summary.avg_received_level_db !== null ? `${selectedArchiveDetail.summary.avg_received_level_db.toFixed(2)} dB` : '—'}
                </span>
              </div>
            ) : null}
            <div className="trends-grid" style={{ marginTop: 12 }}>
              <MiniTrend data={timeSeries.delay} unit="s" label="传播时延" />
              <MiniTrend data={timeSeries.power} unit="dB" label="接收强度" />
              <MiniTrend data={timeSeries.range} unit="m" label="伪距" />
              {timeSeries.noise && <MiniTrend data={timeSeries.noise} unit="dB" label="环境噪声" />}
              {timeSeries.snr && <MiniTrend data={timeSeries.snr} unit="dB" label="信噪比" />}
            </div>
            <div className="data-table data-table--results" style={{ marginTop: 14, maxHeight: '42vh', overflow: 'auto' }}>
              <div className="data-table__row data-table__row--head">
                <span>time_s</span>
                <span>链路</span>
                <span>delay_s</span>
                <span>received_level_db</span>
                <span>pseudo_range_m</span>
                <span>noise_dB</span>
                <span>SNR_dB</span>
              </div>
              {displayMetrics.length === 0 && <div className="data-table__row"><span>暂无结果</span><span>—</span><span>—</span><span>—</span><span>—</span><span>—</span><span>—</span></div>}
              {displayMetrics.map((metric) => (
                <div key={`${metric.tx_id}-${metric.rx_id}-${metric.time_s ?? 'static'}`} className="data-table__row">
                  <span>{typeof metric.time_s === 'number' ? metric.time_s.toFixed(2) : 'static'}</span>
                  <span>{metric.tx_id} → {metric.rx_id}</span>
                  <span>{metric.delay_s.toFixed(6)}</span>
                  <span>{metric.received_level_db.toFixed(3)}</span>
                  <span>{metric.pseudo_range_m.toFixed(2)}</span>
                  <span>{metric.noise_level_db !== undefined ? metric.noise_level_db.toFixed(1) : '—'}</span>
                  <span>{metric.snr_db !== undefined ? metric.snr_db.toFixed(1) : '—'}</span>
                </div>
              ))}
            </div>
          </div>
        </div>
      )}

      <ScenarioWizard
        open={wizardOpen}
        initialName={`${workingScenario.scenario_metadata.scenario_id}_wizard`}
        onClose={() => setWizardOpen(false)}
        onCreated={handleWizardCreated}
      />

      {ctxMenu && (
        <div ref={ctxMenuRef} className="ctx-menu" style={{ top: ctxMenu.y, left: ctxMenu.x }}>
          {ctxMenu.nodeId !== undefined ? (
            <>
              <div className="ctx-menu__item" onClick={ctxCopyNode}>📋 复制节点 <span style={{ marginLeft: 'auto', color: '#607080', fontSize: 11 }}>Ctrl+C</span></div>
              <div className="ctx-menu__item" onClick={ctxPasteNode} style={{ opacity: clipboardNode ? 1 : 0.4 }}>📌 粘贴节点 <span style={{ marginLeft: 'auto', color: '#607080', fontSize: 11 }}>Ctrl+V</span></div>
              <div className="ctx-menu__divider" />
              <div className="ctx-menu__item" onClick={ctxSetAsSink}>⬟ 设为汇聚中心</div>
              <div className="ctx-menu__divider" />
              <div className="ctx-menu__item ctx-menu__item--danger" onClick={ctxDeleteNode}>🗑 删除节点 <span style={{ marginLeft: 'auto', color: '#607080', fontSize: 11 }}>Del</span></div>
            </>
          ) : (
            <>
              <div className="ctx-menu__item" onClick={() => { handleAddNode(); closeCtxMenu(); }}>+ 新增传感器节点</div>
              <div className="ctx-menu__item" onClick={ctxPasteNode} style={{ opacity: clipboardNode ? 1 : 0.4 }}>📌 粘贴节点 <span style={{ marginLeft: 'auto', color: '#607080', fontSize: 11 }}>Ctrl+V</span></div>
              <div className="ctx-menu__divider" />
              <div className="ctx-menu__item" onClick={() => { handleSwitchTopology(workingScenario.topology.deployment_type === 'p2p' ? 'star' : 'p2p'); closeCtxMenu(); }}>
                ↺ 切换 {workingScenario.topology.deployment_type === 'p2p' ? 'Star' : 'P2P'}
              </div>
            </>
          )}
        </div>
      )}
    </div>
  );
}
