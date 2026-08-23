import { useEffect, useState } from 'react';

import { SectionCard } from '../components/SectionCard';
import { fetchStatus, saveScenario } from '../services/api';
import type { RunStatus } from '../services/api';
import type { DemoDataset } from '../types';

interface SettingsPageProps {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
}

type BackendHealth = 'checking' | 'online' | 'offline';

function formatRunPhase(phase: RunStatus['phase']) {
  if (phase === 'running') return '运行中';
  if (phase === 'done') return '已完成';
  if (phase === 'error') return '异常结束';
  return '空闲';
}

export function SettingsPage({ dataset, activeScenario, loadScenario }: SettingsPageProps) {
  const [health, setHealth] = useState<BackendHealth>('checking');
  const [status, setStatus] = useState<RunStatus | null>(null);
  const [archiveSaving, setArchiveSaving] = useState(false);
  const [archiveError, setArchiveError] = useState<string | null>(null);

  useEffect(() => {
    fetchStatus()
      .then((s) => {
        setStatus(s);
        setHealth('online');
      })
      .catch(() => setHealth('offline'));
  }, []);

  function handleRecheck() {
    setHealth('checking');
    setStatus(null);
    fetchStatus()
      .then((s) => {
        setStatus(s);
        setHealth('online');
      })
      .catch(() => setHealth('offline'));
  }

  async function handleArchiveToggle(nextValue: boolean) {
    setArchiveSaving(true);
    setArchiveError(null);
    try {
      await saveScenario(activeScenario, {
        ...dataset.scenario,
        output: {
          ...dataset.scenario.output,
          archive_experiment: nextValue,
        },
      });
      await loadScenario(activeScenario);
    } catch (e) {
      setArchiveError((e as Error).message);
    } finally {
      setArchiveSaving(false);
    }
  }

  const healthPill =
    health === 'checking'
      ? <span className="status-pill">检测中</span>
      : health === 'online'
        ? <span className="status-pill status-pill--ready">在线</span>
        : <span className="status-pill status-pill--warning">离线</span>;

  return (
    <div className="page-stack">
      <section className="hero-panel">
        <p className="eyebrow">系统设置</p>
        <h2>系统设置</h2>
        <p className="hero-panel__lead">
          这里统一查看平台连接状态，并维护当前场景的基础显示选项与归档开关。
        </p>
      </section>

      <SectionCard title="平台级设置" eyebrow="平台设置">
        <div className="settings-grid">
          <div className="settings-row"><span>当前场景</span><strong>{activeScenario}</strong></div>
          <div className="settings-row"><span>默认主题</span><strong>{dataset.scenario.ui.theme}</strong></div>
          <div className="settings-row"><span>3D 视图</span><strong>{dataset.scenario.ui.enable_3d_view ? '启用' : '未启用'}</strong></div>
          <div className="settings-row">
            <span>结果归档</span>
            <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, fontWeight: 600 }}>
              <input
                type="checkbox"
                checked={dataset.scenario.output.archive_experiment}
                disabled={archiveSaving}
                onChange={(event) => void handleArchiveToggle(event.target.checked)}
              />
              {archiveSaving ? '保存中...' : dataset.scenario.output.archive_experiment ? '开启' : '关闭'}
            </label>
          </div>
          <div className="settings-row"><span>输出模式</span><strong>{dataset.scenario.output.trace}</strong></div>
        </div>
        {archiveError ? <div style={{ marginTop: 10, color: 'var(--danger)', fontSize: 13 }}>归档设置保存失败：{archiveError}</div> : null}
      </SectionCard>

      <SectionCard
        title="服务健康检查"
        eyebrow="服务状态"
        actions={
          <button type="button" className="ghost-button" onClick={handleRecheck}>
            重新检测
          </button>
        }
      >
        <div className="status-grid">
          <div className="status-grid__item">
            <span>后端 API</span>
            <strong>{healthPill}</strong>
          </div>
          <div className="status-grid__item">
            <span>仿真进程</span>
            <strong>
              {health === 'online' && status
                ? status.phase === 'running'
                  ? <span className="status-pill status-pill--ready">运行中</span>
                  : <span className="status-pill">{formatRunPhase(status.phase)}</span>
                : health === 'offline'
                  ? <span className="status-pill status-pill--warning">不可达</span>
                  : <span className="status-pill">—</span>}
            </strong>
          </div>
          <div className="status-grid__item">
            <span>上次运行场景</span>
            <strong>{status?.scenario ?? '—'}</strong>
          </div>
          <div className="status-grid__item">
            <span>上次 exit_code</span>
            <strong>{status?.exit_code !== null && status?.exit_code !== undefined ? String(status.exit_code) : '—'}</strong>
          </div>
        </div>
      </SectionCard>
    </div>
  );
}