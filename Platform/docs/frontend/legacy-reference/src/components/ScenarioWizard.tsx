import { useEffect, useMemo, useState } from 'react';

import { generateScenario } from '../services/api';

type Purpose = 'nlos_test' | 'los_baseline' | 'general';
type TopologyType = 'p2p' | 'star' | 'full_mesh';
type TerrainType = 'flat' | 'ridge' | 'slope' | 'trench';

interface ScenarioWizardProps {
  open: boolean;
  initialName?: string;
  onClose: () => void;
  onCreated: (name: string) => Promise<void>;
}

interface WizardFormState {
  name: string;
  purpose: Purpose;
  nodeCount: number;
  topologyType: TopologyType;
  terrainType: TerrainType;
  waterDepthM: number;
  areaRangeM: number;
  nodeDepthM: number;
  nlosRatio: number;
}

const PRESETS: Array<{
  id: string;
  title: string;
  description: string;
  apply: Partial<WizardFormState>;
}> = [
  {
    id: 'bias-separation',
    title: '4 UUV 偏置分离测试',
    description: '1 个主参考节点，3 个协同 UUV，利用山脊制造部分链路 NLOS。适合检验测距偏置分离能力。',
    apply: {
      purpose: 'nlos_test',
      nodeCount: 4,
      topologyType: 'full_mesh',
      terrainType: 'ridge',
      waterDepthM: 100,
      areaRangeM: 6000,
      nodeDepthM: 30,
      nlosRatio: 0.4,
    },
  },
  {
    id: 'baseline',
    title: 'LOS 基线对照',
    description: '平坦海底，链路尽量保持直视，用于和 NLOS 场景做算法对照。',
    apply: {
      purpose: 'los_baseline',
      nodeCount: 4,
      topologyType: 'full_mesh',
      terrainType: 'flat',
      waterDepthM: 90,
      areaRangeM: 4000,
      nodeDepthM: 25,
      nlosRatio: 0.05,
    },
  },
  {
    id: 'corridor',
    title: '坡地巡航通道',
    description: '通过斜坡和较长航迹制造渐进式遮挡，适合观察运行过程中链路状态变化。',
    apply: {
      purpose: 'general',
      nodeCount: 4,
      topologyType: 'p2p',
      terrainType: 'slope',
      waterDepthM: 120,
      areaRangeM: 8000,
      nodeDepthM: 35,
      nlosRatio: 0.3,
    },
  },
];

function buildInitialState(initialName: string): WizardFormState {
  return {
    name: initialName,
    purpose: 'nlos_test',
    nodeCount: 4,
    topologyType: 'full_mesh',
    terrainType: 'ridge',
    waterDepthM: 100,
    areaRangeM: 6000,
    nodeDepthM: 30,
    nlosRatio: 0.4,
  };
}

function estimateNlosLinks(nodeCount: number, topologyType: TopologyType, nlosRatio: number): number {
  const linkCount = topologyType === 'full_mesh'
    ? (nodeCount * (nodeCount - 1)) / 2
    : nodeCount - 1;
  return Math.max(1, Math.round(linkCount * nlosRatio));
}

function sanitizeScenarioName(value: string): string {
  return value.replace(/[^a-zA-Z0-9_-]/g, '_');
}

/* ── 客户端预览：镜像后端布局算法，实时绘制地形 + 节点 + 链路 ── */

interface PreviewNode {
  id: number;
  x: number;       // range position m
  z: number;       // depth negative m
  isNlos: boolean;
  label: string;
}

interface PreviewLink {
  from: number;
  to: number;
  nlos: boolean;
}

interface PreviewData {
  terrainRanges: number[];
  terrainDepths: number[];
  nodes: PreviewNode[];
  links: PreviewLink[];
  areaRange: number;
  waterDepth: number;
}

function computePreview(f: WizardFormState): PreviewData {
  const { nodeCount, terrainType, waterDepthM, areaRangeM, nodeDepthM, nlosRatio, topologyType } = f;

  // 1. terrain profile (same logic as backend)
  let featureHeight: number;
  let featureRange: number;
  if (terrainType === 'flat') {
    featureHeight = 0;
    featureRange = areaRangeM / 2;
  } else if (terrainType === 'ridge') {
    featureHeight = waterDepthM * 0.75;
    featureRange = areaRangeM * (0.3 + 0.2 * nlosRatio);
  } else if (terrainType === 'slope') {
    featureHeight = waterDepthM * 0.5;
    featureRange = areaRangeM / 2;
  } else {
    featureHeight = waterDepthM * 0.6;
    featureRange = areaRangeM * 0.5;
  }

  const nPts = 40;
  const terrainRanges: number[] = [];
  const terrainDepths: number[] = [];
  for (let i = 0; i < nPts; i++) {
    const r = (areaRangeM * i) / (nPts - 1);
    terrainRanges.push(r);
    if (terrainType === 'ridge') {
      const sigma = areaRangeM * 0.06;
      terrainDepths.push(Math.max(waterDepthM - featureHeight * Math.exp(-((r - featureRange) ** 2) / (2 * sigma ** 2)), 1));
    } else if (terrainType === 'slope') {
      terrainDepths.push(waterDepthM + (r / areaRangeM) * featureHeight);
    } else if (terrainType === 'trench') {
      const sigma = areaRangeM * 0.06;
      terrainDepths.push(waterDepthM + featureHeight * Math.exp(-((r - featureRange) ** 2) / (2 * sigma ** 2)));
    } else {
      terrainDepths.push(waterDepthM);
    }
  }

  // 2. node placement (mirror backend)
  const minDepth = Math.min(...terrainDepths);
  const minIdx = terrainDepths.indexOf(minDepth);
  const obstacleRange = minIdx > 0 ? terrainRanges[minIdx] : areaRangeM * 0.4;

  const nodes: PreviewNode[] = [];
  nodes.push({ id: 0, x: 0, z: -nodeDepthM, isNlos: false, label: 'UUV-0' });

  const nOthers = nodeCount - 1;
  const nNlos = Math.max(1, Math.round(nOthers * nlosRatio));
  const nLos = nOthers - nNlos;

  for (let i = 0; i < nLos; i++) {
    const frac = (i + 1) / (nLos + 1);
    const x = obstacleRange * frac * 0.8;
    nodes.push({ id: i + 1, x, z: -nodeDepthM, isNlos: false, label: `UUV-${i + 1}` });
  }
  for (let j = 0; j < nNlos; j++) {
    const frac = (j + 1) / (nNlos + 1);
    const x = obstacleRange + (areaRangeM - obstacleRange) * frac;
    nodes.push({ id: nLos + j + 1, x, z: -nodeDepthM, isNlos: true, label: `UUV-${nLos + j + 1}` });
  }

  // 3. links
  const links: PreviewLink[] = [];
  const nodeIds = nodes.map((n) => n.id);

  // helper: does the straight line from node A to node B pass below the terrain at any point?
  function isLinkNlos(aIdx: number, bIdx: number): boolean {
    const na = nodes[aIdx];
    const nb = nodes[bIdx];
    const xMin = Math.min(na.x, nb.x);
    const xMax = Math.max(na.x, nb.x);
    if (xMax - xMin < 1) return false;
    for (let k = 0; k < terrainRanges.length; k++) {
      const tr = terrainRanges[k];
      if (tr <= xMin || tr >= xMax) continue;
      const t = (tr - na.x) / (nb.x - na.x);
      const linkDepthAtR = Math.abs(na.z + t * (nb.z - na.z)); // positive depth
      const terrainDepthAtR = terrainDepths[k];
      if (linkDepthAtR >= terrainDepthAtR) return true; // link below terrain = blocked
    }
    return false;
  }

  if (topologyType === 'full_mesh') {
    for (let a = 0; a < nodeIds.length; a++) {
      for (let b = a + 1; b < nodeIds.length; b++) {
        links.push({ from: nodeIds[a], to: nodeIds[b], nlos: isLinkNlos(a, b) });
      }
    }
  } else {
    for (let i = 1; i < nodeIds.length; i++) {
      links.push({ from: 0, to: nodeIds[i], nlos: isLinkNlos(0, i) });
    }
  }

  return { terrainRanges, terrainDepths, nodes, links, areaRange: areaRangeM, waterDepth: waterDepthM };
}

const PREVIEW_W = 520;
const PREVIEW_H = 220;
const PREVIEW_PAD = { top: 18, right: 24, bottom: 28, left: 38 };
const PLOT_W = PREVIEW_W - PREVIEW_PAD.left - PREVIEW_PAD.right;
const PLOT_H = PREVIEW_H - PREVIEW_PAD.top - PREVIEW_PAD.bottom;

function WizardPreview({ data }: { data: PreviewData }) {
  const maxDepth = Math.max(...data.terrainDepths, data.waterDepth) * 1.15;

  function sx(range: number) {
    return PREVIEW_PAD.left + (range / data.areaRange) * PLOT_W;
  }
  function sy(depth: number) {
    // depth is positive-down
    return PREVIEW_PAD.top + (depth / maxDepth) * PLOT_H;
  }

  // terrain polygon
  const terrainPoints = data.terrainRanges
    .map((r, i) => `${sx(r).toFixed(1)},${sy(data.terrainDepths[i]).toFixed(1)}`)
    .join(' ');
  const terrainPolygon = `${sx(0).toFixed(1)},${(PREVIEW_PAD.top + PLOT_H).toFixed(1)} ${terrainPoints} ${sx(data.areaRange).toFixed(1)},${(PREVIEW_PAD.top + PLOT_H).toFixed(1)}`;

  // water surface line y
  const surfaceY = PREVIEW_PAD.top;

  return (
    <svg viewBox={`0 0 ${PREVIEW_W} ${PREVIEW_H}`} className="wizard-preview__svg">
      {/* water fill */}
      <rect x={PREVIEW_PAD.left} y={surfaceY} width={PLOT_W} height={PLOT_H} fill="rgba(14,165,233,0.08)" rx="4" />

      {/* terrain fill */}
      <polygon points={terrainPolygon} fill="rgba(148,163,184,0.18)" />
      <polyline
        points={data.terrainRanges.map((r, i) => `${sx(r).toFixed(1)},${sy(data.terrainDepths[i]).toFixed(1)}`).join(' ')}
        fill="none"
        stroke="rgba(148,163,184,0.5)"
        strokeWidth="1.5"
      />

      {/* surface label */}
      <text x={PREVIEW_PAD.left + 4} y={surfaceY + 12} fill="rgba(148,163,184,0.5)" fontSize="9">水面</text>
      <line x1={PREVIEW_PAD.left} y1={surfaceY} x2={PREVIEW_PAD.left + PLOT_W} y2={surfaceY} stroke="rgba(14,165,233,0.3)" strokeWidth="1" />

      {/* depth axis labels */}
      {[0, 0.5, 1].map((f) => {
        const d = f * maxDepth;
        const yPos = sy(d);
        return (
          <g key={f}>
            <line x1={PREVIEW_PAD.left - 4} y1={yPos} x2={PREVIEW_PAD.left} y2={yPos} stroke="rgba(148,163,184,0.3)" />
            <text x={PREVIEW_PAD.left - 6} y={yPos + 3} fill="rgba(148,163,184,0.5)" fontSize="8" textAnchor="end">{Math.round(d)}m</text>
          </g>
        );
      })}

      {/* range axis labels */}
      {[0, 0.5, 1].map((f) => {
        const r = f * data.areaRange;
        const xPos = sx(r);
        return (
          <g key={f}>
            <text x={xPos} y={PREVIEW_PAD.top + PLOT_H + 14} fill="rgba(148,163,184,0.5)" fontSize="8" textAnchor="middle">{(r / 1000).toFixed(1)}km</text>
          </g>
        );
      })}

      {/* links */}
      {data.links.map((link) => {
        const fromNode = data.nodes.find((n) => n.id === link.from)!;
        const toNode = data.nodes.find((n) => n.id === link.to)!;
        return (
          <line
            key={`${link.from}-${link.to}`}
            x1={sx(fromNode.x)}
            y1={sy(Math.abs(fromNode.z))}
            x2={sx(toNode.x)}
            y2={sy(Math.abs(toNode.z))}
            stroke={link.nlos ? 'rgba(251,113,133,0.7)' : 'rgba(52,211,153,0.5)'}
            strokeWidth={link.nlos ? 1.5 : 1}
            strokeDasharray={link.nlos ? '6 3' : 'none'}
          />
        );
      })}

      {/* nodes */}
      {data.nodes.map((node) => {
        const cx = sx(node.x);
        const cy = sy(Math.abs(node.z));
        return (
          <g key={node.id}>
            <circle cx={cx} cy={cy} r={6} fill={node.isNlos ? '#fb7185' : '#34d399'} fillOpacity={0.85} stroke="rgba(255,255,255,0.5)" strokeWidth="1" />
            <text x={cx} y={cy - 10} fill="#e7f4ff" fontSize="9" textAnchor="middle">{node.label}</text>
          </g>
        );
      })}

      {/* legend */}
      <g transform={`translate(${PREVIEW_PAD.left + PLOT_W - 130}, ${PREVIEW_PAD.top + PLOT_H - 28})`}>
        <line x1="0" y1="6" x2="18" y2="6" stroke="rgba(52,211,153,0.7)" strokeWidth="1.5" />
        <text x="22" y="9" fill="rgba(148,163,184,0.7)" fontSize="8">LOS 直视</text>
        <line x1="70" y1="6" x2="88" y2="6" stroke="rgba(251,113,133,0.8)" strokeWidth="1.5" strokeDasharray="5 2" />
        <text x="92" y="9" fill="rgba(148,163,184,0.7)" fontSize="8">NLOS 遮挡</text>
      </g>
    </svg>
  );
}

export function ScenarioWizard({ open, initialName = 'new_scenario', onClose, onCreated }: ScenarioWizardProps) {
  const [form, setForm] = useState<WizardFormState>(() => buildInitialState(initialName));
  const [submitting, setSubmitting] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [successNote, setSuccessNote] = useState<string | null>(null);

  useEffect(() => {
    if (!open) return;
    setForm(buildInitialState(initialName));
    setSubmitting(false);
    setError(null);
    setSuccessNote(null);
  }, [initialName, open]);

  const summary = useMemo(() => {
    const estimatedNlosLinks = estimateNlosLinks(form.nodeCount, form.topologyType, form.nlosRatio);
    const topologyLabel = form.topologyType === 'full_mesh' ? '全互测' : form.topologyType === 'star' ? '中心汇聚' : '主从测距';
    const terrainLabel = form.terrainType === 'ridge'
      ? '海底山脊'
      : form.terrainType === 'slope'
        ? '连续斜坡'
        : form.terrainType === 'trench'
          ? '海沟'
          : '平坦海底';
    return {
      estimatedNlosLinks,
      topologyLabel,
      terrainLabel,
      target: form.purpose === 'nlos_test' ? '重点制造 NLOS 偏置' : form.purpose === 'los_baseline' ? '构建 LOS 对照组' : '生成通用协同场景',
    };
  }, [form]);

  const previewData = useMemo(() => computePreview(form), [form]);

  if (!open) {
    return null;
  }

  async function handleSubmit() {
    const name = sanitizeScenarioName(form.name.trim());
    if (!name) {
      setError('请输入场景名称。');
      return;
    }

    setSubmitting(true);
    setError(null);
    setSuccessNote(null);
    try {
      const result = await generateScenario({
        name,
        purpose: form.purpose,
        node_count: form.nodeCount,
        topology_type: form.topologyType,
        terrain_type: form.terrainType,
        water_depth_m: form.waterDepthM,
        area_range_m: form.areaRangeM,
        node_depth_m: form.nodeDepthM,
        nlos_ratio: form.nlosRatio,
      });
      setSuccessNote(`场景 ${result.name} 已生成。下一步请在场景页或环境库页绑定环境数据库。`);
      await onCreated(result.name);
    } catch (requestError) {
      setError(requestError instanceof Error ? requestError.message : '场景生成失败。');
      setSubmitting(false);
    }
  }

  return (
    <div className="studio-modal__backdrop" onClick={onClose}>
      <div className="studio-modal studio-modal--wizard" onClick={(event) => event.stopPropagation()}>
        <div className="studio-modal__head">
          <div>
            <strong>直观场景向导</strong>
            <div className="wizard-subtitle">用实验目标和障碍意图描述网络场景，系统自动生成节点布局、拓扑和运行骨架；环境数据库后续独立绑定。</div>
          </div>
          <button type="button" className="ghost-button" onClick={onClose} disabled={submitting}>关闭</button>
        </div>

        <div className="scenario-wizard">
          <section className="scenario-wizard__section">
            <div className="scenario-wizard__section-head">
              <strong>1. 选择你的实验意图</strong>
              <span>先定义网络目标和遮挡意图，而不是先处理环境文件。</span>
            </div>
            <div className="scenario-wizard__presets">
              {PRESETS.map((preset) => (
                <button
                  key={preset.id}
                  type="button"
                  className={`scenario-wizard__preset${preset.apply.purpose === form.purpose && preset.apply.terrainType === form.terrainType ? ' scenario-wizard__preset--active' : ''}`}
                  onClick={() => setForm((prev) => ({ ...prev, ...preset.apply, name: prev.name }))}
                >
                  <strong>{preset.title}</strong>
                  <span>{preset.description}</span>
                </button>
              ))}
            </div>
          </section>

          <section className="scenario-wizard__section">
            <div className="scenario-wizard__section-head">
              <strong>2. 用直觉定义场景结构</strong>
              <span>你只需要决定有几台 UUV、怎么互测、障碍意图有多强。</span>
            </div>
            <div className="form-grid">
              <label>
                <span>场景名称</span>
                <input value={form.name} onChange={(event) => setForm((prev) => ({ ...prev, name: sanitizeScenarioName(event.target.value) }))} placeholder="uuv_bias_eval_01" />
              </label>
              <label>
                <span>实验目标</span>
                <select value={form.purpose} onChange={(event) => setForm((prev) => ({ ...prev, purpose: event.target.value as Purpose }))}>
                  <option value="nlos_test">制造 NLOS 偏置</option>
                  <option value="los_baseline">生成 LOS 对照</option>
                  <option value="general">通用协同场景</option>
                </select>
              </label>
              <label>
                <span>UUV 数量</span>
                <input type="number" min="2" max="20" value={form.nodeCount} onChange={(event) => setForm((prev) => ({ ...prev, nodeCount: Math.max(2, Math.min(20, Number(event.target.value) || 2)) }))} />
              </label>
              <label>
                <span>互联方式</span>
                <select value={form.topologyType} onChange={(event) => setForm((prev) => ({ ...prev, topologyType: event.target.value as TopologyType }))}>
                  <option value="full_mesh">全互测</option>
                  <option value="p2p">主节点对其余节点测距</option>
                  <option value="star">中心汇聚</option>
                </select>
              </label>
              <label>
                <span>海底障碍</span>
                <select value={form.terrainType} onChange={(event) => setForm((prev) => ({ ...prev, terrainType: event.target.value as TerrainType }))}>
                  <option value="ridge">海底山脊</option>
                  <option value="slope">连续斜坡</option>
                  <option value="trench">海沟</option>
                  <option value="flat">平坦海底</option>
                </select>
              </label>
              <label>
                <span>NLOS 强度</span>
                <input type="range" min="0.05" max="0.9" step="0.05" value={form.nlosRatio} onChange={(event) => setForm((prev) => ({ ...prev, nlosRatio: Number(event.target.value) }))} />
                <div className="wizard-inline-note">预计约 {Math.round(form.nlosRatio * 100)}% 链路穿过障碍区</div>
              </label>
              <label>
                <span>水深</span>
                <input type="number" min="20" max="500" value={form.waterDepthM} onChange={(event) => setForm((prev) => ({ ...prev, waterDepthM: Math.max(20, Number(event.target.value) || 20) }))} />
              </label>
              <label>
                <span>水平覆盖范围</span>
                <input type="number" min="1000" max="20000" step="500" value={form.areaRangeM} onChange={(event) => setForm((prev) => ({ ...prev, areaRangeM: Math.max(1000, Number(event.target.value) || 1000) }))} />
              </label>
              <label>
                <span>UUV 巡航深度</span>
                <input type="number" min="5" max="300" value={form.nodeDepthM} onChange={(event) => setForm((prev) => ({ ...prev, nodeDepthM: Math.max(5, Number(event.target.value) || 5) }))} />
              </label>
            </div>
          </section>

          <section className="scenario-wizard__section scenario-wizard__section--preview">
            <div className="scenario-wizard__section-head">
              <strong>实时预览</strong>
              <span>地形剖面 + 节点分布 + 链路 LOS/NLOS 判断</span>
            </div>
            <div className="wizard-preview">
              <WizardPreview data={previewData} />
            </div>
          </section>

          <section className="scenario-wizard__section scenario-wizard__section--summary">
            <div className="scenario-wizard__section-head">
              <strong>3. 系统将自动生成场景骨架</strong>
              <span>你确认实验语义，系统负责生成节点布局、拓扑和运行参数；环境数据库稍后在场景页或环境库页绑定。</span>
            </div>
            <div className="scenario-wizard__summary-grid">
              <div className="scenario-wizard__summary-card">
                <strong>实验目标</strong>
                <span>{summary.target}</span>
              </div>
              <div className="scenario-wizard__summary-card">
                <strong>网络结构</strong>
                <span>{form.nodeCount} 台 UUV，{summary.topologyLabel}</span>
              </div>
              <div className="scenario-wizard__summary-card">
                <strong>环境意图</strong>
                <span>{summary.terrainLabel}，水深 {form.waterDepthM} m，后续绑定环境库</span>
              </div>
              <div className="scenario-wizard__summary-card scenario-wizard__summary-card--accent">
                <strong>预计 NLOS 链路</strong>
                <span>{summary.estimatedNlosLinks} 条左右</span>
              </div>
            </div>
            <div className="scenario-wizard__list">
              <div>自动摆放节点，让部分链路跨越障碍</div>
              <div>自动写入拓扑、Bellhop 运行骨架和结果输出路径</div>
              <div>创建完成后，在场景页或环境库页选择已有环境数据库</div>
            </div>
            {successNote && <div className="scenario-wizard__error" style={{ color: 'var(--success)' }}>{successNote}</div>}
            {error && <div className="scenario-wizard__error">{error}</div>}
          </section>
        </div>

        <div className="scenario-wizard__actions">
          <button type="button" className="ghost-button" onClick={onClose} disabled={submitting}>取消</button>
          <button type="button" className="primary-button" onClick={() => void handleSubmit()} disabled={submitting}>
            {submitting ? '生成中...' : '生成并载入场景'}
          </button>
        </div>
      </div>
    </div>
  );
}