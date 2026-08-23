import { BrowserRouter, Route, Routes, useOutletContext } from 'react-router-dom';
import { useCallback, useEffect, useState } from 'react';

import { Shell } from './components/Shell';
import { loadDemoDataset } from './services/demoData';
import type { AppCtx, DemoDataset } from './types';
import { AssetsPage } from './pages/AssetsPage';
import { EnvironmentPage } from './pages/EnvironmentPage';
import { ExperimentTemplatesPage } from './pages/ExperimentTemplatesPage';
import { OverviewPage } from './pages/OverviewPage';
import { SettingsPage } from './pages/SettingsPage';
import { StudioPage } from './pages/StudioPage';

function useAppCtx() {
  return useOutletContext<AppCtx>();
}

function OverviewRoute() {
  const ctx = useAppCtx();
  return <OverviewPage dataset={ctx.dataset} activeScenario={ctx.activeScenario} loadScenario={ctx.loadScenario} />;
}

function StudioRoute() {
  const { dataset, activeScenario, loadScenario, notifyRunDone } = useAppCtx();
  return <StudioPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} notifyRunDone={notifyRunDone} />;
}

function ConfigRoute() {
  const { dataset, activeScenario, loadScenario, notifyRunDone } = useAppCtx();
  return <StudioPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} notifyRunDone={notifyRunDone} initialTab="config" />;
}

function MonitorRoute() {
  const { dataset, activeScenario, loadScenario, notifyRunDone } = useAppCtx();
  return <StudioPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} notifyRunDone={notifyRunDone} initialTab="run" />;
}

function ResultsRoute() {
  const { dataset, activeScenario, loadScenario, notifyRunDone } = useAppCtx();
  return <StudioPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} notifyRunDone={notifyRunDone} initialTab="results" />;
}

function AssetsRoute() {
  const ctx = useAppCtx();
  return (
    <AssetsPage
      dataset={ctx.dataset}
      activeScenario={ctx.activeScenario}
      loadScenario={ctx.loadScenario}
      runVersion={ctx.runVersion}
    />
  );
}

function EnvironmentRoute() {
  const { activeScenario } = useAppCtx();
  return <EnvironmentPage activeScenario={activeScenario} />;
}

function ExperimentTemplatesRoute() {
  const { dataset, activeScenario, loadScenario } = useAppCtx();
  return <ExperimentTemplatesPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} />;
}

function SettingsRoute() {
  const { dataset, activeScenario, loadScenario } = useAppCtx();
  return <SettingsPage dataset={dataset} activeScenario={activeScenario} loadScenario={loadScenario} />;
}

export default function App() {
  const [dataset, setDataset] = useState<DemoDataset | null>(null);
  const [activeScenario, setActiveScenario] = useState('star_monitoring_demo_simple');
  const [runVersion, setRunVersion] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [bootstrapNonce, setBootstrapNonce] = useState(0);

  useEffect(() => {
    let cancelled = false;
    async function bootstrap() {
      setError(null);
      let lastError: Error | null = null;
      for (let attempt = 1; attempt <= 5; attempt += 1) {
        try {
          const loaded = await loadDemoDataset();
          if (cancelled) return;
          setDataset(loaded);
          setActiveScenario(loaded.scenarioName);
          setError(null);
          return;
        } catch (err) {
          lastError = err instanceof Error ? err : new Error('未知错误');
          if (attempt < 5) {
            await new Promise((resolve) => window.setTimeout(resolve, attempt * 1000));
          }
        }
      }
      if (!cancelled && lastError) {
        setError(lastError.message);
      }
    }

    bootstrap();
    return () => { cancelled = true; };
  }, [bootstrapNonce]);

  const loadScenario = useCallback(async (name: string) => {
    const loaded = await loadDemoDataset(name);
    setDataset(loaded);
    setActiveScenario(name);
  }, []);

  const notifyRunDone = useCallback((metrics: import('./types').LinkMetric[]) => {
    setDataset((prev) => prev ? { ...prev, metrics } : prev);
    setRunVersion((v) => v + 1);
  }, []);

  if (error) {
    return (
      <div className="screen-state">
        <p style={{ fontWeight: 600, marginBottom: 8 }}>平台初始化失败</p>
        <p style={{ fontSize: 13, color: 'var(--text-dim)', marginBottom: 12 }}>
          无法连接仿真后端：{error}
        </p>
        <div style={{ display: 'flex', gap: 8, alignItems: 'center', flexWrap: 'wrap', justifyContent: 'center' }}>
          <button
            type="button"
            className="primary-button"
            style={{ padding: '8px 14px', borderRadius: 8, fontSize: 12 }}
            onClick={() => setBootstrapNonce((value) => value + 1)}
          >
            重试连接
          </button>
          <code style={{ fontSize: 12, background: 'var(--surface)', padding: '6px 10px', borderRadius: 8 }}>
            运行 start.bat / start_backend.bat，或在 PowerShell 中执行：cd backend; & ..\.venv\Scripts\python.exe -m uvicorn main:app --host 127.0.0.1 --port 8000
          </code>
        </div>
      </div>
    );
  }

  if (!dataset) {
    return <div className="screen-state">正在连接仿真引擎...</div>;
  }

  const ctx: AppCtx = { dataset, activeScenario, loadScenario, notifyRunDone, runVersion };

  return (
    <BrowserRouter>
      <Routes>
        <Route path="/" element={<Shell ctx={ctx} />}>
          <Route index element={<OverviewRoute />} />
          <Route path="studio" element={<StudioRoute />} />
          <Route path="environment" element={<EnvironmentRoute />} />
          <Route path="templates" element={<ExperimentTemplatesRoute />} />
          <Route path="config" element={<ConfigRoute />} />
          <Route path="monitor" element={<MonitorRoute />} />
          <Route path="results" element={<ResultsRoute />} />
          <Route path="assets" element={<AssetsRoute />} />
          <Route path="settings" element={<SettingsRoute />} />
        </Route>
      </Routes>
    </BrowserRouter>
  );
}