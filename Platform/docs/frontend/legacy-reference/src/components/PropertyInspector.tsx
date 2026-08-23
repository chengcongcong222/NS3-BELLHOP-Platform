import { type ReactNode, useState, useRef, useEffect, useMemo } from 'react';

import { buildEnvironmentBatchFromWossSource, buildEnvironmentFromWossSource, createEnvironmentDatabase, createWossSource, deleteWossSource, fetchEnvironmentCapabilities, fetchWossSources, generateBathymetry, importBathymetry, importWossCacheIntoSource, importWossRealDataIntoSource, runBellhop, generateGrid, fetchSspData, uploadSsp, fetchDataFiles, generateSsp, fetchBathymetry, saveSsp } from '../services/api';
import type { BuildEnvironmentBatchFromWossSourceParams, BuildEnvironmentFromWossSourceParams, CreateWossSourceParams, GenerateBathymetryParams, ImportWossCacheIntoSourceParams, ImportWossRealDataIntoSourceParams, RunBellhopParams, GenerateSspParams } from '../services/api';
import type {
  DemoNode,
  DemoScenario,
  EnvironmentCapabilities,
  EnvironmentDatabase,
  LinkMetric,
  ModelLibraryAsset,
  ModelLibrarySection,
  NodeTemplate,
  StudioEdgeBinding,
  StudioEnvironmentBounds,
  StudioNodeBinding,
  StudioSceneBinding,
  WossSourceProfile,
  WossSourceVariant,
} from '../types';

/* ─────────── Interface ─────────── */

interface PropertyInspectorProps {
  selection: { scope: 'scene' } | { scope: 'node'; nodeId: number } | { scope: 'edge'; edgeKey: string };
  workingScenario: DemoScenario;
  nodeTemplates: NodeTemplate[];
  nodeBindings: Record<number, StudioNodeBinding>;
  edgeBindings: Record<string, StudioEdgeBinding>;
  sceneBinding: StudioSceneBinding;
  previewMetrics: LinkMetric[];
  resultMetricEdgeKeys: string[];
  sections: ModelLibrarySection[];
  gridFiles: string[];
  bathymetryFiles: string[];
  environmentDatabases: EnvironmentDatabase[];
  environmentBounds?: StudioEnvironmentBounds;
  onUpdateNode: (nodeId: number, updates: Partial<DemoNode>) => void;
  onUpdateNodeApp: (nodeId: number, field: string, value: unknown) => void;
  onUpdateNodeBinding: (nodeId: number, binding: Partial<StudioNodeBinding>) => void;
  onApplyNodeTemplate: (nodeId: number, templateId: string) => void;
  onSaveNodeAsTemplate: (nodeId: number) => void;
  onUpdateEdgeBinding: (edgeKey: string, binding: Partial<StudioEdgeBinding>) => void;

  onUpdateScenarioMeta: (field: string, value: unknown) => void;
  onUpdateMeasurement: (field: string, value: unknown) => void;
  onSwitchTopology: (kind: string) => void;
  onSetAllEdgeLinkProfile: (profileId: string) => void;
  onUpdateNoiseComposition: (asset: ModelLibraryAsset) => void;
  onUpdateNoiseParam: (field: string, value: unknown) => void;
  onUpdateTransmissionParam: (field: string, value: string) => void;
  onRefreshDataFiles: () => void;
  onAddNode: () => void;
  onRemoveSelectedNode: () => void;
  onOpenWizard: () => void;
  onSelectEnvironmentDatabase: (databaseId: string) => void;
}

/* ─────────── Styles ─────────── */

const selectStyle: React.CSSProperties = {
  width: '100%',
  border: '1px solid rgba(148,163,184,0.18)',
  borderRadius: 8,
  background: 'rgba(2,10,19,0.9)',
  color: '#fff',
  padding: '5px 8px',
  fontSize: 12,
};

const smallBtnStyle: React.CSSProperties = {
  border: '1px solid rgba(148,163,184,0.25)',
  borderRadius: 8,
  background: 'rgba(2,10,19,0.6)',
  color: 'var(--text-dim)',
  fontSize: 11,
  cursor: 'pointer',
  padding: '4px 10px',
  whiteSpace: 'nowrap',
};

const compactActionBtnStyle: React.CSSProperties = {
  ...smallBtnStyle,
  padding: '6px 10px',
  whiteSpace: 'normal',
  lineHeight: 1.35,
  textAlign: 'center',
  height: '100%',
};

const dangerBtnStyle: React.CSSProperties = {
  ...smallBtnStyle,
  borderColor: 'rgba(239,68,68,0.3)',
  color: '#f87171',
};

const warnBoxStyle: React.CSSProperties = {
  fontSize: 10,
  color: '#94a3b8',
  lineHeight: 1.5,
  marginTop: 6,
  padding: '6px 10px',
  background: 'rgba(234,179,8,0.06)',
  border: '1px solid rgba(234,179,8,0.15)',
  borderRadius: 8,
};

function formatBuildModeLabel(value?: string | null): string {
  if (value === 'bellhop') return 'Bellhop';
  if (value === 'analytical') return '解析近似';
  return value || '未标记';
}

function formatSourceTypeLabel(value?: string | null): string {
  if (value === 'woss-import') return 'WOSS/GEBCO/WOA';
  if (value === 'manual-import') return '手工导入';
  if (value === 'preset') return '预设/内建';
  return value || '未标记';
}

function formatValidationLabel(value?: string | null): string {
  if (value === 'validated') return '已验收';
  if (value === 'draft') return '草稿';
  if (value === 'error') return '异常';
  return value || '未标记';
}

function formatTimeReferenceLabel(value?: { month?: number | null; season?: string | null; label?: string | null } | null): string {
  if (!value) return '未标记';
  const parts = [
    value.month != null ? `${value.month}月` : '',
    value.season || '',
    value.label || '',
  ].filter(Boolean);
  return parts.join(' / ') || '未标记';
}

function formatTimestampLabel(value?: number | null): string {
  if (!value) return '未记录';
  const date = new Date(value * 1000);
  if (Number.isNaN(date.getTime())) return '未记录';
  return date.toLocaleString('zh-CN', { hour12: false });
}

function formatCacheArtifactLabel(value?: { path: string; size_bytes?: number | null } | null): string {
  if (!value?.path) return '未缓存';
  if (typeof value.size_bytes !== 'number') return value.path;
  const sizeLabel = value.size_bytes >= 1024 * 1024
    ? `${(value.size_bytes / (1024 * 1024)).toFixed(2)} MB`
    : `${(value.size_bytes / 1024).toFixed(1)} KB`;
  return `${value.path} (${sizeLabel})`;
}

function sanitizeWossProfileId(value: string): string {
  return value.trim().replace(/[^a-zA-Z0-9_-]+/g, '_').replace(/^_+|_+$/g, '');
}

function parseTransectBearingFromLabel(value?: string | null): number | null {
  if (!value) return null;
  const match = /bearing_(-?\d+(?:\.\d+)?)/.exec(value);
  if (!match) return null;
  const parsed = Number(match[1]);
  return Number.isFinite(parsed) ? parsed : null;
}

function formatPathName(value?: string | null) {
  return value ? value.split('/').pop() ?? value : '未绑定';
}

function mergeUniqueStrings(...groups: Array<string[] | undefined | null>) {
  return Array.from(new Set(groups.flatMap((group) => group ?? []).map((item) => item.trim()).filter(Boolean)));
}

function buildSuggestedEnvironmentDescription(args: {
  pipelineMode: 'analytical' | 'bellhop';
  gridRange: number;
  gridDepth: number;
  gridFreq: number;
  wossRegion: string;
  wossLatitude: number;
  wossLongitude: number;
  wossMonth: number;
  wossSeason: string;
  builderSspFile: string;
  builderBathymetryFile: string;
  selectedWossSourceName?: string | null;
}) {
  const parts = [
    args.selectedWossSourceName || args.wossRegion.trim() || `${args.wossLatitude.toFixed(3)}, ${args.wossLongitude.toFixed(3)}`,
    args.wossMonth >= 1 && args.wossMonth <= 12 ? `${args.wossMonth} 月` : (args.wossSeason.trim() || '未指定时相'),
    `${args.gridFreq.toFixed(1)} kHz`,
    `${Math.round(args.gridRange)} m`,
    `${Math.round(args.gridDepth)} m`,
    args.pipelineMode === 'bellhop' ? 'Bellhop 声线追踪' : '解析近似',
    args.builderSspFile ? `SSP:${formatPathName(args.builderSspFile)}` : '',
    args.builderBathymetryFile ? `地形:${formatPathName(args.builderBathymetryFile)}` : '',
  ].filter(Boolean);
  return parts.join(' / ');
}

function buildEnvironmentCoverageSummary(args: {
  sspData: number[][] | null;
  bathData: { range_m: number[]; depth_m: number[] } | null;
  gridRange: number;
  gridDepth: number;
  waterDepth: number;
}) {
  const sspDepthMax = args.sspData && args.sspData.length > 0
    ? Math.max(...args.sspData.map((row) => row[0]))
    : null;
  const bathymetryRangeMax = args.bathData?.range_m?.length
    ? Math.max(...args.bathData.range_m)
    : null;
  const bathymetryDepthMax = args.bathData?.depth_m?.length
    ? Math.max(...args.bathData.depth_m)
    : null;
  const recommendedDepth = bathymetryDepthMax != null && sspDepthMax != null
    ? Math.min(bathymetryDepthMax, sspDepthMax)
    : (bathymetryDepthMax ?? sspDepthMax ?? null);
  const warnings: string[] = [];
  const blockingIssues: string[] = [];

  if (sspDepthMax != null && args.gridDepth > sspDepthMax) {
    warnings.push(`当前最大深度 ${Math.round(args.gridDepth)} m 已超过 SSP 覆盖深度 ${Math.round(sspDepthMax)} m，将触发声速尾值延伸。`);
  }
  if (bathymetryRangeMax != null && args.gridRange > bathymetryRangeMax) {
    warnings.push(`当前最大距离 ${Math.round(args.gridRange)} m 已超过地形覆盖范围 ${Math.round(bathymetryRangeMax)} m，超出段不再具有真实地形约束。`);
  }
  if (bathymetryDepthMax != null && args.gridDepth > bathymetryDepthMax) {
    warnings.push(`当前最大深度 ${Math.round(args.gridDepth)} m 已超过地形记录的最大水深 ${Math.round(bathymetryDepthMax)} m。`);
  }
  if (bathymetryDepthMax != null && args.waterDepth > bathymetryDepthMax) {
    warnings.push(`当前水深记录值 ${Math.round(args.waterDepth)} m 已超过地形最大水深 ${Math.round(bathymetryDepthMax)} m。`);
  }
  if (args.gridRange <= 0) blockingIssues.push('最大距离必须大于 0。');
  if (args.gridDepth <= 0) blockingIssues.push('最大深度必须大于 0。');
  if (args.waterDepth <= 0) blockingIssues.push('水深必须大于 0。');

  return {
    sspDepthMax,
    bathymetryRangeMax,
    bathymetryDepthMax,
    recommendedRange: bathymetryRangeMax,
    recommendedDepth,
    warnings,
    blockingIssues,
  };
}

/* ─────────── Option lists ─────────── */

const sspFormulaOptions = [
  { value: 'munk', label: 'Munk 深海剖面' },
  { value: 'isovelocity', label: '等速 (Isovelocity)' },
  { value: 'linear_gradient', label: '线性梯度' },
  { value: 'thermocline', label: '温跃层' },
  { value: 'deep_channel', label: '深海声道' },
];

const sspQuickPresets = [
  { label: '南海浅海', formula: 'thermocline' as const, depthMax: 200, c0: 1540, tcDepth: 40, tcThick: 25, surfSpeed: 1540, deepSpeed: 1505 },
  { label: '深海大洋', formula: 'munk' as const, depthMax: 5000, c0: 1500, tcDepth: 50, tcThick: 30, surfSpeed: 1540, deepSpeed: 1500 },
  { label: '等温浅水', formula: 'isovelocity' as const, depthMax: 100, c0: 1520, tcDepth: 50, tcThick: 30, surfSpeed: 1540, deepSpeed: 1500 },
  { label: '深海声道', formula: 'deep_channel' as const, depthMax: 5000, c0: 1500, tcDepth: 50, tcThick: 30, surfSpeed: 1540, deepSpeed: 1500 },
];

const profileOptions = [
  { value: 'flat', label: '平坦' },
  { value: 'ridge', label: '海脊' },
  { value: 'slope', label: '斜坡' },
  { value: 'trench', label: '海沟' },
];

const sourceLevelPresets = [
  { label: '170 dB 低功率', value: 170, description: '短距监听、低功率信标或保守起步值' },
  { label: '190 dB 常规', value: 190, description: '常规主动通信/信标的常见起步档位' },
  { label: '210 dB 强主动', value: 210, description: '较强主动声源或远距探测的高档位' },
];

const roleOptions = [
  { value: 'sensor', label: '传感器 (Sensor)' },
  { value: 'sink', label: '汇聚节点 (Sink)' },
  { value: 'relay', label: '中继 (Relay)' },
  { value: 'anchor', label: '锚节点 (Anchor)' },
  { value: 'hil', label: '硬件在环 (HIL)' },
];

const monthOptions = [
  { value: '0', label: '年平均 / 仅使用季节' },
  ...Array.from({ length: 12 }, (_, index) => ({ value: String(index + 1), label: `${index + 1} 月` })),
];

const seasonOptions = [
  { value: '', label: '未指定' },
  { value: 'spring', label: '春季' },
  { value: 'summer', label: '夏季' },
  { value: 'autumn', label: '秋季' },
  { value: 'winter', label: '冬季' },
];

const woaVersionOptions = [
  { value: '2023', label: 'WOA23' },
  { value: '2018', label: 'WOA18' },
];

const gebcoVersionOptions = [
  { value: '2020-public-api', label: 'GEBCO2020 公共 API' },
  { value: '2024', label: 'GEBCO2024' },
];

const deck41VersionOptions = [
  { value: 'legacy', label: 'DECK41 legacy' },
  { value: 'modern', label: 'DECK41 modern' },
];

const mobilityOptions = [
  { value: 'static', label: '静止' },
  { value: 'random_waypoint', label: '随机路点' },
  { value: 'constant_velocity', label: '匀速直线' },
];

const appOptionsByRole: Record<string, { value: string; label: string }[]> = {
  sensor: [
    { value: 'periodic_report', label: '周期上报' },
    { value: 'event_driven', label: '事件触发' },
    { value: 'null', label: '无应用' },
  ],
  sink: [
    { value: 'sink_aggregator', label: '汇聚接收' },
    { value: 'null', label: '无应用' },
  ],
  relay: [
    { value: 'null', label: '无应用 (转发)' },
  ],
  anchor: [
    { value: 'beacon', label: '锚点信标' },
    { value: 'null', label: '无应用' },
  ],
};

const macOptionsByRole: Record<string, { value: string; label: string }[]> = {
  sensor: [
    { value: 'mac-aloha', label: 'ALOHA' },
    { value: 'mac-csma', label: 'CSMA/CA' },
    { value: 'mac-tdma', label: 'TDMA' },
    { value: 'mac-no-mac', label: '无 MAC' },
  ],
  sink: [
    { value: 'mac-polling', label: 'Polling' },
    { value: 'mac-tdma', label: 'TDMA' },
    { value: 'mac-csma', label: 'CSMA/CA' },
    { value: 'mac-no-mac', label: '无 MAC' },
  ],
  relay: [
    { value: 'mac-aloha', label: 'ALOHA' },
    { value: 'mac-csma', label: 'CSMA/CA' },
    { value: 'mac-tdma', label: 'TDMA' },
    { value: 'mac-no-mac', label: '无 MAC' },
  ],
  anchor: [
    { value: 'mac-tdma', label: 'TDMA' },
    { value: 'mac-no-mac', label: '无 MAC' },
  ],
  hil: [
    { value: 'mac-csma', label: 'CSMA/CA' },
    { value: 'mac-tdma', label: 'TDMA' },
    { value: 'mac-no-mac', label: '无 MAC' },
  ],
};

const routingOptionsByRole: Record<string, { value: string; label: string }[]> = {
  sensor: [
    { value: 'routing-static', label: '静态路由' },
    { value: 'routing-flooding', label: '泛洪 (Flooding)' },
    { value: 'routing-aodv', label: 'AODV' },
    { value: 'routing-olsr', label: 'OLSR' },
  ],
  sink: [
    { value: 'routing-static', label: '静态路由' },
    { value: 'routing-olsr', label: 'OLSR' },
  ],
  relay: [
    { value: 'routing-static', label: '静态路由' },
    { value: 'routing-flooding', label: '泛洪 (Flooding)' },
    { value: 'routing-aodv', label: 'AODV' },
  ],
  anchor: [
    { value: 'routing-static', label: '静态路由' },
  ],
  hil: [
    { value: 'routing-static', label: '静态路由' },
    { value: 'routing-olsr', label: 'OLSR' },
  ],
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

function buildMacConfigFromAssetId(assetId: string, current?: DemoNode['mac']): NonNullable<DemoNode['mac']> {
  const protocol = MAC_ASSET_TO_PROTOCOL[assetId] ?? current?.protocol ?? 'aloha';
  if (assetId === 'mac-csma') {
    return {
      protocol,
      sense_duration_ms: current?.sense_duration_ms ?? 200,
      backoff_min_ms: current?.backoff_min_ms ?? 150,
      backoff_max_ms: current?.backoff_max_ms ?? 600,
    };
  }
  if (assetId === 'mac-tdma') {
    return {
      protocol,
      slot_duration_ms: current?.slot_duration_ms ?? 800,
      guard_ms: current?.guard_ms ?? current?.guard_time_ms ?? 50,
    };
  }
  if (assetId === 'mac-polling') {
    return {
      protocol,
      poll_interval_s: current?.poll_interval_s ?? 2.0,
      guard_ms: current?.guard_ms ?? current?.guard_time_ms ?? 50,
    };
  }
  if (assetId === 'mac-no-mac') {
    return { protocol };
  }
  return {
    protocol,
    backoff_min_ms: current?.backoff_min_ms ?? 250,
    backoff_max_ms: current?.backoff_max_ms ?? 1000,
  };
}

function buildRoutingConfigFromAssetId(assetId: string, current?: DemoNode['routing']): NonNullable<DemoNode['routing']> {
  const protocol = ROUTING_ASSET_TO_PROTOCOL[assetId] ?? current?.protocol ?? 'static';
  if (assetId === 'routing-flooding') {
    return {
      protocol,
      ttl: current?.ttl ?? 10,
    };
  }
  if (assetId === 'routing-aodv') {
    return {
      protocol,
      hello_interval_s: current?.hello_interval_s ?? 1.0,
      route_timeout_s: current?.route_timeout_s ?? 3.0,
    };
  }
  if (assetId === 'routing-olsr') {
    return {
      protocol,
      hello_interval_s: current?.hello_interval_s ?? 2.0,
      tc_interval_s: current?.tc_interval_s ?? 5.0,
    };
  }
  return {
    protocol,
    next_hop: current?.next_hop,
  };
}

/* ─────────── Helper components ─────────── */

function SectionTitle({ label }: { label: string }) {
  return (
    <div style={{ fontSize: 11, fontWeight: 600, color: '#94a3b8', marginTop: 12, marginBottom: 4, letterSpacing: 0.3 }}>
      {label}
    </div>
  );
}

function BuilderStageHeader({
  phase,
  title,
  description,
  status,
  tone = 'info',
}: {
  phase: string;
  title: string;
  description: string;
  status?: string;
  tone?: 'ready' | 'pending' | 'info';
}) {
  return (
    <div className="environment-stage__header">
      <div className="environment-stage__body">
        <div className="environment-stage__badge">{phase}</div>
        <div className="environment-stage__title">{title}</div>
        <div className="environment-stage__description">{description}</div>
      </div>
      {status && <div className={`environment-stage__status environment-stage__status--${tone}`}>{status}</div>}
    </div>
  );
}

function FieldRow({
  label,
  hint,
  description,
  warning,
  children,
}: {
  label: string;
  hint?: string;
  description?: string;
  warning?: string | null;
  children: ReactNode;
}) {
  return (
    <label style={{ display: 'grid', gap: 2 }}>
      <span style={{ fontSize: 11, color: 'var(--text-dim)' }}>
        {label}
        {hint && <span style={{ fontSize: 10, color: '#607080', marginLeft: 4 }}>({hint})</span>}
      </span>
      {children}
      {description && <span style={{ fontSize: 10, color: '#607080', lineHeight: 1.4 }}>{description}</span>}
      {warning && <span style={{ fontSize: 10, color: '#fbbf24', lineHeight: 1.4 }}>{warning}</span>}
    </label>
  );
}

function NumInput({ value, onChange, min, max, step, disabled }: { value: number; onChange: (v: number) => void; min?: number; max?: number; step?: number; disabled?: boolean }) {
  return (
    <input
      type="number"
      value={value}
      min={min}
      max={max}
      step={step}
      disabled={disabled}
      onChange={(e) => onChange(Number(e.target.value))}
      style={{ ...selectStyle, MozAppearance: 'textfield', opacity: disabled ? 0.45 : 1, cursor: disabled ? 'not-allowed' : undefined }}
    />
  );
}

function TabBar({ tabs, active, onChange }: { tabs: { key: string; label: string }[]; active: string; onChange: (key: string) => void }) {
  return (
    <div style={{ display: 'flex', gap: 4, marginBottom: 8 }}>
      {tabs.map((tab) => (
        <button
          key={tab.key}
          type="button"
          onClick={() => onChange(tab.key)}
          style={{
            flex: 1, padding: '4px 0', borderRadius: 6, fontSize: 11, cursor: 'pointer',
            border: '1px solid',
            background: active === tab.key ? 'rgba(34,211,238,0.12)' : 'transparent',
            borderColor: active === tab.key ? 'rgba(34,211,238,0.3)' : 'rgba(148,163,184,0.15)',
            color: active === tab.key ? 'var(--accent)' : 'var(--text-dim)',
          }}
        >
          {tab.label}
        </button>
      ))}
    </div>
  );
}

function Select({ value, onChange, options }: { value: string; onChange: (v: string) => void; options: { value: string; label: string }[] }) {
  return (
    <select value={value} onChange={(e) => onChange(e.target.value)} style={selectStyle}>
      {options.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
    </select>
  );
}

function AssetButtons({ assets, activeId, onSelect }: { assets: ModelLibraryAsset[]; activeId?: string; onSelect: (asset: ModelLibraryAsset) => void }) {
  if (assets.length === 0) return null;
  return (
    <div style={{ display: 'grid', gridTemplateColumns: `repeat(${Math.min(assets.length, 3)}, 1fr)`, gap: 4 }}>
      {assets.map((asset) => {
        const active = asset.id === activeId;
        return (
          <button
            key={asset.id}
            type="button"
            onClick={() => onSelect(asset)}
            style={{
              padding: '8px 6px', borderRadius: 10, fontSize: 11, cursor: 'pointer', textAlign: 'center',
              border: '1px solid', lineHeight: 1.4,
              background: active ? 'rgba(34,211,238,0.15)' : 'rgba(2,10,19,0.5)',
              borderColor: active ? 'rgba(34,211,238,0.4)' : 'rgba(148,163,184,0.18)',
              color: active ? 'var(--accent)' : 'var(--text-dim)',
            }}
          >
            <div style={{ fontWeight: 600, color: active ? '#eff7ff' : '#94a3b8' }}>{asset.name}</div>
            {asset.summary && <div style={{ fontSize: 10, marginTop: 2 }}>{asset.summary}</div>}
          </button>
        );
      })}
    </div>
  );
}

function SspEditor({ data, onSave }: { data: number[][]; onSave: (rows: number[][]) => void }) {
  const [rows, setRows] = useState<number[][]>(data);
  useEffect(() => setRows(data), [data]);
  return (
    <div style={{ maxHeight: 200, overflowY: 'auto', border: '1px solid rgba(148,163,184,0.12)', borderRadius: 8, padding: 4 }}>
      <table style={{ width: '100%', fontSize: 10, borderCollapse: 'collapse' }}>
        <thead>
          <tr style={{ color: '#94a3b8' }}>
            <th style={{ textAlign: 'left', padding: '2px 4px' }}>深度 (m)</th>
            <th style={{ textAlign: 'left', padding: '2px 4px' }}>声速 (m/s)</th>
          </tr>
        </thead>
        <tbody>
          {rows.map((row, i) => (
            <tr key={i}>
              <td style={{ padding: '1px 4px' }}>
                <input type="number" value={row[0]} style={{ width: '100%', background: 'transparent', border: 'none', color: '#eff7ff', fontSize: 10 }} onChange={(e) => { const next = [...rows]; next[i] = [Number(e.target.value), row[1]]; setRows(next); }} />
              </td>
              <td style={{ padding: '1px 4px' }}>
                <input type="number" value={row[1]} style={{ width: '100%', background: 'transparent', border: 'none', color: '#eff7ff', fontSize: 10 }} onChange={(e) => { const next = [...rows]; next[i] = [row[0], Number(e.target.value)]; setRows(next); }} />
              </td>
            </tr>
          ))}
        </tbody>
      </table>
      <button type="button" onClick={() => onSave(rows)} style={{ ...smallBtnStyle, width: '100%', marginTop: 4, padding: '4px 0' }}>保存修改</button>
    </div>
  );
}

function SspChart({ data }: { data: number[][] }) {
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const width = 200;
  const height = 120;
  const pad = 30;
  if (!data || data.length < 2) return null;
  const depths = data.map((r) => r[0]);
  const speeds = data.map((r) => r[1]);
  const minD = Math.min(...depths);
  const maxD = Math.max(...depths);
  const minS = Math.min(...speeds);
  const maxS = Math.max(...speeds);
  const rangeD = maxD - minD || 1;
  const rangeS = maxS - minS || 1;
  const pointList = data.map((r, index) => {
    const x = pad + ((r[1] - minS) / rangeS) * (width - pad * 2);
    const y = pad + ((r[0] - minD) / rangeD) * (height - pad * 2);
    return { x, y, depth: r[0], speed: r[1], index };
  });
  const points = pointList.map((point) => `${point.x},${point.y}`).join(' ');
  const hoveredPoint = hoveredIndex != null ? pointList[hoveredIndex] : null;
  return (
    <svg viewBox={`0 0 ${width} ${height}`} style={{ width: '100%', maxHeight: 120, marginTop: 6 }} onMouseLeave={() => setHoveredIndex(null)}>
      <polyline points={points} fill="none" stroke="var(--accent)" strokeWidth={1.5} />
      {pointList.map((point) => (
        <g key={`${point.depth}-${point.speed}-${point.index}`} onMouseEnter={() => setHoveredIndex(point.index)}>
          <circle cx={point.x} cy={point.y} r={hoveredIndex === point.index ? 3.2 : 2.2} fill={hoveredIndex === point.index ? '#fbbf24' : '#22d3ee'} />
          <title>{`深度 ${point.depth} m / 声速 ${point.speed} m/s`}</title>
        </g>
      ))}
      <text x={width / 2} y={height - 4} textAnchor="middle" fontSize={8} fill="#607080">声速 (m/s)</text>
      <text x={4} y={height / 2} textAnchor="middle" fontSize={8} fill="#607080" transform={`rotate(-90, 4, ${height / 2})`}>深度 (m)</text>
      {hoveredPoint && (
        <g>
          <rect x={width - 86} y={6} width={80} height={24} rx={5} fill="rgba(2,10,19,0.92)" stroke="rgba(251,191,36,0.45)" />
          <text x={width - 46} y={16} textAnchor="middle" fontSize={7} fill="#fde68a">{`${hoveredPoint.depth} m`}</text>
          <text x={width - 46} y={25} textAnchor="middle" fontSize={7} fill="#cbd5e1">{`${hoveredPoint.speed} m/s`}</text>
        </g>
      )}
    </svg>
  );
}

function BathymetryChart({ data }: { data: { range_m: number[]; depth_m: number[] } }) {
  const [hoveredIndex, setHoveredIndex] = useState<number | null>(null);
  const width = 200;
  const height = 100;
  const pad = 30;
  if (!data || !data.range_m || data.range_m.length < 2) return null;
  const minR = Math.min(...data.range_m);
  const maxR = Math.max(...data.range_m);
  const minDp = Math.min(...data.depth_m);
  const maxDp = Math.max(...data.depth_m);
  const rangeR = maxR - minR || 1;
  const rangeDp = maxDp - minDp || 1;
  const pointList = data.range_m.map((r, i) => {
    const x = pad + ((r - minR) / rangeR) * (width - pad * 2);
    const y = pad + ((data.depth_m[i] - minDp) / rangeDp) * (height - pad * 2);
    return { x, y, range: r, depth: data.depth_m[i], index: i };
  });
  const points = pointList.map((point) => `${point.x},${point.y}`).join(' ');
  const hoveredPoint = hoveredIndex != null ? pointList[hoveredIndex] : null;
  return (
    <svg viewBox={`0 0 ${width} ${height}`} style={{ width: '100%', maxHeight: 100, marginTop: 6 }} onMouseLeave={() => setHoveredIndex(null)}>
      <polyline points={points} fill="none" stroke="#22d3ee" strokeWidth={1.5} />
      {pointList.map((point) => (
        <g key={`${point.range}-${point.depth}-${point.index}`} onMouseEnter={() => setHoveredIndex(point.index)}>
          <circle cx={point.x} cy={point.y} r={hoveredIndex === point.index ? 3.2 : 2.2} fill={hoveredIndex === point.index ? '#fbbf24' : '#22d3ee'} />
          <title>{`距离 ${point.range} m / 深度 ${point.depth} m`}</title>
        </g>
      ))}
      <text x={width / 2} y={height - 4} textAnchor="middle" fontSize={8} fill="#607080">距离 (m)</text>
      <text x={4} y={height / 2} textAnchor="middle" fontSize={8} fill="#607080" transform={`rotate(-90, 4, ${height / 2})`}>深度 (m)</text>
      {hoveredPoint && (
        <g>
          <rect x={width - 92} y={6} width={86} height={24} rx={5} fill="rgba(2,10,19,0.92)" stroke="rgba(251,191,36,0.45)" />
          <text x={width - 49} y={16} textAnchor="middle" fontSize={7} fill="#fde68a">{`${Math.round(hoveredPoint.range)} m`}</text>
          <text x={width - 49} y={25} textAnchor="middle" fontSize={7} fill="#cbd5e1">{`${Math.round(hoveredPoint.depth)} m`}</text>
        </g>
      )}
    </svg>
  );
}

/* ─────────── BellhopParamsPanel ─────────── */

interface BellhopPanelProps {
  workingScenario: DemoScenario;
  gridFiles: string[];
  bathymetryFiles: string[];
  environmentDatabases: EnvironmentDatabase[];
  onUpdateTransmissionParam: (field: string, value: string) => void;
  onRefreshDataFiles: () => void;
  onSelectEnvironmentDatabase: (databaseId: string) => void;
  mode?: 'full' | 'selector' | 'builder';
}

export function BellhopParamsPanel({
  workingScenario,
  gridFiles,
  bathymetryFiles,
  environmentDatabases,
  onUpdateTransmissionParam,
  onRefreshDataFiles,
  onSelectEnvironmentDatabase,
  mode = 'full',
}: BellhopPanelProps) {
  const p = workingScenario.transmission.params;
  const selectedEnvironmentDatabaseId = String(p.environment_database_id ?? '');
  const selectedEnvironmentDatabase = environmentDatabases.find((item) => item.id === selectedEnvironmentDatabaseId) ?? null;
  const selectedEnvironmentBuildMode = selectedEnvironmentDatabase?.metadata?.build_mode === 'analytical' ? 'analytical' : 'bellhop';
  const scenarioId = workingScenario.scenario_metadata?.scenario_id || 'scene';
  const showOfflineBuilder = mode !== 'selector';

  const [sspTab, setSspTab] = useState<string>('formula');
  const [sspFiles, setSspFiles] = useState<string[]>([]);
  const [sspData, setSspData] = useState<number[][] | null>(null);
  const [sspUploadBusy, setSspUploadBusy] = useState(false);
  const sspFileRef = useRef<HTMLInputElement>(null);
  const [sspFormula, setSspFormula] = useState<string>('munk');
  const [sspFormulaName, setSspFormulaName] = useState('');
  const [sspDepthMax, setSspDepthMax] = useState(200);
  const [sspC0, setSspC0] = useState(1500);
  const [sspGradient, setSspGradient] = useState(-0.017);
  const [sspTcDepth, setSspTcDepth] = useState(50);
  const [sspTcThick, setSspTcThick] = useState(30);
  const [sspSurfSpeed, setSspSurfSpeed] = useState(1540);
  const [sspDeepSpeed, setSspDeepSpeed] = useState(1500);
  const [sspFormulaBusy, setSspFormulaBusy] = useState(false);

  const [bathTab, setBathTab] = useState<string>('preset');
  const [bathName, setBathName] = useState('');
  const [bathRange, setBathRange] = useState(10000);
  const [bathDepth, setBathDepth] = useState(100);
  const [bathProfile, setBathProfile] = useState('ridge');
  const [bathFeatureRange, setBathFeatureRange] = useState(5000);
  const [bathFeatureHeight, setBathFeatureHeight] = useState(75);
  const [bathBusy, setBathBusy] = useState(false);
  const [bathImportBusy, setBathImportBusy] = useState(false);
  const bathFileRef = useRef<HTMLInputElement>(null);
  const [bathData, setBathData] = useState<{ range_m: number[]; depth_m: number[] } | null>(null);
  const [bathStatus, setBathStatus] = useState<string | null>(null);

  const [gridRange, setGridRange] = useState(10000);
  const [gridDepth, setGridDepth] = useState(100);
  const [gridFreq, setGridFreq] = useState(12);
  const [builderSspFile, setBuilderSspFile] = useState('');
  const [builderBathymetryFile, setBuilderBathymetryFile] = useState('');
  const [builderGridFile, setBuilderGridFile] = useState('');
  const [databaseName, setDatabaseName] = useState('');
  const [databaseDescription, setDatabaseDescription] = useState('');
  const [bellhopExe, setBellhopExe] = useState('');
  const [bellhopBusy, setBellhopBusy] = useState(false);
  const [bellhopResult, setBellhopResult] = useState<string | null>(null);
  const [bellhopError, setBellhopError] = useState<string | null>(null);
  const [bellhopWarnings, setBellhopWarnings] = useState<string[]>([]);
  const [lastCreatedEnvironmentDatabaseId, setLastCreatedEnvironmentDatabaseId] = useState<string | null>(null);
  const [pipelineMode, setPipelineMode] = useState<'analytical' | 'bellhop'>('analytical');
  const [environmentCapabilities, setEnvironmentCapabilities] = useState<EnvironmentCapabilities | null>(null);
  const [wossSources, setWossSources] = useState<WossSourceProfile[]>([]);
  const [selectedWossSourceId, setSelectedWossSourceId] = useState('');
  const [selectedWossProfileId, setSelectedWossProfileId] = useState('');
  const [wossName, setWossName] = useState('');
  const [wossProfileName, setWossProfileName] = useState('');
  const [wossProfileDescription, setWossProfileDescription] = useState('');
  const [wossDescription, setWossDescription] = useState('');
  const [wossLatitude, setWossLatitude] = useState(18.0);
  const [wossLongitude, setWossLongitude] = useState(114.0);
  const [wossTransectBearing, setWossTransectBearing] = useState(90);
  const [wossRegion, setWossRegion] = useState('');
  const [wossMonth, setWossMonth] = useState(4);
  const [wossSeason, setWossSeason] = useState('spring');
  const [wossWoaVersion, setWossWoaVersion] = useState('2023');
  const [wossGebcoVersion, setWossGebcoVersion] = useState('2020-public-api');
  const [wossDeck41Version, setWossDeck41Version] = useState('legacy');
  const [wossNotes, setWossNotes] = useState('');
  const [wossProfilesDraft, setWossProfilesDraft] = useState<WossSourceVariant[]>([]);
  const [wossBatchNamePrefix, setWossBatchNamePrefix] = useState('');
  const [wossAllowOverwrite, setWossAllowOverwrite] = useState(false);
  const [wossBusy, setWossBusy] = useState(false);
  const [wossStatus, setWossStatus] = useState<string | null>(null);

  const draftBathymetryPreview = useMemo(() => {
    const maxRange = Math.max(100, bathRange);
    const baseDepth = Math.max(1, bathDepth);
    const ranges = Array.from({ length: 20 }, (_, index) => Number(((maxRange * index) / 19).toFixed(1)));
    const depths = ranges.map((range) => {
      if (bathProfile === 'flat') return baseDepth;
      if (bathProfile === 'slope') return Number((baseDepth + (range / maxRange) * bathFeatureHeight).toFixed(2));
      if (bathProfile === 'trench') {
        const sigma = Math.max(1, maxRange * 0.08);
        return Number((baseDepth + bathFeatureHeight * Math.exp(-((range - bathFeatureRange) ** 2) / (2 * sigma ** 2))).toFixed(2));
      }
      const sigma = Math.max(1, maxRange * 0.06);
      return Number((baseDepth - bathFeatureHeight * Math.exp(-((range - bathFeatureRange) ** 2) / (2 * sigma ** 2))).toFixed(2));
    });
    return { range_m: ranges, depth_m: depths };
  }, [bathDepth, bathFeatureHeight, bathFeatureRange, bathProfile, bathRange]);

  const selectedWossSource = useMemo(
    () => wossSources.find((item) => item.id === selectedWossSourceId) ?? null,
    [selectedWossSourceId, wossSources],
  );

  const selectedWossProfile = useMemo(
    () => selectedWossSource?.profiles.find((item) => item.id === selectedWossProfileId) ?? null,
    [selectedWossProfileId, selectedWossSource],
  );

  const effectiveWossArtifacts = selectedWossProfile?.artifacts ?? selectedWossSource?.artifacts ?? {};
  const effectiveWossLocation = selectedWossProfile?.location ?? selectedWossSource?.location ?? {};
  const effectiveWossTimeReference = selectedWossProfile?.time_reference ?? selectedWossSource?.time_reference ?? {};
  const effectiveWossDatasets = selectedWossProfile?.datasets ?? selectedWossSource?.datasets ?? {};
  const effectiveWossCache = selectedWossProfile?.cache ?? selectedWossSource?.cache ?? null;
  const resolvedWossBatchPrefix = wossBatchNamePrefix.trim() || databaseName.trim() || selectedWossSource?.id || '';
  const normalizedWossMonth = wossMonth >= 1 && wossMonth <= 12 ? wossMonth : undefined;
  const parsedWaterDepth = Number(p.water_depth_m);
  const hasExplicitWaterDepth = Number.isFinite(parsedWaterDepth) && parsedWaterDepth > 0;
  const coverageAnalysis = useMemo(
    () => buildEnvironmentCoverageSummary({
      sspData,
      bathData,
      gridRange,
      gridDepth,
      waterDepth: hasExplicitWaterDepth ? parsedWaterDepth : gridDepth,
    }),
    [bathData, gridDepth, gridRange, hasExplicitWaterDepth, parsedWaterDepth, sspData],
  );
  const autoManagedCoverageSource = Boolean(
    builderSspFile
    || builderBathymetryFile
    || selectedWossSourceId
    || effectiveWossArtifacts.ssp_file
    || effectiveWossArtifacts.bathymetry_file,
  );
  const autoManagedRange = autoManagedCoverageSource && coverageAnalysis.recommendedRange != null;
  const autoManagedDepth = autoManagedCoverageSource && coverageAnalysis.recommendedDepth != null;
  const effectiveGridRange = autoManagedRange ? Number(coverageAnalysis.recommendedRange) : gridRange;
  const effectiveGridDepth = autoManagedDepth ? Number(coverageAnalysis.recommendedDepth) : gridDepth;
  const effectiveWaterDepth = autoManagedDepth ? effectiveGridDepth : (hasExplicitWaterDepth ? parsedWaterDepth : effectiveGridDepth);
  const suggestedDatabaseDescription = useMemo(
    () => buildSuggestedEnvironmentDescription({
      pipelineMode,
      gridRange: effectiveGridRange,
      gridDepth: effectiveGridDepth,
      gridFreq,
      wossRegion,
      wossLatitude,
      wossLongitude,
      wossMonth,
      wossSeason,
      builderSspFile,
      builderBathymetryFile,
      selectedWossSourceName: selectedWossSource?.name ?? null,
    }),
    [builderBathymetryFile, builderSspFile, effectiveGridDepth, effectiveGridRange, gridFreq, pipelineMode, selectedWossSource?.name, wossLatitude, wossLongitude, wossMonth, wossRegion, wossSeason],
  );
  const bellhopCandidates = environmentCapabilities?.bellhop_candidates ?? [];

  useEffect(() => {
    if (selectedWossProfileId && selectedWossSource && !selectedWossProfile) {
      setSelectedWossProfileId('');
    }
  }, [selectedWossProfile, selectedWossProfileId, selectedWossSource]);

  useEffect(() => {
    fetchDataFiles('ssp').then(setSspFiles).catch(() => {});
    fetchEnvironmentCapabilities().then(setEnvironmentCapabilities).catch(() => {});
    fetchWossSources().then(setWossSources).catch(() => {});
  }, []);

  useEffect(() => {
    if (!bellhopExe.trim() && environmentCapabilities?.bellhop?.resolved_path) {
      setBellhopExe(environmentCapabilities.bellhop.resolved_path);
    }
  }, [bellhopExe, environmentCapabilities?.bellhop?.resolved_path]);

  useEffect(() => {
    if (!builderSspFile) { setSspData(null); return; }
    setSspData(null);
    fetchSspData(builderSspFile).then(setSspData).catch(() => setSspData(null));
  }, [builderSspFile]);

  useEffect(() => {
    if (!builderBathymetryFile) { setBathData(null); return; }
    setBathData(null);
    fetchBathymetry(builderBathymetryFile).then((d) => setBathData(d as { range_m: number[]; depth_m: number[] } | null)).catch(() => setBathData(null));
  }, [builderBathymetryFile]);

  useEffect(() => {
    if (mode === 'selector') {
      setBuilderSspFile(selectedEnvironmentDatabase?.artifacts.ssp_file ?? String(p.ssp_file ?? ''));
      setBuilderBathymetryFile(selectedEnvironmentDatabase?.artifacts.bathymetry_file ?? String(p.bathymetry_file ?? ''));
      setBuilderGridFile(selectedEnvironmentDatabase?.artifacts.grid_file ?? String(p.grid_file ?? ''));
      setSelectedWossSourceId(selectedEnvironmentDatabase?.metadata?.woss_source_id ?? '');
      setSelectedWossProfileId(selectedEnvironmentDatabase?.metadata?.woss_profile_id ?? '');
      setLastCreatedEnvironmentDatabaseId(null);
      setBellhopWarnings(selectedEnvironmentDatabase?.metadata?.warnings ?? []);
      return;
    }

    setBuilderSspFile(String(p.ssp_file ?? ''));
    setBuilderBathymetryFile(String(p.bathymetry_file ?? ''));
    setBuilderGridFile(String(p.grid_file ?? ''));
    setDatabaseName(`${scenarioId}_env`);
    setDatabaseDescription('');
    setSelectedWossSourceId('');
    setSelectedWossProfileId('');
    setWossProfilesDraft([]);
    setWossBatchNamePrefix('');
    setWossAllowOverwrite(false);
    setLastCreatedEnvironmentDatabaseId(null);
    setBellhopWarnings([]);
  }, [mode, selectedEnvironmentDatabase, p.bathymetry_file, p.grid_file, p.ssp_file, scenarioId]);

  useEffect(() => {
    if (mode !== 'builder' || !selectedEnvironmentDatabase) return;

    const buildMode = selectedEnvironmentDatabase.metadata.build_mode === 'analytical' ? 'analytical' : 'bellhop';
    const initialGridRange = Number(
      buildMode === 'bellhop'
        ? (selectedEnvironmentDatabase.metadata.recommendations?.bathymetry_range_max_m
          ?? selectedEnvironmentDatabase.metadata.recommendations?.recommended_range_max_m
          ?? selectedEnvironmentDatabase.build.range_max_m
          ?? 10000)
        : (selectedEnvironmentDatabase.build.range_max_m ?? 10000),
    );
    const initialGridDepth = Number(
      buildMode === 'bellhop'
        ? (selectedEnvironmentDatabase.metadata.recommendations?.bathymetry_depth_max_m
          ?? selectedEnvironmentDatabase.metadata.recommendations?.recommended_depth_max_m
          ?? selectedEnvironmentDatabase.build.depth_max_m
          ?? 100)
        : (selectedEnvironmentDatabase.build.depth_max_m ?? 100),
    );

    setBuilderSspFile(selectedEnvironmentDatabase.artifacts.ssp_file ?? '');
    setBuilderBathymetryFile(selectedEnvironmentDatabase.artifacts.bathymetry_file ?? '');
    setBuilderGridFile(selectedEnvironmentDatabase.artifacts.grid_file ?? '');
    setDatabaseName(selectedEnvironmentDatabase.name);
    setDatabaseDescription(selectedEnvironmentDatabase.description ?? '');
    setGridRange(initialGridRange);
    setGridDepth(initialGridDepth);
    setGridFreq(Number(((selectedEnvironmentDatabase.build.frequency_hz ?? 12000) / 1000).toFixed(1)));
    setPipelineMode(buildMode);
    setBellhopExe(selectedEnvironmentDatabase.metadata.engine_path ?? environmentCapabilities?.bellhop?.resolved_path ?? '');
    setSelectedWossSourceId(selectedEnvironmentDatabase.metadata.woss_source_id ?? '');
    setSelectedWossProfileId(selectedEnvironmentDatabase.metadata.woss_profile_id ?? '');
    setBellhopWarnings(selectedEnvironmentDatabase.metadata.warnings ?? []);
    setLastCreatedEnvironmentDatabaseId(selectedEnvironmentDatabase.id);

    if (buildMode === 'bellhop' && Number.isFinite(initialGridDepth) && initialGridDepth > 0) {
      onUpdateTransmissionParam('water_depth_m', String(initialGridDepth));
    } else if (selectedEnvironmentDatabase.build.water_depth_m != null) {
      onUpdateTransmissionParam('water_depth_m', String(selectedEnvironmentDatabase.build.water_depth_m));
    }
    if (buildMode === 'analytical' && selectedEnvironmentDatabase.build.sound_speed_mps != null) onUpdateTransmissionParam('sound_speed_mps', String(selectedEnvironmentDatabase.build.sound_speed_mps));
    if (buildMode === 'analytical' && selectedEnvironmentDatabase.build.absorption_db_per_km != null) onUpdateTransmissionParam('absorption_db_per_km', String(selectedEnvironmentDatabase.build.absorption_db_per_km));
    if (buildMode === 'analytical' && selectedEnvironmentDatabase.build.spreading_factor != null) onUpdateTransmissionParam('spreading_factor', String(selectedEnvironmentDatabase.build.spreading_factor));
    if (buildMode === 'bellhop' && selectedEnvironmentDatabase.build.max_bounces != null) onUpdateTransmissionParam('max_bounces', String(selectedEnvironmentDatabase.build.max_bounces));
    if (selectedEnvironmentDatabase.build.source_level_db != null) onUpdateTransmissionParam('source_level_db', String(selectedEnvironmentDatabase.build.source_level_db));
  }, [environmentCapabilities?.bellhop?.resolved_path, mode, onUpdateTransmissionParam, selectedEnvironmentDatabase]);

  useEffect(() => {
    if (!autoManagedRange || coverageAnalysis.recommendedRange == null) return;
    const nextRange = coverageAnalysis.recommendedRange;
    setGridRange((current) => (current === nextRange ? current : nextRange));
  }, [autoManagedRange, coverageAnalysis.recommendedRange]);

  useEffect(() => {
    if (!autoManagedDepth || coverageAnalysis.recommendedDepth == null) return;
    const nextDepth = coverageAnalysis.recommendedDepth;
    setGridDepth((current) => (current === nextDepth ? current : nextDepth));
  }, [autoManagedDepth, coverageAnalysis.recommendedDepth]);

  useEffect(() => {
    if (autoManagedDepth && coverageAnalysis.recommendedDepth != null) {
      if (parsedWaterDepth !== coverageAnalysis.recommendedDepth) {
        onUpdateTransmissionParam('water_depth_m', String(coverageAnalysis.recommendedDepth));
      }
      return;
    }
    if (!hasExplicitWaterDepth && effectiveGridDepth > 0) {
      onUpdateTransmissionParam('water_depth_m', String(effectiveGridDepth));
    }
  }, [autoManagedDepth, coverageAnalysis.recommendedDepth, effectiveGridDepth, hasExplicitWaterDepth, onUpdateTransmissionParam, parsedWaterDepth]);

  async function handleSaveSsp(rows: number[][]) {
    if (!builderSspFile) return;
    try {
      await saveSsp(builderSspFile, rows);
      fetchSspData(builderSspFile).then(setSspData).catch(() => {});
    } catch { /* ignore */ }
  }

  async function refreshSspFiles(selectPath?: string) {
    const newFiles = await fetchDataFiles('ssp');
    setSspFiles(newFiles);
    if (selectPath) setBuilderSspFile(selectPath);
  }

  async function handleUploadSsp(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setSspUploadBusy(true);
    try {
      const res = await uploadSsp(file);
      if (res?.path) await refreshSspFiles(res.path);
    } catch { /* ignore */ } finally {
      setSspUploadBusy(false);
      if (sspFileRef.current) sspFileRef.current.value = '';
    }
  }

  async function handleGenerateSsp() {
    if (!sspFormulaName.trim()) return;
    setSspFormulaBusy(true);
    try {
      const params: GenerateSspParams = {
        name: sspFormulaName.trim(),
        formula: sspFormula as GenerateSspParams['formula'],
        depth_max: sspDepthMax,
        c0: sspC0,
        step: 5,
      };
      if (sspFormula === 'linear_gradient') params.gradient = sspGradient;
      if (sspFormula === 'thermocline') {
        params.thermocline_depth = sspTcDepth;
        params.thermocline_thickness = sspTcThick;
        params.surface_speed = sspSurfSpeed;
        params.deep_speed = sspDeepSpeed;
      }
      const res = await generateSsp(params);
      if (res?.path) {
        await refreshSspFiles(res.path);
        setSspFormulaName('');
      }
    } catch { /* ignore */ } finally {
      setSspFormulaBusy(false);
    }
  }

  async function handleCreateBath() {
    if (!bathName.trim()) return;
    setBathBusy(true);
    setBathStatus(null);
    try {
      const path = await generateBathymetry({
        name: bathName.trim(),
        max_range_m: bathRange,
        base_depth_m: bathDepth,
        profile: bathProfile as GenerateBathymetryParams['profile'],
        feature_range_m: bathFeatureRange,
        feature_height_m: bathFeatureHeight,
      });
      if (path) {
        onRefreshDataFiles();
        setBuilderBathymetryFile(path);
        setBathName('');
        setBathStatus(`✓ 地形草稿已生成: ${path.split('/').pop()}`);
      }
    } catch {
      setBathStatus('✕ 地形生成失败，请检查参数。');
    } finally {
      setBathBusy(false);
    }
  }

  async function handleImportBath(e: React.ChangeEvent<HTMLInputElement>) {
    const file = e.target.files?.[0];
    if (!file) return;
    setBathImportBusy(true);
    try {
      const res = await importBathymetry(file);
      if (res?.path) {
        onRefreshDataFiles();
        setBuilderBathymetryFile(res.path);
      }
    } catch { /* ignore */ } finally {
      setBathImportBusy(false);
      if (bathFileRef.current) bathFileRef.current.value = '';
    }
  }

  async function refreshWossSourceList(selectId?: string) {
    const items = await fetchWossSources();
    setWossSources(items);
    if (selectId !== undefined) {
      setSelectedWossSourceId(selectId);
    }
  }

  async function handleDeleteWossSource() {
    if (!selectedWossSource) {
      setWossStatus('✕ 请先选择要删除的 WOSS source。');
      return;
    }
    if (!confirm(`确认删除 WOSS source「${selectedWossSource.name}」？`)) {
      return;
    }
    setWossBusy(true);
    setWossStatus(null);
    try {
      await deleteWossSource(selectedWossSource.id);
      await refreshWossSourceList('');
      setSelectedWossSourceId('');
      setSelectedWossProfileId('');
      setWossStatus(`✓ 已删除 WOSS source ${selectedWossSource.name}。`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  function handleLoadWossSourceIntoDraft(source: WossSourceProfile) {
    setWossName(source.name);
    setWossDescription(source.description || '');
    setBuilderSspFile(source.artifacts.ssp_file || '');
    setBuilderBathymetryFile(source.artifacts.bathymetry_file || '');
    setWossLatitude(source.location.latitude_deg ?? 18);
    setWossLongitude(source.location.longitude_deg ?? 114);
    setWossTransectBearing(parseTransectBearingFromLabel(source.location.transect_label) ?? 90);
    setWossRegion(source.location.region || '');
    setWossMonth(source.time_reference.month ?? 0);
    setWossSeason(source.time_reference.season || '');
    setWossWoaVersion(source.datasets.woa || '');
    setWossGebcoVersion(source.datasets.gebco || '');
    setWossDeck41Version(source.datasets.deck41 || '');
    setWossNotes(source.notes || '');
    setWossProfilesDraft(source.profiles ?? []);
    setWossBatchNamePrefix(source.name);
    setWossStatus(`✓ 已将 WOSS source ${source.name} 载入编辑草稿。`);
  }

  function handleLoadWossProfileIntoDraft(profile: WossSourceVariant) {
    setWossProfileName(profile.name || profile.id);
    setWossProfileDescription(profile.description || '');
    setBuilderSspFile(profile.artifacts.ssp_file || '');
    setBuilderBathymetryFile(profile.artifacts.bathymetry_file || '');
    setWossLatitude(profile.location.latitude_deg ?? 18);
    setWossLongitude(profile.location.longitude_deg ?? 114);
    setWossTransectBearing(parseTransectBearingFromLabel(profile.location.transect_label) ?? 90);
    setWossRegion(profile.location.region || '');
    setWossMonth(profile.time_reference.month ?? 0);
    setWossSeason(profile.time_reference.season || '');
    setWossWoaVersion(profile.datasets.woa || '');
    setWossGebcoVersion(profile.datasets.gebco || '');
    setWossDeck41Version(profile.datasets.deck41 || '');
    setWossNotes(profile.notes || '');
    setWossStatus(`✓ 已载入 profile ${profile.name} 到编辑草稿。`);
  }

  async function handleImportCurrentWossDraft(targetKind: 'source' | 'profile') {
    const resolvedSourceName = wossName.trim() || selectedWossSource?.id || '';
    if (!resolvedSourceName) {
      setWossStatus('✕ 请先填写或选择 WOSS source 名称。');
      return;
    }
    if (!builderSspFile && !builderBathymetryFile) {
      setWossStatus('✕ 请先绑定 SSP 或地形文件，再导入缓存。');
      return;
    }

    const resolvedProfileName = wossProfileName.trim() || selectedWossProfile?.name || selectedWossProfile?.id || '';
    if (targetKind === 'profile' && !resolvedProfileName) {
      setWossStatus('✕ 导入时相剖面缓存前，请先填写剖面名称或选择已有剖面。');
      return;
    }

    setWossBusy(true);
    setWossStatus(null);
    try {
      const payload: ImportWossCacheIntoSourceParams = {
        source_name: resolvedSourceName,
        source_display_name: wossName.trim() || selectedWossSource?.name || resolvedSourceName,
        source_description: wossDescription.trim() || selectedWossSource?.description || undefined,
        target_kind: targetKind,
        profile_name: targetKind === 'profile' ? resolvedProfileName : undefined,
        profile_description: targetKind === 'profile'
          ? (wossProfileDescription.trim() || selectedWossProfile?.description || undefined)
          : undefined,
        ssp_file: builderSspFile || undefined,
        bathymetry_file: builderBathymetryFile || undefined,
        location: {
          latitude_deg: wossLatitude,
          longitude_deg: wossLongitude,
          region: wossRegion.trim() || undefined,
          transect_label: `bearing_${wossTransectBearing.toFixed(1)}_range_${Math.round(effectiveGridRange)}m`,
        },
        time_reference: {
          month: normalizedWossMonth,
          season: wossSeason.trim() || undefined,
          label: targetKind === 'profile'
            ? `${resolvedProfileName} cached import`
            : `${resolvedSourceName} cached import`,
        },
        datasets: {
          woa: wossWoaVersion.trim(),
          gebco: wossGebcoVersion.trim(),
          deck41: wossDeck41Version.trim(),
        },
        notes: wossNotes.trim() || undefined,
      };
      const result = await importWossCacheIntoSource(payload);
      const importedSource = result.source;
      await refreshWossSourceList(importedSource.id);
      setSelectedWossSourceId(importedSource.id);
      setWossProfilesDraft(importedSource.profiles ?? []);
      setWossBatchNamePrefix(importedSource.name);
      handleLoadWossSourceIntoDraft(importedSource);
      if (result.profile_id) {
        setSelectedWossProfileId(result.profile_id);
        const importedProfile = importedSource.profiles.find((item) => item.id === result.profile_id) ?? null;
        if (importedProfile) {
          handleLoadWossProfileIntoDraft(importedProfile);
        }
      } else {
        setSelectedWossProfileId('');
      }
      const label = targetKind === 'profile'
        ? `profile ${resolvedProfileName}`
        : `source ${importedSource.name}`;
      setWossStatus(`✓ 已将当前 ${label} 资产导入缓存，并回写到 WOSS source ${importedSource.name}。`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  async function handleImportRealOceanData(targetKind: 'source' | 'profile') {
    const resolvedSourceName = wossName.trim() || selectedWossSource?.id || '';
    if (!resolvedSourceName) {
      setWossStatus('✕ 请先填写或选择 WOSS source 名称。');
      return;
    }

    const resolvedProfileName = wossProfileName.trim() || selectedWossProfile?.name || selectedWossProfile?.id || '';
    if (targetKind === 'profile' && !resolvedProfileName) {
      setWossStatus('✕ 导入时相剖面真实数据前，请先填写剖面名称或选择已有剖面。');
      return;
    }

    setWossBusy(true);
    setWossStatus(null);
    try {
      const payload: ImportWossRealDataIntoSourceParams = {
        source_name: resolvedSourceName,
        source_display_name: wossName.trim() || selectedWossSource?.name || resolvedSourceName,
        source_description: wossDescription.trim() || selectedWossSource?.description || undefined,
        target_kind: targetKind,
        profile_name: targetKind === 'profile' ? resolvedProfileName : undefined,
        profile_description: targetKind === 'profile'
          ? (wossProfileDescription.trim() || selectedWossProfile?.description || undefined)
          : undefined,
        latitude_deg: wossLatitude,
        longitude_deg: wossLongitude,
        region: wossRegion.trim() || undefined,
        month: normalizedWossMonth,
        season: wossSeason.trim() || undefined,
        datasets: {
          woa: wossWoaVersion.trim() || '2023',
          gebco: wossGebcoVersion.trim() || '2020-public-api',
          deck41: wossDeck41Version.trim(),
        },
        notes: wossNotes.trim() || undefined,
        range_max_m: effectiveGridRange,
        transect_bearing_deg: wossTransectBearing,
      };
      const result = await importWossRealDataIntoSource(payload);
      const importedSource = result.source;
      await refreshWossSourceList(importedSource.id);
      setSelectedWossSourceId(importedSource.id);
      setWossProfilesDraft(importedSource.profiles ?? []);
      setWossBatchNamePrefix(importedSource.name);
      if (result.generated_artifacts.ssp_file) setBuilderSspFile(result.generated_artifacts.ssp_file);
      if (result.generated_artifacts.bathymetry_file) setBuilderBathymetryFile(result.generated_artifacts.bathymetry_file);
      handleLoadWossSourceIntoDraft(importedSource);
      if (result.profile_id) {
        setSelectedWossProfileId(result.profile_id);
        const importedProfile = importedSource.profiles.find((item) => item.id === result.profile_id) ?? null;
        if (importedProfile) {
          handleLoadWossProfileIntoDraft(importedProfile);
        }
      } else {
        setSelectedWossProfileId('');
      }
      const label = targetKind === 'profile'
        ? `profile ${resolvedProfileName}`
        : `source ${importedSource.name}`;
      const importedDepthLabel = typeof result.real_data.ssp_depth_max_m === 'number'
        ? ` SSP 覆盖至 ${Math.round(result.real_data.ssp_depth_max_m)} m。`
        : '';
      setWossStatus(`✓ 已按坐标抓取 ${result.real_data.time_label} WOA23 + GEBCO2020，并导入 ${label}.${importedDepthLabel}`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  function handleUpsertWossProfileDraft() {
    const profileName = wossProfileName.trim();
    if (!profileName) {
      setWossStatus('✕ 请先填写时相剖面名称。');
      return;
    }
    if (!builderSspFile && !builderBathymetryFile) {
      setWossStatus('✕ profile 至少需要绑定 SSP 或地形文件。');
      return;
    }
    const profileId = sanitizeWossProfileId(profileName);
    if (!profileId) {
      setWossStatus('✕ 当前剖面名称无法转换为有效 ID。');
      return;
    }
    const profile: WossSourceVariant = {
      id: profileId,
      name: profileName,
      description: wossProfileDescription.trim(),
      artifacts: {
        ssp_file: builderSspFile || undefined,
        bathymetry_file: builderBathymetryFile || undefined,
      },
      location: {
        latitude_deg: wossLatitude,
        longitude_deg: wossLongitude,
        region: wossRegion.trim() || undefined,
        transect_label: `bearing_${wossTransectBearing.toFixed(1)}_range_${Math.round(effectiveGridRange)}m`,
      },
      time_reference: {
        month: normalizedWossMonth,
        season: wossSeason.trim() || undefined,
        label: `${profileName} profile`,
        timestamp_utc: undefined,
      },
      datasets: {
        woa: wossWoaVersion.trim(),
        gebco: wossGebcoVersion.trim(),
        deck41: wossDeck41Version.trim(),
      },
      notes: wossNotes.trim(),
    };
    setWossProfilesDraft((prev) => {
      const next = prev.filter((item) => item.id !== profileId);
      return [...next, profile];
    });
    setWossStatus(`✓ 时相剖面 ${profileName} 已加入 WOSS 来源草稿。`);
  }

  function handleRemoveWossProfileDraft(profileId: string) {
    setWossProfilesDraft((prev) => prev.filter((item) => item.id !== profileId));
    if (selectedWossProfileId === profileId) setSelectedWossProfileId('');
    setWossStatus(`✓ 已移除时相剖面 ${profileId}。`);
  }

  function handleChangeWossSource(sourceId: string) {
    setSelectedWossSourceId(sourceId);
    setSelectedWossProfileId('');
  }

  function handleApplyWossSource(source: WossSourceProfile, profile: WossSourceVariant | null = null) {
    const artifacts = profile?.artifacts ?? source.artifacts;
    if (artifacts.ssp_file) setBuilderSspFile(artifacts.ssp_file);
    if (artifacts.bathymetry_file) setBuilderBathymetryFile(artifacts.bathymetry_file);
    const label = profile ? `${source.name}/${profile.name}` : source.name;
    setWossStatus(`✓ 已应用 WOSS source ${label} 到当前离线构建草稿。`);
  }

  async function handleBuildEnvironmentFromSelectedWossSource() {
    if (!selectedWossSource) {
      setWossStatus('✕ 请先选择 WOSS source。');
      return;
    }
    const buildMode = pipelineMode === 'bellhop' ? 'bellhop' : 'analytical';
    const resolvedBellhopPath = bellhopExe.trim() || environmentCapabilities?.bellhop?.resolved_path || '';
    if (buildMode === 'bellhop' && !resolvedBellhopPath) {
      setWossStatus('✕ 当前模式需要可用的 Bellhop 引擎路径。');
      return;
    }
    if (buildMode === 'bellhop' && !effectiveWossArtifacts.ssp_file) {
      setWossStatus('✕ 当前 WOSS source/profile 未绑定 SSP 文件，无法直接构建 Bellhop 环境库。');
      return;
    }

    const targetName = databaseName.trim() || (selectedWossProfileId
      ? `${selectedWossSource.id}_${selectedWossProfileId}_${buildMode}`
      : `${selectedWossSource.id}_${buildMode}`);
    setDatabaseName(targetName);
    setWossBusy(true);
    setWossStatus(null);
    setBellhopError(null);
    try {
      const payload: BuildEnvironmentFromWossSourceParams = {
        name: targetName,
        description: databaseDescription.trim() || selectedWossSource.description || undefined,
        profile_id: selectedWossProfileId || undefined,
        build_mode: buildMode,
        frequency_hz: gridFreq * 1000,
        range_max_m: effectiveGridRange,
        depth_max_m: effectiveGridDepth,
        water_depth_m: effectiveWaterDepth,
        source_level_db: Number(p.source_level_db) || 190,
        run_type: buildMode === 'bellhop' ? 'A' : 'analytical',
        bellhop_exe: buildMode === 'bellhop' ? resolvedBellhopPath : undefined,
        notes: wossNotes.trim() || undefined,
        overwrite: wossAllowOverwrite,
        ...(buildMode === 'analytical' ? {
          sound_speed_mps: Number(p.sound_speed_mps) || 1500,
          spreading_factor: Number(p.spreading_factor) || 20,
          absorption_db_per_km: Number(p.absorption_db_per_km) || 0.5,
        } : {}),
        ...(buildMode === 'bellhop' ? {
          max_bounces: Number(p.max_bounces) || 3,
        } : {}),
      };
      const database = await buildEnvironmentFromWossSource(selectedWossSource.id, payload);
      if (database.artifacts.ssp_file) setBuilderSspFile(database.artifacts.ssp_file);
      if (database.artifacts.bathymetry_file) setBuilderBathymetryFile(database.artifacts.bathymetry_file);
      if (database.artifacts.grid_file) setBuilderGridFile(database.artifacts.grid_file);
      setLastCreatedEnvironmentDatabaseId(database.id);
      onRefreshDataFiles();
      fetchEnvironmentCapabilities().then(setEnvironmentCapabilities).catch(() => {});
      const label = selectedWossProfile ? `${selectedWossSource.name}/${selectedWossProfile.name}` : selectedWossSource.name;
      setBellhopWarnings(database.metadata?.warnings ?? []);
      setWossStatus(`✓ 已由 WOSS source ${label} 直接构建环境数据库 ${database.name}。`);
      setBellhopResult(`✓ 已由 WOSS source ${label} 生成环境数据库 ${database.name}。${database.metadata?.warnings?.length ? ` 共返回 ${database.metadata.warnings.length} 条边界提示。` : ''}`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  async function handleBuildAllProfilesFromSelectedWossSource() {
    if (!selectedWossSource) {
      setWossStatus('✕ 请先选择 WOSS source。');
      return;
    }
    if (!selectedWossSource.profiles.length) {
      setWossStatus('✕ 当前 WOSS source 未配置 profiles，无法执行批量建库。');
      return;
    }
    const buildMode = pipelineMode === 'bellhop' ? 'bellhop' : 'analytical';
    const resolvedBellhopPath = bellhopExe.trim() || environmentCapabilities?.bellhop?.resolved_path || '';
    if (buildMode === 'bellhop' && !resolvedBellhopPath) {
      setWossStatus('✕ 当前模式需要可用的 Bellhop 引擎路径。');
      return;
    }

    setWossBusy(true);
    setWossStatus(null);
    setBellhopError(null);
    try {
      const payload: BuildEnvironmentBatchFromWossSourceParams = {
        build_all_profiles: true,
        name_prefix: resolvedWossBatchPrefix,
        build_mode: buildMode,
        frequency_hz: gridFreq * 1000,
        range_max_m: effectiveGridRange,
        depth_max_m: effectiveGridDepth,
        water_depth_m: effectiveWaterDepth,
        source_level_db: Number(p.source_level_db) || 190,
        run_type: buildMode === 'bellhop' ? 'A' : 'analytical',
        bellhop_exe: buildMode === 'bellhop' ? resolvedBellhopPath : undefined,
        notes: wossNotes.trim() || undefined,
        overwrite: wossAllowOverwrite,
        ...(buildMode === 'analytical' ? {
          sound_speed_mps: Number(p.sound_speed_mps) || 1500,
          spreading_factor: Number(p.spreading_factor) || 20,
          absorption_db_per_km: Number(p.absorption_db_per_km) || 0.5,
        } : {}),
        ...(buildMode === 'bellhop' ? {
          max_bounces: Number(p.max_bounces) || 3,
        } : {}),
      };
      const databases = await buildEnvironmentBatchFromWossSource(selectedWossSource.id, payload);
      const lastDatabase = databases[databases.length - 1];
      if (lastDatabase?.artifacts.ssp_file) setBuilderSspFile(lastDatabase.artifacts.ssp_file);
      if (lastDatabase?.artifacts.bathymetry_file) setBuilderBathymetryFile(lastDatabase.artifacts.bathymetry_file);
      if (lastDatabase?.artifacts.grid_file) setBuilderGridFile(lastDatabase.artifacts.grid_file);
      if (lastDatabase) setLastCreatedEnvironmentDatabaseId(lastDatabase.id);
      onRefreshDataFiles();
      fetchEnvironmentCapabilities().then(setEnvironmentCapabilities).catch(() => {});
      setBellhopWarnings(mergeUniqueStrings(...databases.map((database) => database.metadata?.warnings ?? [])));
      setWossStatus(`✓ 已由 WOSS source ${selectedWossSource.name} 批量构建 ${databases.length} 个环境数据库。`);
      setBellhopResult(`✓ 已按全部时相剖面批量生成 ${databases.length} 个环境数据库。`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  async function handleCreateWossSource() {
    if (!wossName.trim()) {
      setWossStatus('✕ 请先填写 WOSS source 名称。');
      return;
    }
    if (!builderSspFile && !builderBathymetryFile) {
      setWossStatus('✕ 请先准备 SSP 或地形文件，再登记为 WOSS 来源。');
      return;
    }
    setWossBusy(true);
    setWossStatus(null);
    try {
      const fallbackProfiles = selectedWossSource?.id === wossName.trim() && wossProfilesDraft.length === 0
        ? selectedWossSource.profiles
        : undefined;
      const payload: CreateWossSourceParams = {
        name: wossName.trim(),
        description: wossDescription.trim(),
        ssp_file: builderSspFile || undefined,
        bathymetry_file: builderBathymetryFile || undefined,
        location: {
          latitude_deg: wossLatitude,
          longitude_deg: wossLongitude,
          region: wossRegion.trim() || undefined,
          transect_label: `bearing_${wossTransectBearing.toFixed(1)}_range_${Math.round(effectiveGridRange)}m`,
        },
        time_reference: {
          month: normalizedWossMonth,
          season: wossSeason.trim() || undefined,
        },
        datasets: {
          woa: wossWoaVersion.trim(),
          gebco: wossGebcoVersion.trim(),
          deck41: wossDeck41Version.trim(),
        },
        profiles: wossProfilesDraft.length > 0 ? wossProfilesDraft : fallbackProfiles,
        notes: wossNotes.trim() || undefined,
      };
      const created = await createWossSource(payload);
      await refreshWossSourceList(created.id);
      fetchEnvironmentCapabilities().then(setEnvironmentCapabilities).catch(() => {});
      setWossProfilesDraft(created.profiles ?? []);
      setWossBatchNamePrefix(created.name);
      setWossStatus(`✓ WOSS source ${created.name} 已登记，可用于环境库溯源。当前 profiles: ${(created.profiles ?? []).length} 个。`);
    } catch (err) {
      setWossStatus(`✕ ${String(err instanceof Error ? err.message : err)}`);
    } finally {
      setWossBusy(false);
    }
  }

  async function handleRunBellhop() {
    const resolvedBellhopPath = bellhopExe.trim() || environmentCapabilities?.bellhop?.resolved_path || '';
    if (!databaseName.trim()) { setBellhopError('请先填写环境数据库名称。'); return; }
    if (pipelineMode === 'bellhop' && !resolvedBellhopPath) { setBellhopError('Bellhop 声线模式必须填写 BELLHOP 引擎路径，或先在系统中配置 BELLHOP_EXE。'); return; }
    if (pipelineMode === 'bellhop' && !builderSspFile) { setBellhopError('请先完成步骤 1，选择或生成 SSP 文件。'); return; }
    setBellhopBusy(true);
    setBellhopResult(null);
    setBellhopError(null);
    setBellhopWarnings([]);
    try {
      let gridPath: string;
      let arrJsonPath: string;
      let arrFilePath: string | undefined;
      let envPath: string | undefined;
      let runWarnings: string[] = [];
      let bellhopSourceDepths: number[] | undefined;
      let bellhopReceiverDepths: number[] | undefined;

      if (pipelineMode === 'analytical') {
        // 解析近似模式：用 Thorp 吸收 + 球面扩展生成网格，不需要 bellhop.exe
        const gridResult = await generateGrid({
          name: databaseName.trim(),
          max_range_m: effectiveGridRange,
          max_depth_m: effectiveGridDepth,
          frequency_khz: gridFreq,
          source_level_db: Number(p.source_level_db) || 190,
          sound_speed_mps: Number(p.sound_speed_mps) || 1500,
          spreading_factor: Number(p.spreading_factor) || 20,
        });
        if (!gridResult) throw new Error('解析网格生成失败。');
        gridPath = gridResult;
        arrJsonPath = '';
      } else {
        // Bellhop 声线追踪模式：生成 .env/.bty/.ati → 调用 bellhop.exe → 解析 .arr
        const params: RunBellhopParams = {
          name: databaseName.trim(),
          freq_hz: gridFreq * 1000,
          range_max_m: effectiveGridRange,
          depth_max_m: effectiveGridDepth,
          source_level_db: Number(p.source_level_db) || 190,
          run_type: 'A',
          skip_run: false,
          ssp_file: builderSspFile,
          bellhop_exe: resolvedBellhopPath,
        };
        if (builderBathymetryFile) params.bathymetry_file = builderBathymetryFile;
        const result = await runBellhop(params);
        if (!result.grid_path || !result.arr_json_path) {
          throw new Error('Bellhop 已执行，但未生成可注册的数据库文件。');
        }
        gridPath = result.grid_path;
        arrJsonPath = result.arr_json_path;
        arrFilePath = result.arr_file_path;
        envPath = result.env_path;
        bellhopSourceDepths = result.source_depths;
        bellhopReceiverDepths = result.receiver_depths;
        runWarnings = mergeUniqueStrings(runWarnings, result.warnings);
      }

      setBuilderGridFile(gridPath);
      const buildMode = pipelineMode;
      const dataSourceType = selectedWossSourceId
        ? 'woss-import'
        : (builderSspFile || builderBathymetryFile ? 'manual-import' : 'preset');
      const datasetVersions = selectedWossSourceId ? effectiveWossDatasets : undefined;
      const geoReference = selectedWossSourceId ? effectiveWossLocation : undefined;
      const timeReference = selectedWossSourceId ? effectiveWossTimeReference : undefined;
      const enginePath = pipelineMode === 'bellhop'
        ? resolvedBellhopPath
        : undefined;
      const resolvedDescription = databaseDescription.trim() || suggestedDatabaseDescription;
      const database = await createEnvironmentDatabase({
        name: databaseName.trim(),
        description: resolvedDescription,
        ssp_file: builderSspFile || undefined,
        bathymetry_file: builderBathymetryFile || undefined,
        grid_file: gridPath,
        arr_json_file: arrJsonPath || undefined,
        arr_file_path: arrFilePath,
        env_path: envPath,
        frequency_hz: gridFreq * 1000,
        range_max_m: effectiveGridRange,
        depth_max_m: effectiveGridDepth,
        water_depth_m: effectiveWaterDepth,
        source_level_db: Number(p.source_level_db) || 190,
        run_type: pipelineMode === 'bellhop' ? 'A' : 'analytical',
        build_mode: buildMode,
        engine_name: pipelineMode === 'bellhop' ? 'bellhop-fortran' : 'analytical-grid',
        engine_version: pipelineMode === 'bellhop' ? 'unknown' : 'builtin-v1',
        engine_path: enginePath,
        at_compatibility: pipelineMode === 'bellhop' ? '2024' : undefined,
        arr_syntax: pipelineMode === 'bellhop' ? '2' : undefined,
        data_source_type: dataSourceType,
        woss_source_id: selectedWossSourceId || undefined,
        woss_profile_id: selectedWossProfileId || undefined,
        validation_status: 'validated',
        validated_at: Date.now() / 1000,
        dataset_versions: datasetVersions,
        geo_reference: geoReference,
        time_reference: timeReference,
        notes: selectedWossSourceId
          ? `关联 WOSS source: ${selectedWossSourceId}${selectedWossProfileId ? ` / ${selectedWossProfileId}` : ''}`
          : undefined,
        ...(buildMode === 'analytical' ? {
          sound_speed_mps: Number(p.sound_speed_mps) || 1500,
          spreading_factor: Number(p.spreading_factor) || 20,
          absorption_db_per_km: Number(p.absorption_db_per_km) || 0.5,
        } : {}),
        ...(buildMode === 'bellhop' ? {
          max_bounces: Number(p.max_bounces) || 3,
          source_depths: bellhopSourceDepths,
          source_depth_m: bellhopSourceDepths?.length === 1 ? bellhopSourceDepths[0] : undefined,
          receiver_depths: bellhopReceiverDepths ?? [],
        } : {}),
      });
      if (!databaseDescription.trim()) {
        setDatabaseDescription(resolvedDescription);
      }
      onRefreshDataFiles();
      setLastCreatedEnvironmentDatabaseId(database.id);
      const modeLabel = pipelineMode === 'analytical' ? '解析近似' : 'Bellhop 声线追踪';
      const finalWarnings = mergeUniqueStrings(runWarnings, database.metadata?.warnings);
      setBellhopWarnings(finalWarnings);
      setBellhopResult(`✓ 已生成环境数据库 ${database.name}（${modeLabel}）。可在上方环境库中按需绑定到当前场景。${finalWarnings.length ? ` 本次共附带 ${finalWarnings.length} 条边界提示。` : ''}`);
    } catch (err) {
      setBellhopError(String(err instanceof Error ? err.message : err));
    } finally {
      setBellhopBusy(false);
    }
  }

  const workflowReady = pipelineMode === 'analytical'
    ? Boolean(databaseName.trim())
    : Boolean(builderSspFile && databaseName.trim() && (bellhopExe.trim() || environmentCapabilities?.bellhop?.resolved_path));
  const buildModeLabel = pipelineMode === 'bellhop' ? 'Bellhop 声线追踪' : '解析近似';
  const buildSummaryItems = [
    { label: '建库名称', value: databaseName.trim() || '未命名' },
    { label: '当前模式', value: buildModeLabel },
    { label: '当前来源', value: selectedWossSource?.name || '手动资产 / 本地草稿' },
  ];
  const buildSummaryPills = [
    { label: 'SSP', value: builderSspFile ? '已准备' : '待准备', tone: builderSspFile ? 'ready' : 'pending' },
    { label: '地形', value: builderBathymetryFile ? '已准备' : '可选', tone: builderBathymetryFile ? 'ready' : 'info' },
    { label: '范围/深度', value: autoManagedCoverageSource ? '自动推导' : '手动规划', tone: autoManagedCoverageSource ? 'ready' : 'info' },
    { label: '输出条件', value: workflowReady ? '可生成' : '待补齐', tone: workflowReady ? 'ready' : 'pending' },
  ] as const;
  const coverageCheckPanel = (
    <div className="environment-builder-summary environment-mode-panel">
      <div className="environment-mode-panel__title">输入资产参数容器</div>
      <div className="environment-mode-panel__description">
        这里保留由当前 SSP / 地形资产自动带入的边界参数容器，不再做运行前检查，也不允许手工填写。Bellhop 运行完成后，相关参数会随环境库结果自动保存。
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6 }}>
        <FieldRow label="最大距离" hint="m" description="自动带入的传播距离边界。">
          <NumInput value={effectiveGridRange} onChange={setGridRange} min={100} max={100000} step={500} disabled />
        </FieldRow>
        <FieldRow label="最大深度" hint="m" description="自动带入的传播深度边界。">
          <NumInput value={effectiveGridDepth} onChange={setGridDepth} min={5} max={11000} step={10} disabled />
        </FieldRow>
        <FieldRow label="水深" hint="m" description="自动带入的名义水深记录。">
          <NumInput value={effectiveWaterDepth} onChange={(value) => onUpdateTransmissionParam('water_depth_m', String(value))} min={1} max={11000} step={10} disabled />
        </FieldRow>
      </div>
    </div>
  );
  const sourceLevelField = (
    <FieldRow label="声源级" hint="发射功率 dB re 1μPa@1m" description="这是声学源级，用于把传播损失换算为接收级。没有实测值时，建议先用 190 dB 起步；170 dB 左右适合低功率短距，185-195 dB 常见于主动通信/信标，205-220 dB 更接近较强主动声源。">
      <div style={{ display: 'grid', gap: 6 }}>
        <NumInput value={Number(p.source_level_db) || 190} onChange={(v) => onUpdateTransmissionParam('source_level_db', String(v))} min={100} max={230} step={1} />
        <div className="environment-builder-grid environment-builder-grid--actions">
          {sourceLevelPresets.map((preset) => (
            <button key={preset.label} type="button" style={compactActionBtnStyle} title={preset.description} onClick={() => onUpdateTransmissionParam('source_level_db', String(preset.value))}>
              {preset.label}
            </button>
          ))}
        </div>
      </div>
    </FieldRow>
  );
  const rangeDepthSummary = (autoManagedRange || autoManagedDepth) ? (
    <div className="environment-builder-summary environment-mode-panel">
      <div style={{ fontSize: 10, color: '#7dd3fc', lineHeight: 1.5 }}>
        当前已识别可用的 SSP / 地形资产，范围、深度和水深会按两类资产的共同覆盖范围自动推导；切换到 Bellhop 模式后，这些边界项也不会在 Bellhop 参数里重复展示。
      </div>
    </div>
  ) : null;

  return (
    <div className={`inspector-panel__section ${mode === 'builder' ? 'environment-builder-mode' : ''}`}>
      <p className="eyebrow" style={{ margin: 0 }}>Bellhop 环境数据库</p>
      <div style={{ fontSize: 11, color: 'var(--text-dim)', marginBottom: 6, lineHeight: 1.5 }}>
        {mode === 'builder'
          ? '这里是独立环境构建区：先离线配置 SSP、地形和传播网格，再把结果存入环境库供多个场景复用。'
          : '当前场景只负责选择已有环境数据库并预览其内容；离线环境构建请在独立环境库页面完成。'}
      </div>

      <div className="inspector-panel__form" style={{ display: 'grid', gap: 10 }}>
        {showOfflineBuilder && (
          <div className="environment-builder-overview">
            <div className="environment-builder-overview__head">
              <div className="environment-builder-overview__eyebrow">环境构建流程</div>
              <div className="environment-builder-overview__title">先准备环境资产，再确认传播假设，最后生成可复用环境库</div>
              <div className="environment-builder-overview__description">
                这一页现在按步骤组织：先处理 SSP 和海底地形，再确认范围与声学参数，最后一次性生成并入库。真实 WOSS 资产到位后，范围和深度会自动回填，不需要反复手工对齐。
              </div>
            </div>
            <div className="environment-builder-overview__pills">
              {buildSummaryPills.map((item) => (
                <span key={item.label} className={`environment-builder-pill environment-builder-pill--${item.tone}`}>
                  <strong>{item.label}</strong>
                  <span>{item.value}</span>
                </span>
              ))}
            </div>
            <div className="environment-builder-grid environment-builder-grid--metrics">
              {buildSummaryItems.map((item) => (
                <div key={item.label} className="environment-builder-metric environment-builder-overview__metric">
                  <span className="environment-builder-overview__metric-label">{item.label}</span>
                  <strong>{item.value}</strong>
                </div>
              ))}
            </div>
          </div>
        )}

        {/* ── 在线阶段 / 选择环境数据库 ── */}
        <div className="environment-builder-card environment-builder-card--selection">
          <BuilderStageHeader phase="在线阶段" title="选择环境数据库" description="先绑定已有环境库，或把下方刚生成的环境库回挂到当前场景。" status={selectedEnvironmentDatabase ? `已绑定 ${selectedEnvironmentDatabase.name}` : '当前未绑定'} tone={selectedEnvironmentDatabase ? 'ready' : 'pending'} />
          <FieldRow label="环境库">
            <select value={selectedEnvironmentDatabaseId} onChange={(e) => onSelectEnvironmentDatabase(e.target.value)} style={selectStyle}>
              <option value="">— 未绑定，沿用当前旧字段 —</option>
              {environmentDatabases.map((database) => <option key={database.id} value={database.id}>{database.name}</option>)}
            </select>
          </FieldRow>
          <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.5 }}>
            当前项目环境库数量：{environmentDatabases.length}。历史网格文件：{gridFiles.length} 个。推荐使用环境库而不是手工维护零散文件路径。
          </div>
          {lastCreatedEnvironmentDatabaseId && lastCreatedEnvironmentDatabaseId !== selectedEnvironmentDatabaseId && (
            <button
              type="button"
              onClick={() => onSelectEnvironmentDatabase(lastCreatedEnvironmentDatabaseId)}
              style={{ ...smallBtnStyle, width: '100%', marginTop: 8, padding: '6px 10px' }}
            >
              绑定最近生成的环境库：{lastCreatedEnvironmentDatabaseId}
            </button>
          )}
          {selectedEnvironmentDatabase ? (
            <div style={{ display: 'grid', gap: 5, marginTop: 8 }}>
              {selectedEnvironmentDatabase.description && <div style={{ fontSize: 11, color: '#94a3b8', lineHeight: 1.5 }}>{selectedEnvironmentDatabase.description}</div>}
              {selectedEnvironmentDatabase.metadata && (
                <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 10px', fontSize: 10, color: '#7dd3fc', lineHeight: 1.5 }}>
                  <span>模式: {formatBuildModeLabel(selectedEnvironmentDatabase.metadata.build_mode)}</span>
                  <span>来源: {formatSourceTypeLabel(selectedEnvironmentDatabase.metadata.data_source_type)}</span>
                  <span>状态: {formatValidationLabel(selectedEnvironmentDatabase.metadata.validation_status)}</span>
                  <span>引擎: {selectedEnvironmentDatabase.metadata.engine_name || '未标记'}</span>
                  {selectedEnvironmentDatabase.metadata.at_compatibility && <span>AT兼容: {selectedEnvironmentDatabase.metadata.at_compatibility}</span>}
                  {selectedEnvironmentDatabase.metadata.arr_syntax && <span>ARR语法: {selectedEnvironmentDatabase.metadata.arr_syntax}</span>}
                  {selectedEnvironmentDatabase.metadata.woss_source_id && <span>WOSS源: {selectedEnvironmentDatabase.metadata.woss_source_id}</span>}
                  {selectedEnvironmentDatabase.metadata.woss_profile_id && <span>WOSS剖面: {selectedEnvironmentDatabase.metadata.woss_profile_id}</span>}
                </div>
              )}
              {([
                ['SSP', selectedEnvironmentDatabase.artifacts.ssp_file || '未指定（解析近似模式）'],
                ['地形', selectedEnvironmentDatabase.artifacts.bathymetry_file || '平坦海底 / 未指定'],
                ['网格', selectedEnvironmentDatabase.artifacts.grid_file],
                ['到达库', selectedEnvironmentDatabase.artifacts.arr_json_file || '无（解析近似模式）'],
              ] as [string, string][]).map(([label, value]) => (
                <div key={label} style={{ display: 'flex', justifyContent: 'space-between', gap: 8, fontSize: 11 }}>
                  <span style={{ color: 'var(--text-dim)' }}>{label}</span>
                  <span style={{ color: '#eff7ff', textAlign: 'right', wordBreak: 'break-all' }}>{value}</span>
                </div>
              ))}
              {selectedEnvironmentDatabase.build && (
                <div style={{ marginTop: 4, padding: '4px 8px', borderRadius: 6, background: 'rgba(34,211,238,0.04)', border: '1px solid rgba(34,211,238,0.1)' }}>
                  <div style={{ fontSize: 10, color: '#607080', marginBottom: 2 }}>构建参数</div>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 2, fontSize: 10 }}>
                    {selectedEnvironmentDatabase.build.frequency_hz && <span style={{ color: '#94a3b8' }}>频率: {(selectedEnvironmentDatabase.build.frequency_hz / 1000).toFixed(1)} kHz</span>}
                    {selectedEnvironmentDatabase.build.range_max_m && <span style={{ color: '#94a3b8' }}>范围: {selectedEnvironmentDatabase.build.range_max_m} m</span>}
                    {selectedEnvironmentDatabase.build.depth_max_m && <span style={{ color: '#94a3b8' }}>深度: {selectedEnvironmentDatabase.build.depth_max_m} m</span>}
                    {selectedEnvironmentDatabase.build.water_depth_m && <span style={{ color: '#94a3b8' }}>水深: {selectedEnvironmentDatabase.build.water_depth_m} m</span>}
                    {selectedEnvironmentBuildMode === 'analytical' && selectedEnvironmentDatabase.build.sound_speed_mps && <span style={{ color: '#94a3b8' }}>声速: {selectedEnvironmentDatabase.build.sound_speed_mps} m/s</span>}
                    {selectedEnvironmentDatabase.build.source_level_db && <span style={{ color: '#94a3b8' }}>声源级: {selectedEnvironmentDatabase.build.source_level_db} dB</span>}
                    {selectedEnvironmentBuildMode === 'analytical' && selectedEnvironmentDatabase.build.spreading_factor && <span style={{ color: '#94a3b8' }}>扩展因子: {selectedEnvironmentDatabase.build.spreading_factor}</span>}
                    {selectedEnvironmentBuildMode === 'analytical' && selectedEnvironmentDatabase.build.absorption_db_per_km && <span style={{ color: '#94a3b8' }}>吸收: {selectedEnvironmentDatabase.build.absorption_db_per_km} dB/km</span>}
                    {selectedEnvironmentBuildMode === 'bellhop' && selectedEnvironmentDatabase.build.max_bounces !== undefined && selectedEnvironmentDatabase.build.max_bounces !== null && <span style={{ color: '#94a3b8' }}>反射次数: {selectedEnvironmentDatabase.build.max_bounces}</span>}
                  </div>
                  <div style={{ fontSize: 10, color: '#607080', marginTop: 2 }}>
                    模式: {selectedEnvironmentDatabase.build.run_type === 'analytical' ? '解析近似' : `Bellhop (${selectedEnvironmentDatabase.build.run_type || 'A'})`}
                  </div>
                  {selectedEnvironmentDatabase.metadata?.dataset_versions && Object.keys(selectedEnvironmentDatabase.metadata.dataset_versions).length > 0 && (
                    <div style={{ fontSize: 10, color: '#94a3b8', marginTop: 2 }}>
                      数据集: {Object.entries(selectedEnvironmentDatabase.metadata.dataset_versions).map(([key, value]) => `${key}:${value}`).join(' / ')}
                    </div>
                  )}
                  {selectedEnvironmentDatabase.metadata?.geo_reference && (selectedEnvironmentDatabase.metadata.geo_reference.latitude_deg != null || selectedEnvironmentDatabase.metadata.geo_reference.longitude_deg != null || selectedEnvironmentDatabase.metadata.geo_reference.region) && (
                    <div style={{ fontSize: 10, color: '#94a3b8', marginTop: 2 }}>
                      位置: {selectedEnvironmentDatabase.metadata.geo_reference.region || '未命名海域'}
                      {selectedEnvironmentDatabase.metadata.geo_reference.latitude_deg != null && selectedEnvironmentDatabase.metadata.geo_reference.longitude_deg != null && ` (${selectedEnvironmentDatabase.metadata.geo_reference.latitude_deg.toFixed(3)}, ${selectedEnvironmentDatabase.metadata.geo_reference.longitude_deg.toFixed(3)})`}
                    </div>
                  )}
                </div>
              )}
              {mode !== 'builder' && sspData && sspData.length > 1 && (
                <div style={{ marginTop: 4 }}>
                  <div style={{ fontSize: 10, color: '#607080', marginBottom: 2 }}>声速剖面预览</div>
                  <SspChart data={sspData} />
                </div>
              )}
              {mode !== 'builder' && bathData && bathData.range_m && bathData.range_m.length > 1 && (
                <div style={{ marginTop: 4 }}>
                  <div style={{ fontSize: 10, color: '#607080', marginBottom: 2 }}>海底地形预览</div>
                  <BathymetryChart data={bathData} />
                </div>
              )}
            </div>
          ) : (
            <div style={warnBoxStyle}>{mode === 'builder' ? '当前未选中环境数据库。你可以直接在下方创建新的环境库，或切换上方已有条目查看详情。' : '当前场景尚未绑定环境数据库。请在独立环境库页面先完成环境构建，或直接选择已有环境库。'}</div>
          )}
        </div>

        {showOfflineBuilder && (
          <>
        {/* ── 离线阶段 / 1. SSP 声速剖面 ── */}
        <div className="environment-builder-card">
          <BuilderStageHeader phase="步骤 1" title="SSP 声速剖面" description="先准备声速剖面。Bellhop 追踪和真实环境建库都会复用这一份资产。" status={builderSspFile ? `已选 ${formatPathName(builderSspFile)}` : '尚未准备 SSP'} tone={builderSspFile ? 'ready' : 'pending'} />
          <TabBar tabs={[{ key: 'formula', label: '标准公式' }, { key: 'import', label: '手动 / 导入' }, { key: 'woss', label: 'WOSS / WOA' }]} active={sspTab} onChange={setSspTab} />

          {sspTab === 'formula' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <div style={{ fontSize: 10, color: '#94a3b8', marginBottom: 2 }}>快速预设（一键填充参数）</div>
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, 1fr)', gap: 4 }}>
                {sspQuickPresets.map((preset) => (
                  <button
                    key={preset.label}
                    type="button"
                    onClick={() => {
                      setSspFormula(preset.formula);
                      setSspDepthMax(preset.depthMax);
                      setSspC0(preset.c0);
                      setSspTcDepth(preset.tcDepth);
                      setSspTcThick(preset.tcThick);
                      setSspSurfSpeed(preset.surfSpeed);
                      setSspDeepSpeed(preset.deepSpeed);
                    }}
                    style={{
                      padding: '5px 2px', borderRadius: 6, fontSize: 10, cursor: 'pointer', textAlign: 'center',
                      border: '1px solid', lineHeight: 1.3,
                      background: sspFormula === preset.formula ? 'rgba(34,211,238,0.12)' : 'rgba(2,10,19,0.5)',
                      borderColor: sspFormula === preset.formula ? 'rgba(34,211,238,0.3)' : 'rgba(148,163,184,0.15)',
                      color: sspFormula === preset.formula ? 'var(--accent)' : 'var(--text-dim)',
                    }}
                  >
                    {preset.label}
                  </button>
                ))}
              </div>
              <FieldRow label="公式类型">
                <select value={sspFormula} onChange={(e) => setSspFormula(e.target.value)} style={selectStyle}>
                  {sspFormulaOptions.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
                </select>
              </FieldRow>
              <FieldRow label="输出文件名">
                <input type="text" value={sspFormulaName} onChange={(e) => setSspFormulaName(e.target.value)} placeholder="ssp_munk_200m" style={selectStyle} />
              </FieldRow>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="最大深度" hint="m"><NumInput value={sspDepthMax} onChange={setSspDepthMax} min={20} max={11000} step={10} /></FieldRow>
                <FieldRow label="参考声速" hint="m/s"><NumInput value={sspC0} onChange={setSspC0} min={1400} max={1600} step={1} /></FieldRow>
              </div>
              {sspFormula === 'linear_gradient' && <FieldRow label="梯度" hint="(m/s)/m"><NumInput value={sspGradient} onChange={setSspGradient} min={-1} max={1} step={0.001} /></FieldRow>}
              {sspFormula === 'thermocline' && (
                <>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                    <FieldRow label="温跃层起点" hint="m"><NumInput value={sspTcDepth} onChange={setSspTcDepth} min={0} max={5000} step={5} /></FieldRow>
                    <FieldRow label="温跃层厚度" hint="m"><NumInput value={sspTcThick} onChange={setSspTcThick} min={1} max={1000} step={5} /></FieldRow>
                  </div>
                  <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                    <FieldRow label="表层声速" hint="m/s"><NumInput value={sspSurfSpeed} onChange={setSspSurfSpeed} min={1400} max={1600} step={1} /></FieldRow>
                    <FieldRow label="深层声速" hint="m/s"><NumInput value={sspDeepSpeed} onChange={setSspDeepSpeed} min={1400} max={1600} step={1} /></FieldRow>
                  </div>
                </>
              )}
              <button type="button" disabled={sspFormulaBusy || !sspFormulaName.trim()} onClick={handleGenerateSsp} style={{ ...compactActionBtnStyle, justifySelf: 'start', minWidth: 200, opacity: sspFormulaBusy || !sspFormulaName.trim() ? 0.5 : 1 }}>
                {sspFormulaBusy ? '生成中…' : '生成并加入离线草稿'}
              </button>
            </div>
          )}

          {sspTab === 'import' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <FieldRow label="草稿 SSP 文件" hint="CSV: depth_m, sound_speed_mps">
                <div style={{ display: 'flex', gap: 4 }}>
                  <select value={builderSspFile} onChange={(e) => setBuilderSspFile(e.target.value)} style={{ ...selectStyle, flex: 1 }}>
                    <option value="">— 未选择 —</option>
                    {sspFiles.map((f) => <option key={f} value={f}>{f.split('/').pop()}</option>)}
                  </select>
                  <button type="button" style={{ ...smallBtnStyle, opacity: sspUploadBusy ? 0.5 : 1 }} disabled={sspUploadBusy} onClick={() => sspFileRef.current?.click()}>
                    {sspUploadBusy ? '上传中…' : '上传 CSV'}
                  </button>
                  <input ref={sspFileRef} type="file" accept=".csv,.txt" style={{ display: 'none' }} onChange={handleUploadSsp} />
                </div>
              </FieldRow>
              {sspData && sspData.length >= 2 && builderSspFile ? <SspEditor data={sspData} onSave={handleSaveSsp} /> : <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.5 }}>选择或上传 SSP 后，可直接在这里逐点编辑深度-声速数据。</div>}
            </div>
          )}

          {sspTab === 'woss' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <div style={{ ...warnBoxStyle, color: '#cbd5f5' }}>
                当前阶段支持两条链路：一是把已有 WOSS / WOA / GEBCO 提取结果登记为来源；二是直接按坐标、月份或季节抓取 WOA23 + GEBCO2020 生成 SSP 与地形，再回写到来源或时相剖面。Windows 主链路仍不直接运行完整 WOSS runtime。
              </div>
              <FieldRow label="已登记 WOSS 来源">
                <div style={{ display: 'flex', gap: 4 }}>
                  <select value={selectedWossSourceId} onChange={(e) => handleChangeWossSource(e.target.value)} style={{ ...selectStyle, flex: 1 }}>
                    <option value="">— 未选择 —</option>
                    {wossSources.map((source) => <option key={source.id} value={source.id}>{source.name}</option>)}
                  </select>
                  <button type="button" style={smallBtnStyle} onClick={() => refreshWossSourceList(selectedWossSourceId).catch(() => {})}>刷新</button>
                </div>
              </FieldRow>
              {selectedWossSource && (
                <div style={{ padding: '6px 10px', borderRadius: 8, background: 'rgba(34,211,238,0.05)', border: '1px solid rgba(34,211,238,0.12)', display: 'grid', gap: 4 }}>
                  <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
                    <div style={{ fontSize: 11, color: '#eff7ff', fontWeight: 600 }}>{selectedWossSource.name}</div>
                    <button type="button" disabled={wossBusy} style={{ ...dangerBtnStyle, opacity: wossBusy ? 0.5 : 1 }} onClick={handleDeleteWossSource}>
                      {wossBusy ? '处理中…' : '删除来源'}
                    </button>
                  </div>
                  {selectedWossSource.description && <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5 }}>{selectedWossSource.description}</div>}
                  <div style={{ fontSize: 10, color: '#7dd3fc' }}>时相剖面: {selectedWossSource.profiles.length} 个</div>
                  {selectedWossSource.profiles.length > 0 && (
                    <FieldRow label="时相剖面">
                      <select value={selectedWossProfileId} onChange={(e) => setSelectedWossProfileId(e.target.value)} style={selectStyle}>
                        <option value="">— 使用来源默认资产 —</option>
                        {selectedWossSource.profiles.map((profile) => <option key={profile.id} value={profile.id}>{profile.name}</option>)}
                      </select>
                    </FieldRow>
                  )}
                  {selectedWossProfile && <div style={{ fontSize: 10, color: '#7dd3fc' }}>当前剖面: {selectedWossProfile.name}，时相: {formatTimeReferenceLabel(selectedWossProfile.time_reference)}</div>}
                  <div style={{ fontSize: 10, color: '#94a3b8' }}>
                    数据集: {Object.entries(effectiveWossDatasets).map(([key, value]) => `${key}:${value}`).join(' / ') || '未标记'}
                  </div>
                  <div style={{ fontSize: 10, color: '#94a3b8' }}>
                    位置: {effectiveWossLocation.region || '未命名海域'}
                    {effectiveWossLocation.latitude_deg != null && effectiveWossLocation.longitude_deg != null && ` (${effectiveWossLocation.latitude_deg.toFixed(3)}, ${effectiveWossLocation.longitude_deg.toFixed(3)})`}
                  </div>
                  <div style={{ fontSize: 10, color: '#94a3b8' }}>时相: {formatTimeReferenceLabel(effectiveWossTimeReference)}</div>
                  <div style={{ fontSize: 10, color: '#94a3b8' }}>
                    绑定文件: SSP={effectiveWossArtifacts.ssp_file || '未绑定'} / 地形={effectiveWossArtifacts.bathymetry_file || '未绑定'}
                  </div>
                  {effectiveWossCache && (
                    <div style={{ display: 'grid', gap: 2, padding: '6px 8px', borderRadius: 8, background: 'rgba(15,23,42,0.45)', border: '1px solid rgba(148,163,184,0.12)' }}>
                      <div style={{ fontSize: 10, color: '#7dd3fc' }}>缓存记录: {effectiveWossCache.cache_id}</div>
                      <div style={{ fontSize: 10, color: '#94a3b8' }}>导入时间: {formatTimestampLabel(effectiveWossCache.imported_at)}</div>
                      <div style={{ fontSize: 10, color: '#94a3b8' }}>缓存 SSP: {formatCacheArtifactLabel(effectiveWossCache.artifacts.ssp_file ?? null)}</div>
                      <div style={{ fontSize: 10, color: '#94a3b8' }}>缓存地形: {formatCacheArtifactLabel(effectiveWossCache.artifacts.bathymetry_file ?? null)}</div>
                    </div>
                  )}
                  <div className="environment-builder-grid environment-builder-grid--actions">
                    <button type="button" style={compactActionBtnStyle} onClick={() => handleLoadWossSourceIntoDraft(selectedWossSource)}>
                      载入该来源到下方编辑草稿
                    </button>
                    <button type="button" style={compactActionBtnStyle} onClick={() => handleApplyWossSource(selectedWossSource, selectedWossProfile)}>
                      将该 WOSS 来源应用到当前离线构建草稿
                    </button>
                    <button type="button" disabled={wossBusy} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }} onClick={handleBuildEnvironmentFromSelectedWossSource}>
                      {wossBusy ? '处理中…' : `直接构建 ${pipelineMode === 'bellhop' ? 'Bellhop' : '解析'} 环境库`}
                    </button>
                    {selectedWossSource.profiles.length > 0 && (
                      <button type="button" disabled={wossBusy} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }} onClick={handleBuildAllProfilesFromSelectedWossSource}>
                        {wossBusy ? '处理中…' : `按全部时相剖面批量构建 ${pipelineMode === 'bellhop' ? 'Bellhop' : '解析'} 环境库`}
                      </button>
                    )}
                  </div>
                  <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5 }}>
                    若该来源已被环境库引用，删除会被后端阻止，并返回具体引用信息。
                  </div>
                  <FieldRow label="批量名前缀">
                    <input type="text" value={wossBatchNamePrefix} onChange={(e) => setWossBatchNamePrefix(e.target.value)} placeholder={selectedWossSource.id} style={selectStyle} />
                  </FieldRow>
                  <label style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 10, color: '#94a3b8' }}>
                    <input type="checkbox" checked={wossAllowOverwrite} onChange={(e) => setWossAllowOverwrite(e.target.checked)} />
                    允许覆盖同名环境库
                  </label>
                </div>
              )}
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="WOSS 来源名称"><input type="text" value={wossName} onChange={(e) => setWossName(e.target.value)} placeholder="scs_apr2026" style={selectStyle} /></FieldRow>
                <FieldRow label="区域描述"><input type="text" value={wossRegion} onChange={(e) => setWossRegion(e.target.value)} placeholder="南海北部" style={selectStyle} /></FieldRow>
              </div>
              <FieldRow label="来源说明"><input type="text" value={wossDescription} onChange={(e) => setWossDescription(e.target.value)} placeholder="WOSS 外部提取的 WOA SSP + GEBCO 剖面" style={selectStyle} /></FieldRow>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="时相剖面名称"><input type="text" value={wossProfileName} onChange={(e) => setWossProfileName(e.target.value)} placeholder="apr_reference" style={selectStyle} /></FieldRow>
                <FieldRow label="时相剖面说明"><input type="text" value={wossProfileDescription} onChange={(e) => setWossProfileDescription(e.target.value)} placeholder="4 月春季参考剖面" style={selectStyle} /></FieldRow>
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6 }}>
                <FieldRow label="纬度"><NumInput value={wossLatitude} onChange={setWossLatitude} min={-90} max={90} step={0.001} /></FieldRow>
                <FieldRow label="经度"><NumInput value={wossLongitude} onChange={setWossLongitude} min={-180} max={180} step={0.001} /></FieldRow>
                <FieldRow label="剖面方位角" hint="deg"><NumInput value={wossTransectBearing} onChange={setWossTransectBearing} min={-180} max={180} step={1} /></FieldRow>
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="月份" description="月平均或季节平均的时间索引；选择年平均时只保留季节标记。">
                  <Select value={String(wossMonth)} onChange={(value) => setWossMonth(Number(value))} options={monthOptions} />
                </FieldRow>
                <FieldRow label="季节" description="用于补充月份为空时的标签，也会写入 source/profile 元数据。">
                  <Select value={wossSeason} onChange={setWossSeason} options={seasonOptions} />
                </FieldRow>
              </div>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6 }}>
                <FieldRow label="WOA 版本" description="声速剖面来源版本。"><Select value={wossWoaVersion} onChange={setWossWoaVersion} options={woaVersionOptions} /></FieldRow>
                <FieldRow label="GEBCO 版本" description="海底地形来源版本。"><Select value={wossGebcoVersion} onChange={setWossGebcoVersion} options={gebcoVersionOptions} /></FieldRow>
                <FieldRow label="DECK41 版本" description="辅助海洋数据集版本。"><Select value={wossDeck41Version} onChange={setWossDeck41Version} options={deck41VersionOptions} /></FieldRow>
              </div>
              <FieldRow label="备注"><textarea rows={3} value={wossNotes} onChange={(e) => setWossNotes(e.target.value)} placeholder="记录外部 WOSS 提取任务、时间窗口、数据裁剪方式等" style={{ ...selectStyle, resize: 'vertical', minHeight: 72 }} /></FieldRow>
              <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5 }}>
                当前绑定文件: SSP={builderSspFile || '未绑定'} / 地形={builderBathymetryFile || '未绑定'}。按坐标抓取真实数据时，会先使用步骤 3 的当前规划值发起提取；一旦真实资产就位，步骤 3 会自动回填实际覆盖范围与深度。
              </div>
              <div className="environment-builder-grid environment-builder-grid--actions">
                <button type="button" disabled={wossBusy} onClick={() => handleImportRealOceanData('source')} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }}>
                  {wossBusy ? '处理中…' : '按坐标抓取真实数据并登记到源位置'}
                </button>
                <button type="button" disabled={wossBusy} onClick={() => handleImportRealOceanData('profile')} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }}>
                  {wossBusy ? '处理中…' : '按坐标抓取真实数据并登记到时相剖面'}
                </button>
              </div>
              <div className="environment-builder-grid environment-builder-grid--actions">
                <button type="button" disabled={wossBusy} onClick={() => handleImportCurrentWossDraft('source')} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }}>
                  {wossBusy ? '处理中…' : '把当前 SSP/地形导入源位置缓存'}
                </button>
                <button type="button" disabled={wossBusy} onClick={() => handleImportCurrentWossDraft('profile')} style={{ ...compactActionBtnStyle, opacity: wossBusy ? 0.5 : 1 }}>
                  {wossBusy ? '处理中…' : '把当前 SSP/地形导入时相剖面缓存'}
                </button>
              </div>
              <div className="environment-builder-grid environment-builder-grid--actions">
                <button type="button" onClick={handleUpsertWossProfileDraft} style={compactActionBtnStyle}>
                  加入 / 更新当前时相剖面草稿
                </button>
                <button type="button" onClick={() => { setWossProfileName(''); setWossProfileDescription(''); }} style={compactActionBtnStyle}>
                  清空剖面输入
                </button>
              </div>
              {wossProfilesDraft.length > 0 && (
                <div style={{ display: 'grid', gap: 6, padding: '6px 8px', borderRadius: 8, background: 'rgba(255,255,255,0.03)', border: '1px solid rgba(148,163,184,0.12)' }}>
                  <div style={{ fontSize: 10, color: '#7dd3fc' }}>待保存时相剖面: {wossProfilesDraft.length} 个</div>
                  {wossProfilesDraft.map((profile) => (
                    <div key={profile.id} style={{ display: 'grid', gap: 2, padding: '6px 8px', borderRadius: 6, background: 'rgba(2,10,19,0.45)', border: '1px solid rgba(148,163,184,0.08)' }}>
                      <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', gap: 8 }}>
                        <div style={{ fontSize: 11, color: '#eff7ff', fontWeight: 600 }}>{profile.name}</div>
                        <div style={{ display: 'flex', gap: 4 }}>
                          <button type="button" style={smallBtnStyle} onClick={() => handleLoadWossProfileIntoDraft(profile)}>载入</button>
                          <button type="button" style={dangerBtnStyle} onClick={() => handleRemoveWossProfileDraft(profile.id)}>删除</button>
                        </div>
                      </div>
                      <div style={{ fontSize: 10, color: '#94a3b8' }}>ID: {profile.id} / 时相: {formatTimeReferenceLabel(profile.time_reference)}</div>
                      <div style={{ fontSize: 10, color: '#94a3b8' }}>资产: SSP={profile.artifacts.ssp_file || '未绑定'} / 地形={profile.artifacts.bathymetry_file || '未绑定'}</div>
                    </div>
                  ))}
                </div>
              )}
              <button type="button" disabled={wossBusy} onClick={handleCreateWossSource} style={{ ...compactActionBtnStyle, justifySelf: 'start', minWidth: 220, opacity: wossBusy ? 0.5 : 1 }}>
                {wossBusy ? '登记中…' : '保存 / 更新 WOSS 来源'}
              </button>
              {wossStatus && <div style={{ fontSize: 10, padding: '6px 10px', borderRadius: 8, background: wossStatus.startsWith('✓') ? 'rgba(34,197,94,0.1)' : 'rgba(239,68,68,0.1)', border: `1px solid ${wossStatus.startsWith('✓') ? 'rgba(34,197,94,0.25)' : 'rgba(239,68,68,0.25)'}`, color: wossStatus.startsWith('✓') ? '#86efac' : '#fca5a5' }}>{wossStatus}</div>}
            </div>
          )}
          {sspData && sspData.length > 1 && <SspChart data={sspData} />}
        </div>

        {/* ── 离线阶段 / 2. 海底地形 ── */}
        <div className="environment-builder-card">
          <BuilderStageHeader phase="步骤 2" title="海底地形" description="可以使用简化预设、手动导入，或直接复用 WOSS / GEBCO 真实地形资产。" status={builderBathymetryFile ? `已选 ${formatPathName(builderBathymetryFile)}` : '未指定时默认平坦海底'} tone={builderBathymetryFile ? 'ready' : 'info'} />
          <TabBar tabs={[{ key: 'preset', label: '简化预设' }, { key: 'import', label: '导入' }, { key: 'woss', label: 'WOSS / GEBCO' }]} active={bathTab} onChange={setBathTab} />

          {bathTab === 'preset' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.5 }}>这里生成的是地形草稿文件，不会直接改当前场景。环境库创建成功后，场景才通过数据库引用它。</div>
              <FieldRow label="当前草稿地形文件">
                <select value={builderBathymetryFile} onChange={(e) => setBuilderBathymetryFile(e.target.value)} style={selectStyle}>
                  <option value="">— 无（平坦海底）—</option>
                  {bathymetryFiles.map((f) => <option key={f} value={f}>{f.split('/').pop()}</option>)}
                </select>
              </FieldRow>
              <FieldRow label="输出文件名"><input type="text" value={bathName} onChange={(e) => setBathName(e.target.value)} placeholder="ridge_demo" style={selectStyle} /></FieldRow>
              <FieldRow label="地形预设">
                <select value={bathProfile} onChange={(e) => setBathProfile(e.target.value)} style={selectStyle}>
                  {profileOptions.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
                </select>
              </FieldRow>
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="最大距离" hint="m"><NumInput value={bathRange} onChange={setBathRange} min={100} max={100000} step={500} /></FieldRow>
                <FieldRow label="基准水深" hint="m"><NumInput value={bathDepth} onChange={setBathDepth} min={5} max={11000} step={10} /></FieldRow>
              </div>
              {bathProfile !== 'flat' && (
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                  <FieldRow label="地形位置" hint="m"><NumInput value={bathFeatureRange} onChange={setBathFeatureRange} min={0} max={100000} step={100} /></FieldRow>
                  <FieldRow label="地形幅度" hint="m"><NumInput value={bathFeatureHeight} onChange={setBathFeatureHeight} min={1} max={10000} step={5} /></FieldRow>
                </div>
              )}
              <button type="button" disabled={bathBusy || !bathName.trim()} onClick={handleCreateBath} style={{ ...compactActionBtnStyle, justifySelf: 'start', minWidth: 160, opacity: bathBusy || !bathName.trim() ? 0.5 : 1 }}>
                {bathBusy ? '生成中…' : '生成地形草稿'}
              </button>
            </div>
          )}

          {bathTab === 'import' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <FieldRow label="已有地形文件">
                <select value={builderBathymetryFile} onChange={(e) => setBuilderBathymetryFile(e.target.value)} style={selectStyle}>
                  <option value="">— 未选择 —</option>
                  {bathymetryFiles.map((f) => <option key={f} value={f}>{f.split('/').pop()}</option>)}
                </select>
              </FieldRow>
              <button type="button" style={{ ...smallBtnStyle, opacity: bathImportBusy ? 0.5 : 1 }} disabled={bathImportBusy} onClick={() => bathFileRef.current?.click()}>{bathImportBusy ? '导入中…' : '导入地形 CSV/JSON'}</button>
              <input ref={bathFileRef} type="file" accept=".csv,.txt,.json" style={{ display: 'none' }} onChange={handleImportBath} />
            </div>
          )}

          {bathTab === 'woss' && (
            <div style={{ display: 'grid', gap: 8 }}>
              <div style={{ ...warnBoxStyle, color: '#cbd5f5' }}>
                若需要真实海底地形，优先在上方 WOSS 标签页按坐标抓取 WOA23 + GEBCO2020，再复用这里的地形文件；也仍然支持外部完成裁剪后手动导入。
              </div>
              <FieldRow label="当前 WOSS source">
                <select value={selectedWossSourceId} onChange={(e) => handleChangeWossSource(e.target.value)} style={selectStyle}>
                  <option value="">— 未选择 —</option>
                  {wossSources.map((source) => <option key={source.id} value={source.id}>{source.name}</option>)}
                </select>
              </FieldRow>
              {selectedWossSource ? (
                <div style={{ display: 'grid', gap: 4 }}>
                  {selectedWossProfile && <div style={{ fontSize: 10, color: '#7dd3fc' }}>当前 profile: {selectedWossProfile.name}</div>}
                  <div style={{ fontSize: 10, color: '#94a3b8' }}>WOSS 绑定地形: {effectiveWossArtifacts.bathymetry_file || '未绑定'}</div>
                  <button type="button" style={{ ...compactActionBtnStyle, justifySelf: 'start', minWidth: 180 }} onClick={() => handleApplyWossSource(selectedWossSource, selectedWossProfile)}>
                    使用该 source 的地形文件
                  </button>
                </div>
              ) : (
                <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.5 }}>先在 SSP 的 WOSS 标签页登记 source，再回到这里应用其地形资产。</div>
              )}
            </div>
          )}
          {bathStatus && <div style={{ fontSize: 10, padding: '4px 8px', borderRadius: 6, marginTop: 4, background: bathStatus.startsWith('✓') ? 'rgba(34,197,94,0.1)' : 'rgba(239,68,68,0.1)', border: `1px solid ${bathStatus.startsWith('✓') ? 'rgba(34,197,94,0.25)' : 'rgba(239,68,68,0.25)'}`, color: bathStatus.startsWith('✓') ? '#86efac' : '#fca5a5' }}>{bathStatus}</div>}
          {bathTab === 'preset' && <div style={{ display: 'grid', gap: 4 }}><div style={{ fontSize: 10, color: '#94a3b8' }}>草稿预览：仅用于离线构建，成功入库后场景再引用。</div><BathymetryChart data={draftBathymetryPreview} /></div>}
          {bathData && bathData.range_m && bathData.range_m.length > 1 && <BathymetryChart data={bathData} />}
        </div>

        {/* ── 离线阶段 / 3. 采样范围与数据库命名 ── */}
        <div className="environment-builder-card">
          <BuilderStageHeader phase="步骤 3" title="数据库命名与中心频率" description="这里只保留环境库名称、说明和中心频率。边界参数容器会跟随步骤 4 中当前选中的生成模式展开。" status={pipelineMode === 'analytical' ? '下一步填写解析参数' : '下一步填写 Bellhop 参数'} tone="info" />
          <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5, marginBottom: 6, padding: '4px 8px', background: 'rgba(34,211,238,0.05)', borderRadius: 6, border: '1px solid rgba(34,211,238,0.12)' }}>环境库名称与中心频率是两种生成模式都会用到的公共信息；其余传播参数已移到步骤 4 的当前模式面板中，避免整页长期显示无关字段。</div>
          <div style={{ display: 'grid', gap: 8 }}>
            <FieldRow label="环境数据库名称"><input type="text" value={databaseName} onChange={(e) => setDatabaseName(e.target.value)} placeholder="harbor_env_v1" style={selectStyle} /></FieldRow>
            <FieldRow label="说明"><textarea rows={3} value={databaseDescription} onChange={(e) => setDatabaseDescription(e.target.value)} placeholder="例如：浅海港口、ridge 地形、12kHz、春季 SSP" style={{ ...selectStyle, resize: 'vertical', minHeight: 72 }} /></FieldRow>
            <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
              <FieldRow label="频率" hint="kHz" description="传播网格对应的中心声学频率，会影响解析近似吸收估算以及 Bellhop 的声线计算。"><NumInput value={gridFreq} onChange={setGridFreq} min={0.1} max={200} step={0.5} /></FieldRow>
              <div style={{ display: 'grid', alignContent: 'end', fontSize: 10, color: '#94a3b8', lineHeight: 1.6, padding: '0 4px 6px' }}>
                当前模式：{pipelineMode === 'analytical' ? '解析近似' : 'Bellhop 声线追踪'}。点击下方模式标签后，对应参数面板会在其下方展开。
              </div>
            </div>
            {builderGridFile && <div style={{ fontSize: 10, color: '#86efac' }}>最近一次 Bellhop 输出网格：{builderGridFile}</div>}
          </div>
        </div>

        {/* ── 离线阶段 / 4. 生成传播网格并入库 ── */}
        <div className="environment-builder-card environment-builder-card--final">
          <BuilderStageHeader phase="步骤 4" title="生成传播网格并入库" description="最后执行解析网格或 Bellhop 追踪，把结果固化为可复用环境库。" status={workflowReady ? (pipelineMode === 'bellhop' ? '可直接运行 Bellhop' : '可直接生成解析网格') : '仍需补齐生成条件'} tone={workflowReady ? 'ready' : 'pending'} />
          {environmentCapabilities && (
            <div style={{ ...warnBoxStyle, color: environmentCapabilities.bellhop.available ? '#86efac' : '#fcd34d', marginBottom: 8 }}>
              Bellhop 状态: {environmentCapabilities.bellhop.available ? '可用' : '未就绪'}
              {environmentCapabilities.bellhop.resolved_path ? `，解析路径：${environmentCapabilities.bellhop.resolved_path}` : '，未检测到可执行文件'}。
              WOSS source: {environmentCapabilities.woss.profiles_count} 个，缓存记录：{environmentCapabilities.woss.cache_records_count ?? 0} 个，模式：{environmentCapabilities.woss.integration_mode}。
            </div>
          )}
          <FieldRow label="生成模式">
            <TabBar
              tabs={[
                { key: 'analytical', label: '解析近似（无需安装）' },
                { key: 'bellhop', label: 'Bellhop 声线追踪' },
              ]}
              active={pipelineMode}
              onChange={(key) => setPipelineMode(key as 'analytical' | 'bellhop')}
            />
          </FieldRow>
          {pipelineMode === 'analytical' && (
            <div style={{ display: 'grid', gap: 10 }}>
              <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5, padding: '4px 8px', background: 'rgba(34,211,238,0.05)', borderRadius: 6, border: '1px solid rgba(34,211,238,0.12)', marginBottom: 6 }}>
                解析近似使用 Thorp 吸收 + 球面扩展模型直接生成传播网格，无需安装 Bellhop。适合快速验证和原型场景。
              </div>
              {rangeDepthSummary}
              <div className="environment-builder-summary environment-mode-panel">
                <div className="environment-mode-panel__title">解析近似参数</div>
                <div className="environment-mode-panel__description">
                  只有在选择“解析近似（无需安装）”时，才需要填写这一组经验参数。它们会影响经验衰减估算和生成出的解析传播网格。
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                  <FieldRow label="最大距离" hint="m" description={autoManagedRange ? `已按当前 WOSS 地形实际覆盖范围自动锁定为 ${Math.round(effectiveGridRange)} m。` : '若当前已导入 WOSS/真实地形，后续会自动回填到实际覆盖距离。'}><NumInput value={effectiveGridRange} onChange={setGridRange} min={100} max={100000} step={500} disabled={autoManagedRange} /></FieldRow>
                  <FieldRow label="最大深度" hint="m" description={autoManagedDepth ? `已按当前 SSP 与地形的共同覆盖深度自动锁定为 ${Math.round(effectiveGridDepth)} m。` : '若当前已导入 WOSS/真实资产，后续会自动回填到真实资产共同可覆盖的最大深度。'}><NumInput value={effectiveGridDepth} onChange={setGridDepth} min={5} max={11000} step={10} disabled={autoManagedDepth} /></FieldRow>
                  <FieldRow label="水深" hint="m" description={autoManagedDepth ? `默认直接跟随共同覆盖深度，当前自动值 ${Math.round(effectiveWaterDepth)} m。` : '默认会先跟随最大深度；若当前环境不是自动推导模式，也可以在这里手动微调。'} warning={!autoManagedDepth && effectiveWaterDepth > effectiveGridDepth ? '当前水深已超过最大深度，建议下调水深或提高最大深度。' : null}><NumInput value={effectiveWaterDepth} onChange={(v) => onUpdateTransmissionParam('water_depth_m', String(v))} min={1} max={11000} step={10} disabled={autoManagedDepth} /></FieldRow>
                  <FieldRow label="声速" hint="m/s" description="解析近似模式会直接使用该常量声速；它不会读取 SSP 中的逐层声速变化。"><NumInput value={Number(p.sound_speed_mps) || 1500} onChange={(v) => onUpdateTransmissionParam('sound_speed_mps', String(v))} min={1400} max={1600} step={1} /></FieldRow>
                  <FieldRow label="吸收系数" hint="dB/km" description="用于经验衰减估算，主要影响解析近似模式；它不会从 WOSS/SSP 自动反推。"><NumInput value={Number(p.absorption_db_per_km) || 0.5} onChange={(v) => onUpdateTransmissionParam('absorption_db_per_km', String(v))} min={0} max={10} step={0.1} /></FieldRow>
                  <FieldRow label="扩展因子" hint="20=球面" description="控制经验扩展损失，20 对应球面扩展，10 更接近圆柱扩展。"><NumInput value={Number(p.spreading_factor) || 20} onChange={(v) => onUpdateTransmissionParam('spreading_factor', String(v))} min={10} max={40} step={5} /></FieldRow>
                </div>
                {sourceLevelField}
              </div>
              {coverageCheckPanel}
            </div>
          )}
          {pipelineMode === 'bellhop' && (
            <div style={{ display: 'grid', gap: 8 }}>
              {bellhopCandidates.length > 0 && (
                <FieldRow label="检测到的引擎" description="优先从系统配置、工作区和 PATH 中枚举可执行文件，通常直接选择即可。">
                  <select value={bellhopExe} onChange={(e) => setBellhopExe(e.target.value)} style={selectStyle}>
                    <option value="">— 使用自动检测 / 手工填写 —</option>
                    {!bellhopCandidates.some((candidate) => candidate.path === bellhopExe) && bellhopExe.trim() && (
                      <option value={bellhopExe}>{bellhopExe}</option>
                    )}
                    {bellhopCandidates.map((candidate) => (
                      <option key={`${candidate.source}-${candidate.path}`} value={candidate.path}>{candidate.label}</option>
                    ))}
                  </select>
                </FieldRow>
              )}
              <FieldRow label="BELLHOP 引擎路径" hint="兼容引擎完整路径" description="如需覆盖自动检测结果，可手工填写 bellhopcxx.exe 或其他兼容可执行文件路径。">
                <input type="text" value={bellhopExe} onChange={(e) => setBellhopExe(e.target.value)} placeholder={environmentCapabilities?.bellhop?.resolved_path || 'C:\acoustics\bellhop.exe'} style={selectStyle} />
              </FieldRow>
              <div className="environment-builder-summary environment-mode-panel">
                <div className="environment-mode-panel__title">Bellhop 运行参数</div>
                <div className="environment-mode-panel__description">
                  Bellhop 专属运行参数只保留声线反射次数等控制项；边界参数会在下方只读参数容器中自动带入，不再单独手填。
                </div>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                  <FieldRow label="声线反射次数" hint="声线触及海面/海底的最大次数" description="这是 Bellhop 搜索空间的上限，不是自动反演出的物理量；常规场景可先取 6-12，复杂多径环境可以继续增大，前端不再设硬上限。"><NumInput value={Number(p.max_bounces) || 3} onChange={(v) => onUpdateTransmissionParam('max_bounces', String(v))} min={0} step={1} /></FieldRow>
                </div>
                {sourceLevelField}
              </div>
              {coverageCheckPanel}
            </div>
          )}
          <button type="button" disabled={bellhopBusy || !workflowReady} onClick={handleRunBellhop} style={{ width: '100%', padding: '8px 0', borderRadius: 8, fontSize: 12, cursor: workflowReady ? 'pointer' : 'not-allowed', border: '1px solid rgba(34,211,238,0.5)', background: bellhopBusy ? 'rgba(34,211,238,0.05)' : 'rgba(34,211,238,0.18)', color: 'var(--accent)', fontWeight: 600, opacity: bellhopBusy || !workflowReady ? 0.55 : 1 }}>
            {bellhopBusy ? '处理中…' : pipelineMode === 'analytical' ? '生成解析网格并保存到环境库' : '运行 Bellhop 并保存到环境库'}
          </button>
          {!workflowReady && (
            <div style={warnBoxStyle}>
              {pipelineMode === 'analytical'
                ? '请先填写环境数据库名称（步骤 3）。'
                : '要生成环境数据库，至少需要 SSP 文件、数据库名称和 Bellhop 引擎路径。'}
            </div>
          )}
          {bellhopWarnings.length > 0 && (
            <div style={{ ...warnBoxStyle, color: '#fcd34d', display: 'grid', gap: 4 }}>
              {bellhopWarnings.map((warning) => <div key={warning}>{warning}</div>)}
            </div>
          )}
          {bellhopResult && <div style={{ marginTop: 6, padding: '6px 10px', borderRadius: 8, fontSize: 11, background: 'rgba(34,197,94,0.08)', border: '1px solid rgba(34,197,94,0.25)', color: '#86efac' }}>{bellhopResult}</div>}
          {bellhopError && <div style={{ marginTop: 6, padding: '6px 10px', borderRadius: 8, fontSize: 11, background: 'rgba(239,68,68,0.1)', border: '1px solid rgba(239,68,68,0.3)', color: '#fca5a5' }}>{bellhopError}</div>}
        </div>
          </>
        )}

      </div>
    </div>
  );
}

/* ─────────── Helpers ─────────── */

function getAssetsByKinds(sections: ModelLibrarySection[], kinds: string[]) {
  return sections.flatMap((section) => section.assets).filter((asset) => kinds.includes(asset.kind));
}

/* ─────────── PropertyInspector ─────────── */

export function PropertyInspector({
  selection,
  workingScenario,
  nodeTemplates,
  nodeBindings,
  edgeBindings,
  sceneBinding: _sceneBinding,
  previewMetrics,
  resultMetricEdgeKeys,
  sections,
  gridFiles,
  bathymetryFiles,
  environmentDatabases,
  environmentBounds,
  onUpdateNode,
  onUpdateNodeApp,
  onUpdateNodeBinding,
  onApplyNodeTemplate,
  onSaveNodeAsTemplate,
  onUpdateEdgeBinding,
  onUpdateScenarioMeta: _onUpdateScenarioMeta,
  onUpdateMeasurement,
  onSwitchTopology,
  onSetAllEdgeLinkProfile,
  onUpdateNoiseComposition,
  onUpdateNoiseParam,
  onUpdateTransmissionParam,
  onRefreshDataFiles,
  onAddNode,
  onRemoveSelectedNode,
  onOpenWizard,
  onSelectEnvironmentDatabase,
}: PropertyInspectorProps) {
  const linkAssets = getAssetsByKinds(sections, ['link-profile']);
  const noiseAssets = getAssetsByKinds(sections, ['noise-model']);

  const isP2P = workingScenario.topology.deployment_type === 'p2p';
  const isFullMesh = isP2P && workingScenario.topology.logical_type === 'full_mesh';
  const sinkCount = workingScenario.nodes.filter((n) => n.role === 'sink').length;
  const edgeCount = workingScenario.topology.pairs?.length ?? 0;

  if (selection.scope === 'node') {
    const node = workingScenario.nodes.find((n) => n.id === selection.nodeId);
    if (!node) return <div className="inspector-panel"><p style={{ color: 'var(--text-dim)', fontSize: 13 }}>节点不存在</p></div>;

    const binding = nodeBindings[node.id] ?? { nodeId: node.id, overridesByAssetId: {} };
    const templateOptions = nodeTemplates.map((template) => ({
      value: template.id,
      label: `${template.name}${template.builtIn ? ' · 内置' : ''}`,
    }));
    const app = node.application;
    const appOptions = appOptionsByRole[node.role] ?? appOptionsByRole.sensor;
    const macOptions = macOptionsByRole[node.role] ?? macOptionsByRole.sensor;
    const routingOptions = routingOptionsByRole[node.role] ?? routingOptionsByRole.sensor;
    const currentMacAssetId = binding.macProtocolAssetId ?? macOptions.find((option) => MAC_ASSET_TO_PROTOCOL[option.value] === node.mac?.protocol)?.value ?? macOptions[0].value;
    const currentRoutingAssetId = binding.routingProtocolAssetId ?? routingOptions.find((option) => ROUTING_ASSET_TO_PROTOCOL[option.value] === node.routing?.protocol)?.value ?? routingOptions[0].value;
    const currentMacConfig = buildMacConfigFromAssetId(currentMacAssetId, node.mac);
    const currentRoutingConfig = buildRoutingConfigFromAssetId(currentRoutingAssetId, node.routing);
    const currentTemplateId = binding.nodeTemplateId ?? templateOptions[0]?.value ?? '';
    const isBellhopMode = workingScenario.transmission?.type === 'bellhop';
    const handleMacAssetChange = (value: string) => {
      onUpdateNodeBinding(node.id, { macProtocolAssetId: value });
      onUpdateNode(node.id, { mac: buildMacConfigFromAssetId(value, node.mac) });
    };
    const handleRoutingAssetChange = (value: string) => {
      onUpdateNodeBinding(node.id, { routingProtocolAssetId: value });
      onUpdateNode(node.id, { routing: buildRoutingConfigFromAssetId(value, node.routing) });
    };
    return (
      <div className="inspector-panel">
        <div className="inspector-panel__section">
          <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
            <p className="eyebrow" style={{ margin: 0 }}>节点属性</p>
            <span style={{ fontSize: 11, color: 'var(--text-dim)', background: 'rgba(148,163,184,0.1)', padding: '2px 8px', borderRadius: 8 }}>
              ID: {node.id}
            </span>
          </div>

          <SectionTitle label="节点模板与协议" />
          <div className="inspector-panel__form">
            <FieldRow label="节点模板">
              <Select value={currentTemplateId} onChange={(value) => onApplyNodeTemplate(node.id, value)} options={templateOptions} />
            </FieldRow>
            <div style={{ display: 'flex', gap: 6 }}>
              <button type="button" className="ghost-button" style={{ flex: 1, padding: '6px 8px', fontSize: 11, borderRadius: 8 }} onClick={() => onSaveNodeAsTemplate(node.id)}>
                保存当前为模板
              </button>
            </div>
            <FieldRow label="模板角色">
              <div style={{ ...selectStyle, opacity: 0.8 }}>{roleOptions.find((item) => item.value === node.role)?.label ?? node.role}</div>
            </FieldRow>
            <FieldRow label="MAC 协议" hint={`按 ${node.role} 自动收敛`}>
              <Select value={currentMacAssetId} onChange={handleMacAssetChange} options={macOptions} />
            </FieldRow>
            {currentMacAssetId === 'mac-aloha' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(34,211,238,0.2)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="最小退避" hint="ms">
                  <NumInput min={10} max={5000} step={10} value={currentMacConfig.backoff_min_ms ?? 250} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, backoff_min_ms: v } })} />
                </FieldRow>
                <FieldRow label="最大退避" hint="ms">
                  <NumInput min={10} max={5000} step={10} value={currentMacConfig.backoff_max_ms ?? 1000} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, backoff_max_ms: v } })} />
                </FieldRow>
              </div>
            )}
            {/* CSMA 参数 */}
            {currentMacAssetId === 'mac-csma' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(34,211,238,0.2)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="侦听窗口" hint="ms">
                  <NumInput min={10} max={3000} step={10} value={currentMacConfig.sense_duration_ms ?? 200} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, sense_duration_ms: v } })} />
                </FieldRow>
                <FieldRow label="最小退避" hint="ms">
                  <NumInput min={10} max={3000} step={10} value={currentMacConfig.backoff_min_ms ?? 150} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, backoff_min_ms: v } })} />
                </FieldRow>
                <FieldRow label="最大退避" hint="ms">
                  <NumInput min={10} max={5000} step={10} value={currentMacConfig.backoff_max_ms ?? 600} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, backoff_max_ms: v } })} />
                </FieldRow>
              </div>
            )}
            {/* TDMA 参数 */}
            {currentMacAssetId === 'mac-tdma' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(34,211,238,0.2)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="时隙" hint="ms">
                  <NumInput min={10} max={4000} step={10} value={currentMacConfig.slot_duration_ms ?? 800} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, slot_duration_ms: v } })} />
                </FieldRow>
                <FieldRow label="保护间隔" hint="ms">
                  <NumInput min={0} max={500} step={1} value={currentMacConfig.guard_ms ?? currentMacConfig.guard_time_ms ?? 50} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, guard_ms: v } })} />
                </FieldRow>
              </div>
            )}
            {/* Polling 参数 */}
            {currentMacAssetId === 'mac-polling' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(34,211,238,0.2)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="轮询间隔" hint="秒">
                  <NumInput min={0.1} max={30} step={0.5} value={currentMacConfig.poll_interval_s ?? 2.0} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, poll_interval_s: v } })} />
                </FieldRow>
                <FieldRow label="保护间隔" hint="ms">
                  <NumInput min={0} max={500} step={1} value={currentMacConfig.guard_ms ?? currentMacConfig.guard_time_ms ?? 50} onChange={(v) => onUpdateNode(node.id, { mac: { ...currentMacConfig, guard_ms: v } })} />
                </FieldRow>
              </div>
            )}
            <FieldRow label="路由协议" hint={`按 ${node.role} 自动收敛`}>
              <Select value={currentRoutingAssetId} onChange={handleRoutingAssetChange} options={routingOptions} />
            </FieldRow>
            {currentRoutingAssetId === 'routing-static' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(168,85,247,0.25)', marginTop: 4 }}>
                <FieldRow label="下一跳" hint="节点 ID，留空表示直连优先">
                  <NumInput min={0} max={999999} step={1} value={currentRoutingConfig.next_hop ?? 0} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, next_hop: v <= 0 ? undefined : v } })} />
                </FieldRow>
              </div>
            )}
            {/* Flooding 参数 */}
            {currentRoutingAssetId === 'routing-flooding' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(168,85,247,0.25)', marginTop: 4 }}>
                <FieldRow label="TTL 跳数" hint="跳">
                  <NumInput min={1} max={64} step={1} value={currentRoutingConfig.ttl ?? 10} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, ttl: v } })} />
                </FieldRow>
              </div>
            )}
            {/* AODV 参数 */}
            {currentRoutingAssetId === 'routing-aodv' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(168,85,247,0.25)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="Hello 周期" hint="秒">
                  <NumInput min={0.1} max={30} step={0.5} value={currentRoutingConfig.hello_interval_s ?? 1.0} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, hello_interval_s: v } })} />
                </FieldRow>
                <FieldRow label="路由超时" hint="秒">
                  <NumInput min={0.5} max={60} step={0.5} value={currentRoutingConfig.route_timeout_s ?? 3.0} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, route_timeout_s: v } })} />
                </FieldRow>
              </div>
            )}
            {/* OLSR 参数 */}
            {currentRoutingAssetId === 'routing-olsr' && (
              <div style={{ paddingLeft: 8, borderLeft: '2px solid rgba(168,85,247,0.25)', marginTop: 4, display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="Hello 周期" hint="秒">
                  <NumInput min={0.5} max={30} step={0.5} value={currentRoutingConfig.hello_interval_s ?? 2.0} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, hello_interval_s: v } })} />
                </FieldRow>
                <FieldRow label="TC 间隔" hint="秒">
                  <NumInput min={1} max={60} step={1} value={currentRoutingConfig.tc_interval_s ?? 5.0} onChange={(v) => onUpdateNode(node.id, { routing: { ...currentRoutingConfig, tc_interval_s: v } })} />
                </FieldRow>
              </div>
            )}
          </div>

          <SectionTitle label="位置坐标 (m)" />
          {environmentBounds && (
            <div style={{ fontSize: 10, color: '#607080', marginBottom: 6 }}>
              {`复用环境约束：X ${Math.round(environmentBounds.minX)}-${Math.round(environmentBounds.maxX)} m，Y ${Math.round(environmentBounds.minY)}-${Math.round(environmentBounds.maxY)} m，环境深度 0-${Math.round(environmentBounds.depthMax)} m，可编辑深度 0-${Math.round(environmentBounds.editableDepthMax)} m`}
            </div>
          )}
          {environmentBounds?.depthConstraintReason && (
            <div style={{ fontSize: 10, color: '#9a3412', marginBottom: 6 }}>
              {environmentBounds.depthConstraintReason}
            </div>
          )}
          <div className="inspector-panel__form" style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6 }}>
            <FieldRow label="X"><NumInput min={environmentBounds?.minX} max={environmentBounds?.maxX} value={node.position[0]} step={10} onChange={(value) => onUpdateNode(node.id, { position: [value, node.position[1], node.position[2]] })} /></FieldRow>
            <FieldRow label="Y"><NumInput min={environmentBounds?.minY} max={environmentBounds?.maxY} value={node.position[1]} step={10} onChange={(value) => onUpdateNode(node.id, { position: [node.position[0], value, node.position[2]] })} /></FieldRow>
            <FieldRow label="Z (深度)"><NumInput min={0} max={environmentBounds?.editableDepthMax} value={node.position[2]} step={5} onChange={(value) => onUpdateNode(node.id, { position: [node.position[0], node.position[1], value] })} /></FieldRow>
          </div>

          <SectionTitle label="物理层" />
          <div className="inspector-panel__form">
            <FieldRow label="发射功率" hint="dB">
              <NumInput min={100} max={220} step={1} value={node.tx_power_db ?? 170} onChange={(v) => onUpdateNode(node.id, { tx_power_db: v })} />
            </FieldRow>
            <FieldRow label="中心频率" hint="Hz">
              <NumInput min={1000} max={100000} step={500} value={node.center_frequency_hz ?? 12000} onChange={(v) => onUpdateNode(node.id, { center_frequency_hz: v })} />
            </FieldRow>
            <div style={{ position: 'relative' }}>
              <FieldRow label="通信范围" hint={isBellhopMode ? 'Bellhop 自动计算' : 'm'}>
                <NumInput min={100} step={50} value={node.communication_range_m ?? 1800} onChange={(value) => onUpdateNode(node.id, { communication_range_m: value })} disabled={isBellhopMode} />
              </FieldRow>
              {isBellhopMode && (
                <p style={{ fontSize: 10, color: 'rgba(34,211,238,0.6)', margin: '2px 0 0 0' }}>Bellhop 模式下由声线追踪自动确定，不可手动设置</p>
              )}
            </div>
          </div>

          <SectionTitle label="运动模型" />
          <div className="inspector-panel__form">
            <FieldRow label="移动性">
              <Select value={node.mobility?.model ?? 'static'} onChange={(value) => onUpdateNode(node.id, { mobility: { ...node.mobility, model: value } })} options={mobilityOptions} />
            </FieldRow>
            {node.mobility?.model === 'random_waypoint' && (
              <>
                <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                  <FieldRow label="最小速度" hint="m/s"><NumInput step={0.1} value={node.mobility.speed_min ?? 1} onChange={(value) => onUpdateNode(node.id, { mobility: { ...node.mobility, speed_min: value } })} /></FieldRow>
                  <FieldRow label="最大速度" hint="m/s"><NumInput step={0.1} value={node.mobility.speed_max ?? 3} onChange={(value) => onUpdateNode(node.id, { mobility: { ...node.mobility, speed_max: value } })} /></FieldRow>
                </div>
                <FieldRow label="停留时长" hint="秒">
                  <NumInput min={0} step={1} value={node.mobility.pause_s ?? 5} onChange={(value) => onUpdateNode(node.id, { mobility: { ...node.mobility, pause_s: value } })} />
                </FieldRow>
              </>
            )}
            {node.mobility?.model === 'constant_velocity' && (
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr 1fr', gap: 6 }}>
                <FieldRow label="Vx" hint="m/s"><NumInput step={0.1} value={node.mobility?.vx ?? 0} onChange={(v) => onUpdateNode(node.id, { mobility: { ...node.mobility, vx: v } })} /></FieldRow>
                <FieldRow label="Vy" hint="m/s"><NumInput step={0.1} value={node.mobility?.vy ?? 0} onChange={(v) => onUpdateNode(node.id, { mobility: { ...node.mobility, vy: v } })} /></FieldRow>
                <FieldRow label="Vz" hint="m/s"><NumInput step={0.1} value={node.mobility?.vz ?? 0} onChange={(v) => onUpdateNode(node.id, { mobility: { ...node.mobility, vz: v } })} /></FieldRow>
              </div>
            )}
          </div>

          <SectionTitle label="应用层" />
          <div className="inspector-panel__form">
            <FieldRow label="应用类型">
              <Select value={app?.type ?? appOptions[0]?.value ?? 'null'} onChange={(value) => onUpdateNodeApp(node.id, 'type', value)} options={appOptions} />
            </FieldRow>
            {app && app.type !== 'null' && app.type !== 'sink_aggregator' && (
              <div style={{ display: 'grid', gridTemplateColumns: '1fr 1fr', gap: 6 }}>
                <FieldRow label="周期" hint="秒"><NumInput min={1} step={1} value={app.period_seconds ?? 10} onChange={(value) => onUpdateNodeApp(node.id, 'period_seconds', value)} /></FieldRow>
                <FieldRow label="包大小" hint="bytes"><NumInput min={64} max={4096} step={64} value={app.packet_size ?? 512} onChange={(value) => onUpdateNodeApp(node.id, 'packet_size', value)} /></FieldRow>
              </div>
            )}
          </div>
        </div>
      </div>
    );
  }

  if (selection.scope === 'edge') {
    const edgeKey = selection.edgeKey;
    const [txStr, rxStr] = edgeKey.split('-');
    const txId = parseInt(txStr, 10);
    const rxId = parseInt(rxStr, 10);
    const metric = previewMetrics.find((m) => m.tx_id === txId && m.rx_id === rxId);
    const binding = edgeBindings[edgeKey];
    const metricSource = resultMetricEdgeKeys.includes(edgeKey) ? 'results' : 'preview';

    return (
      <div className="inspector-panel">
        <div className="inspector-panel__section">
          <p className="eyebrow" style={{ margin: 0 }}>链路属性</p>
          <div style={{ fontSize: 12, color: 'var(--text-dim)', marginTop: 4 }}>节点 {txId} → 节点 {rxId}</div>

          <SectionTitle label="链路画像" />
          <AssetButtons assets={linkAssets} activeId={binding?.linkProfileAssetId} onSelect={(asset) => onUpdateEdgeBinding(edgeKey, { linkProfileAssetId: asset.id })} />

          {metric && (
            <>
              <SectionTitle label={metricSource === 'results' ? '当前场景结果' : '编排预估'} />
              <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5, marginBottom: 6, padding: '4px 8px', background: metricSource === 'results' ? 'rgba(34,197,94,0.08)' : 'rgba(148,163,184,0.08)', borderRadius: 6, border: `1px solid ${metricSource === 'results' ? 'rgba(34,197,94,0.2)' : 'rgba(148,163,184,0.16)'}` }}>
                {metricSource === 'results'
                  ? '以下数值来自当前场景最近一次仿真结果文件。'
                  : '以下数值为前端根据当前链路画像生成的预估值；未加载 Bellhop 射线时，多径数量不是实算结果。'}
              </div>
              <div style={{ display: 'grid', gap: 6 }}>
                {([
                  ['传播时延', `${metric.delay_s.toFixed(4)} s`],
                  ['接收功率', `${metric.received_level_db.toFixed(2)} dB`],
                  ['伪距', `${metric.pseudo_range_m.toFixed(1)} m`],
                  [metricSource === 'results' ? '多径' : '预估多径', `${metric.multipath_count} 条`],
                  ...(metric.noise_level_db !== undefined ? [['噪声级', `${metric.noise_level_db.toFixed(1)} dB`]] : []),
                  ...(metric.snr_db !== undefined ? [['信噪比', `${metric.snr_db.toFixed(1)} dB`]] : []),
                ] as [string, string][]).map(([k, v]) => (
                  <div key={k} style={{ display: 'flex', justifyContent: 'space-between', fontSize: 12 }}>
                    <span style={{ color: 'var(--text-dim)' }}>{k}</span>
                    <strong style={{ color: '#eff7ff' }}>{v}</strong>
                  </div>
                ))}
              </div>

              {metric.is_nlos !== undefined && (
                <>
                  <SectionTitle label="NLOS 状态" />
                  <div style={{
                    display: 'flex', alignItems: 'center', gap: 8,
                    padding: '8px 10px', borderRadius: 10,
                    background: metric.is_nlos === 1 ? 'rgba(239,68,68,0.12)' : 'rgba(34,197,94,0.1)',
                    border: `1px solid ${metric.is_nlos === 1 ? 'rgba(239,68,68,0.3)' : 'rgba(34,197,94,0.25)'}`,
                  }}>
                    <span style={{ width: 8, height: 8, borderRadius: '50%', background: metric.is_nlos === 1 ? '#ef4444' : '#22c55e', flexShrink: 0 }} />
                    <span style={{ fontSize: 12, color: '#eff7ff' }}>
                      {metric.is_nlos === 1 ? '非视距 (NLOS) — 直达路径受遮挡或衰减' : '视距 (LOS) — 直达路径可用'}
                    </span>
                  </div>
                </>
              )}
            </>
          )}
        </div>
      </div>
    );
  }

  return (
    <div className="inspector-panel">
      <div className="inspector-panel__section">
        <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center' }}>
          <p className="eyebrow" style={{ margin: 0 }}>场景属性</p>
          <div style={{ display: 'flex', gap: 4 }}>
            <button type="button" className="ghost-button" style={{ padding: '2px 8px', fontSize: 11, borderRadius: 6 }} onClick={onAddNode}>+ 节点</button>
            <button type="button" className="ghost-button" style={{ padding: '2px 8px', fontSize: 11, borderRadius: 6 }} disabled onClick={onRemoveSelectedNode}>— 删节点</button>
          </div>
        </div>

        <div style={{ display: 'flex', gap: 4, marginTop: 8, marginBottom: 4 }}>
          <button
            type="button"
            className="primary-button"
            style={{ flex: 1, padding: '5px 0', fontSize: 11, borderRadius: 8 }}
            onClick={onOpenWizard}
          >
            🧭 向导建场景
          </button>
        </div>

        <SectionTitle label="拓扑结构" />
        <div style={{ display: 'flex', gap: 6 }}>
          {([['star', '★ Star'], ['p2p', '↔ P2P'], ['full_mesh', '◆ Full-Mesh']] as const).map(([kind, label]) => {
            const active = kind === 'full_mesh' ? isFullMesh : kind === 'p2p' ? (isP2P && !isFullMesh) : !isP2P;
            return (
              <button
                key={kind}
                type="button"
                onClick={() => onSwitchTopology(kind)}
                style={{
                  flex: 1,
                  padding: '6px 0',
                  borderRadius: 8,
                  fontSize: 12,
                  cursor: 'pointer',
                  border: '1px solid',
                  background: active ? 'rgba(34,211,238,0.18)' : 'rgba(2,10,19,0.5)',
                  borderColor: active ? 'rgba(34,211,238,0.4)' : 'rgba(148,163,184,0.18)',
                  color: active ? 'var(--accent)' : 'var(--text-dim)',
                }}
              >
                {label}
              </button>
            );
          })}
        </div>

        <div style={{ fontSize: 12, color: 'var(--text-dim)', lineHeight: 1.6 }}>
          {isFullMesh
            ? <div>全连接 P2P：所有节点两两互测（{edgeCount} 条边），适合协作定位。</div>
            : isP2P
              ? <><div>当前通信组织：汇聚节点轮询互传（当前 {sinkCount} 个汇聚节点）</div><div>P2P 下默认允许多个汇聚节点同时存在。</div></>
              : <><div>当前通信组织：单汇聚节点一对多广播 / 汇聚</div><div>Star 下只保留一个汇聚节点作为中心。</div></>
          }
        </div>

        <SectionTitle label="传输模型" />
        <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.4, marginBottom: 4 }}>
          选择链路传播模型：「简化」使用固定参数，「Bellhop」使用声线追踪引擎进行多径传播仿真。
        </div>
        <AssetButtons
          assets={linkAssets}
          activeId={Object.values(edgeBindings)[0]?.linkProfileAssetId}
          onSelect={(asset) => onSetAllEdgeLinkProfile(asset.id)}
        />

        <SectionTitle label="环境噪声模型" />
        <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5, marginBottom: 4 }}>
          环境噪声影响所有节点的接收信噪比 (SNR)，从而影响伪距测量精度和通信可靠性。
        </div>
        <AssetButtons
          assets={noiseAssets}
          activeId={noiseAssets.find((a) => a.defaults?.type === workingScenario.noise.composition[0]?.type)?.id}
          onSelect={(asset) => onUpdateNoiseComposition(asset)}
        />
        {workingScenario.noise.composition[0] && (
          <div className="inspector-panel__form" style={{ display: 'grid', gap: 6, marginTop: 8 }}>
            {workingScenario.noise.composition[0].type === 'ambient_constant' && (
              <>
                <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.4 }}>
                  加性高斯白噪声 (AWGN)：各频率能量均匀分布的恒定噪声，设为 0 dB 即理想无噪声传播。
                </div>
                <FieldRow label="噪声级" hint="dB，0 = 无噪声，典型 40–60">
                  <NumInput
                    value={workingScenario.noise.composition[0].value_db ?? 52}
                    onChange={(v) => onUpdateNoiseParam('value_db', v)}
                    min={0} max={120} step={1}
                  />
                </FieldRow>
              </>
            )}
            {workingScenario.noise.composition[0].type === 'wenz' && (() => {
              const comp = workingScenario.noise.composition[0];
              const sf = comp.shipping_factor ?? 5;
              const ws = comp.wind_speed_mps ?? 8;
              const freq = comp.center_frequency_hz ?? 12000;
              const f0 = 100;
              const nlShip = (freq >= 10 && freq <= 50000) ? 76 - 20 * (Math.log10(freq) - Math.log10(f0)) + 5 * sf : -999;
              const nlWind = (freq >= 100 && freq <= 200000) ? 50 + 7.5 * Math.sqrt(ws) - 17 * Math.log10(freq / 1000) : -999;
              const nlThermal = (freq >= 1000) ? -15 + 20 * Math.log10(freq) : -999;
              let sumLin = 0;
              if (nlShip > -100) sumLin += Math.pow(10, nlShip / 10);
              if (nlWind > -100) sumLin += Math.pow(10, nlWind / 10);
              if (nlThermal > -100) sumLin += Math.pow(10, nlThermal / 10);
              const totalDb = sumLin > 0 ? 10 * Math.log10(sumLin) : 0;
              const wenzPresets = [
                { label: '深海静谧', hint: '航运 1 / 风 2 m/s', sf: 1, ws: 2, freq: 12000 },
                { label: '近岸中等', hint: '航运 4 / 风 5 m/s', sf: 4, ws: 5, freq: 12000 },
                { label: '繁忙港口', hint: '航运 7 / 风 8 m/s', sf: 7, ws: 8, freq: 12000 },
                { label: '远洋大风', hint: '航运 2 / 风 15 m/s', sf: 2, ws: 15, freq: 12000 },
              ];
              return (
                <>
                  <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.4, marginBottom: 2 }}>
                    Wenz (1962) 经验谱：根据航运密度、海面风速与热噪声三分量在指定频率处计算等效噪声级。
                  </div>
                  <div style={{ display: 'flex', gap: 4, flexWrap: 'wrap', marginBottom: 4 }}>
                    {wenzPresets.map((p) => (
                      <button key={p.label} type="button" style={{
                        fontSize: 10, padding: '3px 8px', borderRadius: 6, cursor: 'pointer',
                        border: '1px solid rgba(148,163,184,0.2)', background: 'rgba(2,10,19,0.5)', color: '#94a3b8',
                      }} onClick={() => {
                        onUpdateNoiseParam('shipping_factor', p.sf);
                        onUpdateNoiseParam('wind_speed_mps', p.ws);
                        onUpdateNoiseParam('center_frequency_hz', p.freq);
                      }} title={p.hint}>
                        {p.label}
                      </button>
                    ))}
                  </div>
                  <FieldRow label="航运活动因子" hint="0–7">
                    <NumInput
                      value={sf}
                      onChange={(v) => onUpdateNoiseParam('shipping_factor', v)}
                      min={0} max={7} step={0.5}
                    />
                  </FieldRow>
                  <div style={{ fontSize: 9, color: '#607080', lineHeight: 1.4, marginTop: -2, marginBottom: 2, paddingLeft: 2 }}>
                    低频段 10 Hz–1 kHz 主导。0–1 开阔大洋无航运；3–4 中等密度；6–7 繁忙航道/港口。
                  </div>
                  <FieldRow label="风速" hint="0–30 m/s">
                    <NumInput
                      value={ws}
                      onChange={(v) => onUpdateNoiseParam('wind_speed_mps', v)}
                      min={0} max={30} step={0.5}
                    />
                  </FieldRow>
                  <div style={{ fontSize: 9, color: '#607080', lineHeight: 1.4, marginTop: -2, marginBottom: 2, paddingLeft: 2 }}>
                    中高频 500 Hz–50 kHz 主导。0 平静海面；5–8 中等海况；15+ 大风/风暴。
                  </div>
                  <FieldRow label="中心频率" hint="Hz">
                    <NumInput
                      value={freq}
                      onChange={(v) => onUpdateNoiseParam('center_frequency_hz', v)}
                      min={100} max={100000} step={500}
                    />
                  </FieldRow>
                  <div style={{ fontSize: 9, color: '#607080', lineHeight: 1.4, marginTop: -2, marginBottom: 2, paddingLeft: 2 }}>
                    &lt;500 Hz 航运噪声为主；500 Hz–50 kHz 风生噪声为主；&gt;50 kHz 热噪声为主。
                  </div>
                  <div style={{
                    marginTop: 4, padding: '6px 10px', borderRadius: 8,
                    background: 'rgba(34,211,238,0.06)', border: '1px solid rgba(34,211,238,0.15)',
                    display: 'grid', gap: 3, fontSize: 11,
                  }}>
                    <div style={{ fontSize: 10, color: '#94a3b8', marginBottom: 2 }}>▸ 实时噪声级估算（与 C++ Wenz 引擎一致）</div>
                    {nlShip > -100 && <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: '#94a3b8' }}>航运噪声</span><strong style={{ color: '#eff7ff' }}>{nlShip.toFixed(1)} dB</strong></div>}
                    {nlWind > -100 && <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: '#94a3b8' }}>风生噪声</span><strong style={{ color: '#eff7ff' }}>{nlWind.toFixed(1)} dB</strong></div>}
                    {nlThermal > -100 && <div style={{ display: 'flex', justifyContent: 'space-between' }}><span style={{ color: '#94a3b8' }}>热噪声</span><strong style={{ color: '#eff7ff' }}>{nlThermal.toFixed(1)} dB</strong></div>}
                    <div style={{ display: 'flex', justifyContent: 'space-between', borderTop: '1px solid rgba(148,163,184,0.15)', paddingTop: 3, marginTop: 2 }}>
                      <span style={{ color: 'var(--accent)', fontWeight: 600 }}>合计噪声级</span>
                      <strong style={{ color: 'var(--accent)', fontSize: 13 }}>{totalDb.toFixed(1)} dB</strong>
                    </div>
                  </div>
                </>
              );
            })()}
          </div>
        )}

        <SectionTitle label="量测噪声" />
        <div style={{ fontSize: 10, color: '#94a3b8', lineHeight: 1.5, marginBottom: 4 }}>
          以下参数模拟定位算法中的测量误差，影响所有节点的伪距观测精度和航位推算误差积累。
        </div>
        <div className="inspector-panel__form" style={{ display: 'grid', gap: 8 }}>
          <label>
            <span>量测引擎</span>
            <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.3, marginBottom: 2 }}>
              默认引擎保持基础链路量测；自适应测距引擎会在多径/NLOS 条件下引入更稳健的参考时延和偏置建模。
            </div>
            <select
              value={workingScenario.measurement.engine_name ?? 'default_measurement_engine'}
              onChange={(event) => onUpdateMeasurement('engine_name', event.target.value)}
              style={selectStyle}
            >
              <option value="default_measurement_engine">default_measurement_engine</option>
              <option value="adaptive_ranging_engine">adaptive_ranging_engine</option>
            </select>
          </label>
          <label>
            <span>伪距量测噪声 σ (m)</span>
            <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.3, marginBottom: 2 }}>
              每次伪距测量的随机误差标准差，影响所有链路的测距精度。典型值: 0.5–5 m。
            </div>
            <NumInput
              value={workingScenario.measurement.noise_std ?? 0}
              onChange={(v) => onUpdateMeasurement('noise_std', v)}
              min={0} step={0.1}
            />
          </label>
          <label>
            <span>航位推算 (DR) 过程噪声 σ (m/√s)</span>
            <div style={{ fontSize: 10, color: '#607080', lineHeight: 1.3, marginBottom: 2 }}>
              模拟惯性导航的位置漂移速率，值越大，节点估计位置偏移越快。典型值: 0.1–1 m/√s。
            </div>
            <NumInput
              value={workingScenario.measurement.dr_noise_std ?? 0}
              onChange={(v) => onUpdateMeasurement('dr_noise_std', v)}
              min={0} step={0.1}
            />
          </label>
        </div>
      </div>

      {workingScenario.transmission?.type === 'bellhop' && (
        <BellhopParamsPanel
          workingScenario={workingScenario}
          gridFiles={gridFiles}
          bathymetryFiles={bathymetryFiles}
          environmentDatabases={environmentDatabases}
          onSelectEnvironmentDatabase={onSelectEnvironmentDatabase}
          onUpdateTransmissionParam={onUpdateTransmissionParam}
          onRefreshDataFiles={onRefreshDataFiles}
          mode="selector"
        />
      )}

      {workingScenario.transmission?.type && workingScenario.transmission.type !== 'bellhop' && (
        <div className="inspector-panel__section">
          <p className="eyebrow" style={{ margin: 0 }}>传播模型说明</p>
          <div style={{ fontSize: 11, color: '#94a3b8', lineHeight: 1.6, padding: '8px 10px', background: 'rgba(34,211,238,0.05)', borderRadius: 8, border: '1px solid rgba(34,211,238,0.12)' }}>
            当前场景使用「<strong style={{ color: '#eff7ff' }}>{workingScenario.transmission.type === 'simple' ? '简化传播' : workingScenario.transmission.type}</strong>」模型，不使用 Bellhop 声线追踪引擎。
            <br />
            简化模型使用固定时延和路径损耗，适合快速原型验证。若需多径传播、NLOS 识别等声学特性，请将上方「传输模型」切换为 Bellhop。
          </div>
        </div>
      )}


    </div>
  );
}
