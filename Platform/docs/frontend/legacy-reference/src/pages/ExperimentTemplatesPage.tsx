import { useEffect, useMemo, useState } from 'react';

import { SectionCard } from '../components/SectionCard';
import {
  applyExperimentTemplate,
  createExperimentTemplate,
  deleteExperimentTemplate,
  deriveExperimentTemplate,
  fetchEnvironmentDatabases,
  fetchExperimentTemplates,
  fetchScenarios,
  updateExperimentTemplate,
  type UpsertExperimentTemplateParams,
} from '../services/api';
import type { DemoDataset, EnvironmentDatabase, ExperimentTemplate } from '../types';

interface ExperimentTemplatesPageProps {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
}

interface TemplateDraft {
  name: string;
  description: string;
  source_scenario_id: string;
  tagsText: string;
  transmission_type: string;
  environment_database_id: string;
  measurement_engine_name: string;
  duration: string;
  seed: string;
  time_step_ms: string;
  archive_experiment: boolean;
  default_dashboard: string;
  notes: string;
}

function buildDraftFromScenario(dataset: DemoDataset, activeScenario: string): TemplateDraft {
  const scenario = dataset.scenario;
  const params = scenario.transmission.params ?? {};
  const environmentDatabaseId = typeof params.environment_database_id === 'string'
    ? params.environment_database_id
    : '';
  return {
    name: `${activeScenario}_experiment`,
    description: `${scenario.scenario_metadata.name} 试验模板`,
    source_scenario_id: activeScenario,
    tagsText: scenario.scenario_metadata.project_tags.join(', '),
    transmission_type: scenario.transmission.type || 'bellhop',
    environment_database_id: environmentDatabaseId,
    measurement_engine_name: scenario.measurement.engine_name ?? 'default_measurement_engine',
    duration: String(scenario.simulation.duration ?? ''),
    seed: String(scenario.simulation.seed ?? ''),
    time_step_ms: String(scenario.simulation.time_step_ms ?? ''),
    archive_experiment: Boolean(scenario.output.archive_experiment),
    default_dashboard: scenario.ui.default_dashboard ?? '',
    notes: '',
  };
}

function buildDraftFromTemplate(template: ExperimentTemplate): TemplateDraft {
  return {
    name: template.name,
    description: template.description ?? '',
    source_scenario_id: template.source_scenario_id ?? '',
    tagsText: (template.tags ?? []).join(', '),
    transmission_type: template.bindings.transmission_type ?? 'bellhop',
    environment_database_id: template.bindings.environment_database_id ?? '',
    measurement_engine_name: template.bindings.measurement_engine_name ?? 'default_measurement_engine',
    duration: template.runtime.duration != null ? String(template.runtime.duration) : '',
    seed: template.runtime.seed != null ? String(template.runtime.seed) : '',
    time_step_ms: template.runtime.time_step_ms != null ? String(template.runtime.time_step_ms) : '',
    archive_experiment: Boolean(template.runtime.archive_experiment),
    default_dashboard: template.runtime.default_dashboard ?? '',
    notes: template.notes ?? '',
  };
}

function buildTemplatePayload(draft: TemplateDraft, dataset: DemoDataset, activeScenario: string): UpsertExperimentTemplateParams {
  const scenario = dataset.scenario;
  const tags = draft.tagsText
    .split(',')
    .map((item) => item.trim())
    .filter(Boolean);
  return {
    name: draft.name.trim(),
    description: draft.description.trim(),
    source_scenario_id: draft.source_scenario_id.trim() || activeScenario,
    tags,
    bindings: {
      transmission_type: draft.transmission_type.trim() || undefined,
      environment_database_id: draft.environment_database_id.trim() || undefined,
      measurement_engine_name: draft.measurement_engine_name.trim() || undefined,
    },
    runtime: {
      duration: draft.duration.trim() ? Number(draft.duration) : null,
      seed: draft.seed.trim() ? Number(draft.seed) : null,
      time_step_ms: draft.time_step_ms.trim() ? Number(draft.time_step_ms) : null,
      archive_experiment: draft.archive_experiment,
      default_dashboard: draft.default_dashboard.trim() || null,
    },
    summary: {
      node_count: scenario.nodes.length,
      topology_type: scenario.topology.logical_type,
      transmission_type: draft.transmission_type.trim() || scenario.transmission.type,
    },
    notes: draft.notes.trim(),
  };
}

function formatTimestamp(value: number) {
  const date = new Date(value * 1000);
  if (Number.isNaN(date.getTime())) return '未记录';
  return date.toLocaleString('zh-CN', { hour12: false });
}

function buildDerivedScenarioName(sourceScenarioId: string | null | undefined, activeScenario: string) {
  const base = (sourceScenarioId || activeScenario || 'scenario').trim();
  return `${base}_trial`;
}

function shouldExpandAdvancedRuntime(draft: TemplateDraft) {
  return Boolean(draft.seed || draft.time_step_ms || draft.default_dashboard || draft.archive_experiment);
}

export function ExperimentTemplatesPage({ dataset, activeScenario, loadScenario }: ExperimentTemplatesPageProps) {
  const [templates, setTemplates] = useState<ExperimentTemplate[]>([]);
  const [environmentDatabases, setEnvironmentDatabases] = useState<EnvironmentDatabase[]>([]);
  const [scenarioIds, setScenarioIds] = useState<string[]>([]);
  const [draft, setDraft] = useState<TemplateDraft>(() => buildDraftFromScenario(dataset, activeScenario));
  const [selectedTemplateId, setSelectedTemplateId] = useState<string | null>(null);
  const [deriveScenarioName, setDeriveScenarioName] = useState(() => buildDerivedScenarioName(activeScenario, activeScenario));
  const [showAdvancedRuntime, setShowAdvancedRuntime] = useState(() => shouldExpandAdvancedRuntime(buildDraftFromScenario(dataset, activeScenario)));
  const [loading, setLoading] = useState(true);
  const [saving, setSaving] = useState(false);
  const [applyingId, setApplyingId] = useState<string | null>(null);
  const [derivingId, setDerivingId] = useState<string | null>(null);
  const [deletingId, setDeletingId] = useState<string | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [status, setStatus] = useState<string | null>(null);

  function refreshTemplates(keepSelectionId?: string | null) {
    setLoading(true);
    return Promise.all([fetchExperimentTemplates(), fetchEnvironmentDatabases(), fetchScenarios()])
      .then(([templateItems, databases, scenarios]) => {
        setTemplates(templateItems);
        setEnvironmentDatabases(databases);
        setScenarioIds(scenarios);
        if (keepSelectionId !== undefined) {
          setSelectedTemplateId(keepSelectionId);
        }
      })
      .catch((nextError: Error) => setError(nextError.message))
      .finally(() => setLoading(false));
  }

  useEffect(() => {
    refreshTemplates(null).catch(() => {});
  }, []);

  useEffect(() => {
    if (!selectedTemplateId) {
      const nextDraft = buildDraftFromScenario(dataset, activeScenario);
      setDraft(nextDraft);
      setShowAdvancedRuntime(shouldExpandAdvancedRuntime(nextDraft));
      setDeriveScenarioName(buildDerivedScenarioName(activeScenario, activeScenario));
    }
  }, [activeScenario, dataset, selectedTemplateId]);

  const selectedTemplate = useMemo(
    () => templates.find((item) => item.id === selectedTemplateId) ?? null,
    [selectedTemplateId, templates],
  );

  function patchDraft<K extends keyof TemplateDraft>(key: K, value: TemplateDraft[K]) {
    setDraft((prev) => ({ ...prev, [key]: value }));
  }

  function resetDraftFromScenario(note: string) {
    setSelectedTemplateId(null);
    const nextDraft = buildDraftFromScenario(dataset, activeScenario);
    setDraft(nextDraft);
    setShowAdvancedRuntime(shouldExpandAdvancedRuntime(nextDraft));
    setDeriveScenarioName(buildDerivedScenarioName(activeScenario, activeScenario));
    setError(null);
    setStatus(note);
  }

  function handleSelectTemplate(template: ExperimentTemplate) {
    setSelectedTemplateId(template.id);
    const nextDraft = buildDraftFromTemplate(template);
    setDraft(nextDraft);
    setShowAdvancedRuntime(shouldExpandAdvancedRuntime(nextDraft));
    setDeriveScenarioName(buildDerivedScenarioName(template.source_scenario_id, activeScenario));
    setStatus(`已载入模板 ${template.name} 到编辑器。`);
    setError(null);
  }

  async function handleCreateTemplate() {
    if (!draft.name.trim()) {
      setError('请先填写模板名称。');
      return;
    }
    setSaving(true);
    setError(null);
    setStatus(null);
    try {
      const saved = await createExperimentTemplate(buildTemplatePayload(draft, dataset, activeScenario));
      await refreshTemplates(saved.id);
      setSelectedTemplateId(saved.id);
      const nextDraft = buildDraftFromTemplate(saved);
      setDraft(nextDraft);
      setShowAdvancedRuntime(shouldExpandAdvancedRuntime(nextDraft));
      setDeriveScenarioName(buildDerivedScenarioName(saved.source_scenario_id, activeScenario));
      setStatus(`已创建实验模板 ${saved.name}。`);
    } catch (nextError) {
      setError(nextError instanceof Error ? nextError.message : '创建模板失败。');
    } finally {
      setSaving(false);
    }
  }

  async function handleUpdateTemplate() {
    if (!selectedTemplateId) {
      setError('请先选择要更新的模板。');
      return;
    }
    setSaving(true);
    setError(null);
    setStatus(null);
    try {
      const saved = await updateExperimentTemplate(selectedTemplateId, buildTemplatePayload(draft, dataset, activeScenario));
      await refreshTemplates(saved.id);
      const nextDraft = buildDraftFromTemplate(saved);
      setDraft(nextDraft);
      setShowAdvancedRuntime(shouldExpandAdvancedRuntime(nextDraft));
      setDeriveScenarioName(buildDerivedScenarioName(saved.source_scenario_id, activeScenario));
      setStatus(`已更新实验模板 ${saved.name}。`);
    } catch (nextError) {
      setError(nextError instanceof Error ? nextError.message : '更新模板失败。');
    } finally {
      setSaving(false);
    }
  }

  async function handleApplyTemplate(template: ExperimentTemplate) {
    setApplyingId(template.id);
    setError(null);
    setStatus(null);
    try {
      await applyExperimentTemplate(template.id, activeScenario);
      await loadScenario(activeScenario);
      setStatus(`已将模板 ${template.name} 应用到当前场景 ${activeScenario}。`);
    } catch (nextError) {
      setError(nextError instanceof Error ? nextError.message : '模板应用失败。');
    } finally {
      setApplyingId(null);
    }
  }

  async function handleDeriveTemplate(template: ExperimentTemplate) {
    const nextScenarioName = deriveScenarioName.trim();
    if (!nextScenarioName) {
      setError('请先填写派生场景名称。');
      return;
    }
    setDerivingId(template.id);
    setError(null);
    setStatus(null);
    try {
      const result = await deriveExperimentTemplate(template.id, nextScenarioName);
      await loadScenario(result.scenario_name);
      setDeriveScenarioName(buildDerivedScenarioName(result.scenario_name, result.scenario_name));
      setStatus(`已基于模板 ${template.name} 派生新场景 ${result.scenario_name}。`);
    } catch (nextError) {
      setError(nextError instanceof Error ? nextError.message : '模板派生失败。');
    } finally {
      setDerivingId(null);
    }
  }

  async function handleDeleteTemplate(template: ExperimentTemplate) {
    if (!confirm(`确认删除实验模板「${template.name}」？`)) return;
    setDeletingId(template.id);
    setError(null);
    setStatus(null);
    try {
      await deleteExperimentTemplate(template.id);
      await refreshTemplates(selectedTemplateId === template.id ? null : selectedTemplateId);
      if (selectedTemplateId === template.id) {
        resetDraftFromScenario('已删除当前模板，并按当前场景重建模板草稿。');
      }
      if (selectedTemplateId !== template.id) {
        setStatus(`已删除实验模板 ${template.name}。`);
      }
    } catch (nextError) {
      setError(nextError instanceof Error ? nextError.message : '删除模板失败。');
    } finally {
      setDeletingId(null);
    }
  }

  const currentScenarioEnvironmentId = typeof dataset.scenario.transmission.params.environment_database_id === 'string'
    ? dataset.scenario.transmission.params.environment_database_id
    : '未绑定';

  return (
    <div className="page-grid page-grid--assets">
      <section className="hero-panel">
        <p className="eyebrow">实验模板</p>
        <h2>实验模板管理</h2>
        <p className="hero-panel__lead">
          实验模板位于场景之上，用来收口环境库、量测引擎和运行参数。场景继续负责节点、链路、拓扑和网络实例化，模板负责组织“这套场景如何被拿去做试验”。
        </p>
      </section>

      <SectionCard title="当前场景基线" eyebrow="当前场景">
        <div className="status-grid">
          <div className="status-grid__item"><span>场景 ID</span><strong>{activeScenario}</strong></div>
          <div className="status-grid__item"><span>节点数</span><strong>{dataset.scenario.nodes.length} 个</strong></div>
          <div className="status-grid__item"><span>拓扑</span><strong>{dataset.scenario.topology.logical_type}</strong></div>
          <div className="status-grid__item"><span>传播模型</span><strong>{dataset.scenario.transmission.type}</strong></div>
          <div className="status-grid__item"><span>环境库</span><strong>{currentScenarioEnvironmentId}</strong></div>
          <div className="status-grid__item"><span>量测引擎</span><strong>{dataset.scenario.measurement.engine_name ?? 'default_measurement_engine'}</strong></div>
        </div>
      </SectionCard>

      <SectionCard
        title="模板编辑器"
        eyebrow="模板编辑"
        actions={
          <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap' }}>
            <button type="button" className="ghost-button" onClick={() => resetDraftFromScenario('已按当前场景重建模板草稿。')}>
              按当前场景重建草稿
            </button>
            <button type="button" className="primary-button" disabled={saving} onClick={() => void handleCreateTemplate()}>
              {saving && !selectedTemplateId ? '保存中...' : '保存为新模板'}
            </button>
            <button type="button" className="ghost-button" disabled={saving || !selectedTemplateId} onClick={() => void handleUpdateTemplate()}>
              {saving && selectedTemplateId ? '更新中...' : '更新当前模板'}
            </button>
          </div>
        }
      >
        <div className="form-grid">
          <label>
            <span>模板名称</span>
            <input value={draft.name} onChange={(event) => patchDraft('name', event.target.value)} />
          </label>
          <label>
            <span>来源场景</span>
            <select value={draft.source_scenario_id} onChange={(event) => patchDraft('source_scenario_id', event.target.value)}>
              {!draft.source_scenario_id ? <option value="">— 请选择来源场景 —</option> : null}
              {[draft.source_scenario_id, ...scenarioIds].filter((value, index, array) => value && array.indexOf(value) === index).map((scenarioId) => (
                <option key={scenarioId} value={scenarioId}>{scenarioId}</option>
              ))}
            </select>
          </label>
          <label className="form-grid__full">
            <span>模板说明</span>
            <textarea rows={2} value={draft.description} onChange={(event) => patchDraft('description', event.target.value)} />
          </label>
          <label className="form-grid__full">
            <span>标签</span>
            <input value={draft.tagsText} onChange={(event) => patchDraft('tagsText', event.target.value)} placeholder="真实海试, Bellhop, 自适应测距" />
          </label>
          <label>
            <span>传播模型</span>
            <select value={draft.transmission_type} onChange={(event) => patchDraft('transmission_type', event.target.value)}>
              <option value="bellhop">bellhop</option>
              <option value="simple">simple</option>
            </select>
          </label>
          <label>
            <span>环境库</span>
            <select value={draft.environment_database_id} onChange={(event) => patchDraft('environment_database_id', event.target.value)}>
              <option value="">— 不绑定 —</option>
              {environmentDatabases.map((database) => (
                <option key={database.id} value={database.id}>{database.name}</option>
              ))}
            </select>
          </label>
          <label>
            <span>量测引擎</span>
            <select value={draft.measurement_engine_name} onChange={(event) => patchDraft('measurement_engine_name', event.target.value)}>
              <option value="default_measurement_engine">default_measurement_engine</option>
              <option value="adaptive_ranging_engine">adaptive_ranging_engine</option>
            </select>
          </label>
          <label>
            <span>仿真时长</span>
            <input type="number" min="1" value={draft.duration} onChange={(event) => patchDraft('duration', event.target.value)} />
          </label>
          <label className="form-grid__full">
            <span>备注</span>
            <textarea rows={3} value={draft.notes} onChange={(event) => patchDraft('notes', event.target.value)} placeholder="记录适用海域、试验目的、算法入口等" />
          </label>
          <details className="form-grid__full template-advanced-panel" open={showAdvancedRuntime} onToggle={(event) => setShowAdvancedRuntime((event.target as HTMLDetailsElement).open)}>
            <summary>高级运行参数</summary>
            <div style={{ fontSize: 12, color: 'var(--text-dim)', lineHeight: 1.6 }}>
              这些字段仍会在模板应用时写回场景，但默认折叠，避免与环境库和试验绑定信息混在一起。
            </div>
            <div className="template-advanced-grid">
              <label>
                <span>随机种子</span>
                <input type="number" min="0" value={draft.seed} onChange={(event) => patchDraft('seed', event.target.value)} />
              </label>
              <label>
                <span>时间步长 ms</span>
                <input type="number" min="1" value={draft.time_step_ms} onChange={(event) => patchDraft('time_step_ms', event.target.value)} />
              </label>
              <label>
                <span>默认看板</span>
                <input value={draft.default_dashboard} onChange={(event) => patchDraft('default_dashboard', event.target.value)} placeholder="environment / monitor / results" />
              </label>
              <label style={{ display: 'flex', alignItems: 'center', gap: 8, fontWeight: 600, alignSelf: 'end', minHeight: 42 }}>
                <input type="checkbox" checked={draft.archive_experiment} onChange={(event) => patchDraft('archive_experiment', event.target.checked)} />
                运行后自动归档实验
              </label>
            </div>
          </details>
        </div>
        {status ? <div style={{ marginTop: 12, color: 'var(--accent)', fontSize: 13 }}>{status}</div> : null}
        {error ? <div style={{ marginTop: 12, color: 'var(--danger)', fontSize: 13 }}>{error}</div> : null}
      </SectionCard>

      <SectionCard title="模板库" eyebrow="模板列表">
        {loading ? <div className="log-list__item">正在加载模板...</div> : null}
        {!loading && templates.length === 0 ? <div className="log-list__item">暂无实验模板，先从当前场景保存一份。</div> : null}
        {!loading && templates.map((template) => {
          const selected = template.id === selectedTemplateId;
          const applying = applyingId === template.id;
          const deleting = deletingId === template.id;
          return (
            <div key={template.id} className="asset-card" style={{ marginBottom: 10, borderColor: selected ? 'rgba(34,211,238,0.35)' : undefined }}>
              <strong>{template.name}</strong>
              <span style={{ color: 'var(--text-dim)' }}>
                {template.source_scenario_id || '未绑定场景'} · {template.summary.node_count ?? 0} 节点 · {template.summary.topology_type || '未标记拓扑'}
              </span>
              <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                传播={template.bindings.transmission_type || '未标记'} / 环境库={template.bindings.environment_database_id || '未绑定'} / 量测={template.bindings.measurement_engine_name || '未标记'}
              </span>
              <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                更新于 {formatTimestamp(template.updated_at)}
              </span>
              {template.tags.length > 0 ? (
                <div className="chip-list" style={{ marginTop: 6 }}>
                  {template.tags.map((tag) => <span key={tag} className="chip">{tag}</span>)}
                </div>
              ) : null}
              <div style={{ display: 'flex', gap: 8, marginTop: 8, flexWrap: 'wrap' }}>
                <button type="button" className="ghost-button" onClick={() => handleSelectTemplate(template)}>载入到编辑器</button>
                <button type="button" className="primary-button" disabled={applying} onClick={() => void handleApplyTemplate(template)}>
                  {applying ? '应用中...' : '应用到当前场景'}
                </button>
                <button type="button" className="ghost-button" style={{ color: 'var(--danger)', borderColor: 'rgba(251,113,133,0.3)' }} disabled={deleting} onClick={() => void handleDeleteTemplate(template)}>
                  {deleting ? '删除中...' : '删除'}
                </button>
              </div>
            </div>
          );
        })}
      </SectionCard>

      {selectedTemplate ? (
        <SectionCard title="已选模板摘要" eyebrow="模板摘要">
          <div className="status-grid">
            <div className="status-grid__item"><span>模板 ID</span><strong>{selectedTemplate.id}</strong></div>
            <div className="status-grid__item"><span>来源场景</span><strong>{selectedTemplate.source_scenario_id || '未绑定'}</strong></div>
            <div className="status-grid__item"><span>环境库</span><strong>{selectedTemplate.bindings.environment_database_id || '未绑定'}</strong></div>
            <div className="status-grid__item"><span>量测引擎</span><strong>{selectedTemplate.bindings.measurement_engine_name || '未标记'}</strong></div>
            <div className="status-grid__item"><span>运行时长</span><strong>{selectedTemplate.runtime.duration ?? '未标记'}</strong></div>
            <div className="status-grid__item"><span>默认看板</span><strong>{selectedTemplate.runtime.default_dashboard || '未标记'}</strong></div>
          </div>
          <div className="form-grid" style={{ marginTop: 16 }}>
            <label>
              <span>派生场景名称</span>
              <input
                value={deriveScenarioName}
                onChange={(event) => setDeriveScenarioName(event.target.value)}
                placeholder={buildDerivedScenarioName(selectedTemplate.source_scenario_id, activeScenario)}
              />
            </label>
            <div style={{ display: 'flex', alignItems: 'end' }}>
              <button type="button" className="primary-button" disabled={derivingId === selectedTemplate.id} onClick={() => void handleDeriveTemplate(selectedTemplate)}>
                {derivingId === selectedTemplate.id ? '派生中...' : '派生新场景'}
              </button>
            </div>
            <div className="form-grid__full" style={{ fontSize: 12, color: 'var(--text-dim)' }}>
              派生会先复制来源场景，再将模板中的环境库、量测引擎和运行参数写入新场景，并自动切换过去。
            </div>
          </div>
        </SectionCard>
      ) : null}
    </div>
  );
}
