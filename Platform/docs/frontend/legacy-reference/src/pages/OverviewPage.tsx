import { useState } from 'react';
import { Link, useNavigate } from 'react-router-dom';

import { MetricCard } from '../components/MetricCard';
import { SectionCard } from '../components/SectionCard';
import { TopologyPreview } from '../components/TopologyPreview';
import { ScenarioWizard } from '../components/ScenarioWizard';
import { deriveScenario } from '../services/api';
import type { DemoDataset } from '../types';

interface OverviewPageProps {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
}

export function OverviewPage({ dataset, activeScenario, loadScenario }: OverviewPageProps) {
  const { scenario, metrics } = dataset;
  const navigate = useNavigate();
  const [showDerive, setShowDerive] = useState(false);
  const [deriveName, setDeriveName] = useState('');
  const [deriving, setDeriving] = useState(false);
  const [deriveError, setDeriveError] = useState<string | null>(null);
  const [wizardOpen, setWizardOpen] = useState(false);
  const hasMetrics = metrics.length > 0;

  async function handleDerive() {
    const name = deriveName.trim();
    if (!name) return;
    setDeriving(true);
    setDeriveError(null);
    try {
      await deriveScenario(activeScenario, name);
      await loadScenario(name);
      navigate('/config');
    } catch (e) {
      setDeriveError((e as Error).message);
    } finally {
      setDeriving(false);
    }
  }
  const averageDelay = hasMetrics ? metrics.reduce((sum, item) => sum + item.delay_s, 0) / metrics.length : null;
  const averagePower = hasMetrics ? metrics.reduce((sum, item) => sum + item.received_level_db, 0) / metrics.length : null;
  const longestRange = hasMetrics ? Math.max(...metrics.map((item) => item.pseudo_range_m)) : null;
  const nlosCount = metrics.filter((m) => m.is_nlos === 1).length;

  return (
    <div className="page-grid page-grid--overview">
      <section className="hero-panel">
        <p className="eyebrow">总览首页</p>
        <h2>{scenario.scenario_metadata.name}</h2>
        <p className="hero-panel__lead">{scenario.scenario_metadata.description}</p>
        <div className="hero-panel__actions">
          <button type="button" className="primary-button" onClick={() => { setShowDerive(true); setDeriveName(`${activeScenario}_trial`); setDeriveError(null); }}>
            派生当前场景
          </button>
          <button type="button" className="ghost-button" onClick={() => navigate('/studio')}>
            进入模型编排
          </button>
        </div>
        {showDerive && (
          <div style={{ marginTop: 16, display: 'flex', flexDirection: 'column', gap: 8, maxWidth: 380 }}>
            <label style={{ fontSize: 12, color: 'var(--text-dim)' }}>派生后的场景名称（仅允许字母、数字、下划线、短横线）</label>
            <div style={{ display: 'flex', gap: 8 }}>
              <input
                className="form-grid__input"
                value={deriveName}
                onChange={(e) => setDeriveName(e.target.value)}
                style={{ flex: 1, padding: '6px 10px', background: 'var(--surface)', border: '1px solid rgba(148,163,184,0.2)', borderRadius: 8, color: 'var(--text)', fontSize: 13 }}
                placeholder="my_scenario_trial"
                disabled={deriving}
              />
              <button type="button" className="primary-button" onClick={() => void handleDerive()} disabled={deriving || !deriveName.trim()}>
                {deriving ? '创建中...' : '创建副本'}
              </button>
              <button type="button" className="ghost-button" onClick={() => setShowDerive(false)} disabled={deriving}>取消</button>
            </div>
            {deriveError && <span style={{ fontSize: 12, color: 'var(--danger)' }}>{deriveError}</span>}
          </div>
        )}
      </section>

      <div className="metrics-grid">
        <MetricCard label="平均传播时延" value={averageDelay !== null ? `${averageDelay.toFixed(3)} s` : '待运行'} detail={averageDelay !== null ? '来自当前链路结果的平均值' : '运行一次仿真后显示'} tone="accent" />
        <MetricCard label="平均接收强度" value={averagePower !== null ? `${averagePower.toFixed(2)} dB` : '待运行'} detail={averagePower !== null ? '来自当前量测结果的平均值' : '运行一次仿真后显示'} />
        <MetricCard label="最远伪距" value={longestRange !== null ? `${longestRange.toFixed(2)} m` : '待运行'} detail={longestRange !== null ? '当前最远观测链路' : '运行一次仿真后显示'} tone="warning" />
        {hasMetrics && nlosCount > 0 && (
          <MetricCard label="NLOS 链路" value={`${nlosCount} / ${metrics.length}`} detail="检测到非视距传播链路" tone="danger" />
        )}
      </div>

      <SectionCard title="平台态势" eyebrow="系统概览">
        <div className="status-grid">
          <div className="status-grid__item"><span>仿真核心</span><strong>ns-3 离散事件</strong></div>
          <div className="status-grid__item"><span>场景节点数</span><strong>{scenario.nodes.length} 个</strong></div>
          <div className="status-grid__item"><span>拓扑类型</span><strong>{scenario.topology.logical_type}</strong></div>
          <div className="status-grid__item"><span>调度器</span><strong>{scenario.simulation.scheduler}</strong></div>
          <div className="status-grid__item"><span>传播模型</span><strong>{scenario.transmission.type}</strong></div>
          <div className="status-grid__item"><span>链路记录数</span><strong>{metrics.length > 0 ? `${metrics.length} 条` : '待运行'}</strong></div>
        </div>
      </SectionCard>

      <SectionCard title="链路拓扑预览" eyebrow="拓扑预览">
        <TopologyPreview nodes={scenario.nodes} metrics={metrics} />
      </SectionCard>

      <SectionCard title="高频入口" eyebrow="快速开始">
        <div className="quick-actions">
          <button type="button" className="quick-action quick-action--wizard" onClick={() => setWizardOpen(true)}>
            <strong>向导建场景</strong>
            <span>用实验目标直观描述场景，自动生成完整配置</span>
          </button>
          <Link to="/environment" className="quick-action quick-action--link">
            <strong>进入环境库</strong>
            <span>构建或选择可复用环境库，并回写到场景</span>
          </Link>
          <Link to="/templates" className="quick-action quick-action--link">
            <strong>进入实验模板</strong>
            <span>把环境库、量测引擎和运行参数封装成上层试验模板</span>
          </Link>
          <Link to="/config" className="quick-action quick-action--link">
            <strong>进入场景配置</strong>
            <span>在模型编排工作台中编辑节点、链路和运行参数</span>
          </Link>
          <Link to="/monitor" className="quick-action quick-action--link">
            <strong>进入运行监视</strong>
            <span>查看日志、事件流和运行态</span>
          </Link>
          <Link to="/results" className="quick-action quick-action--link">
            <strong>进入结果分析</strong>
            <span>查看最新结果与归档摘要</span>
          </Link>
        </div>
      </SectionCard>

      <ScenarioWizard
        open={wizardOpen}
        onClose={() => setWizardOpen(false)}
        onCreated={async (name: string) => {
          await loadScenario(name);
          setWizardOpen(false);
          navigate('/studio');
        }}
      />
    </div>
  );
}