import { useCallback, useEffect, useRef, useState } from 'react';

import { BellhopParamsPanel } from '../components/PropertyInspector';
import { fetchDataFiles, fetchEnvironmentDatabases, deleteEnvironmentDatabase, updateEnvironmentDatabase } from '../services/api';
import type { DemoScenario, EnvironmentDatabase } from '../types';

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

function formatUsageSummary(database: EnvironmentDatabase): string {
  if (!database.usage?.in_use) return '未被场景或模板引用';
  const userScenarioCount = database.usage.user_scenario_ids?.length ?? database.usage.scenario_ids.length;
  const builtInScenarioCount = database.usage.built_in_scenario_ids?.length ?? 0;
  const templateCount = database.usage.template_ids.length;
  if (!userScenarioCount && !templateCount && builtInScenarioCount) {
    return `仅被 ${builtInScenarioCount} 个内置示例场景引用`;
  }
  return `被 ${userScenarioCount + builtInScenarioCount} 个场景、${templateCount} 个模板引用`;
}

function isAnalyticalEnvironmentDatabase(database: EnvironmentDatabase): boolean {
  return database.metadata?.build_mode === 'analytical';
}

interface EnvironmentPageProps {
  activeScenario: string;
}

/* ── Inline styles ── */

const cardStyle: React.CSSProperties = {
  background: 'rgba(2,10,19,0.72)',
  border: '1px solid rgba(148,163,184,0.14)',
  borderRadius: 18,
  padding: '16px 18px',
  display: 'grid',
  gap: 10,
  transition: 'border-color 0.15s',
  cursor: 'pointer',
};

const cardSelectedStyle: React.CSSProperties = {
  ...cardStyle,
  borderColor: 'rgba(34,211,238,0.4)',
  background: 'rgba(34,211,238,0.04)',
};

const smallBtnStyle: React.CSSProperties = {
  border: '1px solid rgba(148,163,184,0.25)',
  borderRadius: 10,
  background: 'rgba(2,10,19,0.6)',
  color: '#dbe7f2',
  fontSize: 12,
  cursor: 'pointer',
  padding: '6px 12px',
  whiteSpace: 'nowrap',
};

const dangerBtnStyle: React.CSSProperties = {
  ...smallBtnStyle,
  borderColor: 'rgba(239,68,68,0.3)',
  color: '#f87171',
};

const inlineInputStyle: React.CSSProperties = {
  width: '100%',
  border: '1px solid rgba(34,211,238,0.3)',
  borderRadius: 10,
  background: 'rgba(2,10,19,0.9)',
  color: '#fff',
  padding: '8px 10px',
  fontSize: 13,
};

/* ── EnvironmentDatabaseLibrary component ── */

function EnvironmentDatabaseLibrary({
  databases,
  selectedId,
  onSelect,
  onRefresh,
}: {
  databases: EnvironmentDatabase[];
  selectedId: string;
  onSelect: (id: string) => void;
  onRefresh: () => void;
}) {
  const [editingId, setEditingId] = useState<string | null>(null);
  const [editName, setEditName] = useState('');
  const [editDesc, setEditDesc] = useState('');
  const [confirmDeleteId, setConfirmDeleteId] = useState<string | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const nameInputRef = useRef<HTMLInputElement>(null);

  useEffect(() => {
    if (editingId && nameInputRef.current) nameInputRef.current.focus();
  }, [editingId]);

  function startEdit(db: EnvironmentDatabase) {
    setEditingId(db.id);
    setEditName(db.name);
    setEditDesc(db.description || '');
    setConfirmDeleteId(null);
    if ((db.usage?.blocking_reference_count ?? 0) > 0) {
      setError('当前环境库已被用户场景或实验模板引用：允许修改说明，但改名会被后端阻止。');
      return;
    }
    if (db.usage?.built_in_only) {
      setError('当前环境库仅被内置示例场景引用：改名会自动同步示例引用，不再被硬阻断。');
      return;
    }
    setError(null);
  }

  function cancelEdit() {
    setEditingId(null);
    setError(null);
  }

  async function saveEdit() {
    if (!editingId || busy) return;
    setBusy(true);
    setError(null);
    try {
      const updates: { new_name?: string; description?: string } = {};
      const current = databases.find((d) => d.id === editingId);
      if (!current) return;
      const trimmedName = editName.trim();
      if (trimmedName && trimmedName !== current.name) updates.new_name = trimmedName;
      if (editDesc !== (current.description || '')) updates.description = editDesc;
      if (Object.keys(updates).length === 0) { cancelEdit(); return; }
      const updated = await updateEnvironmentDatabase(editingId, updates);
      // If renamed and was selected, update selection
      if (updates.new_name && selectedId === editingId) {
        onSelect(updated.id);
      }
      onRefresh();
      setEditingId(null);
    } catch (err) {
      setError(String(err instanceof Error ? err.message : err));
    } finally {
      setBusy(false);
    }
  }

  async function handleDelete(id: string) {
    if (busy) return;
    setBusy(true);
    setError(null);
    try {
      await deleteEnvironmentDatabase(id);
      if (selectedId === id) onSelect('');
      onRefresh();
      setConfirmDeleteId(null);
    } catch (err) {
      setError(String(err instanceof Error ? err.message : err));
    } finally {
      setBusy(false);
    }
  }

  if (databases.length === 0) {
    return (
      <div style={{ padding: '24px 18px', textAlign: 'center', color: '#c3d1df', fontSize: 14, lineHeight: 1.7 }}>
        暂无环境数据库。请在下方构建器中创建你的第一个环境库。
      </div>
    );
  }

  return (
    <div className="environment-library-grid">
      {error && (
        <div style={{ fontSize: 13, color: '#fecaca', padding: '10px 12px', background: 'rgba(239,68,68,0.08)', border: '1px solid rgba(239,68,68,0.2)', borderRadius: 12, gridColumn: '1 / -1', lineHeight: 1.6 }}>
          {error}
        </div>
      )}
      {databases.map((db) => {
        const isSelected = db.id === selectedId;
        const isEditing = editingId === db.id;
        const isConfirmingDelete = confirmDeleteId === db.id;

        return (
          <div
            key={db.id}
            style={isSelected ? cardSelectedStyle : cardStyle}
            onClick={() => { if (!isEditing && !isConfirmingDelete) onSelect(isSelected ? '' : db.id); }}
          >
            {/* Header: name + actions */}
            <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
              {isEditing ? (
                <input
                  ref={nameInputRef}
                  value={editName}
                  onChange={(e) => setEditName(e.target.value)}
                  onKeyDown={(e) => { if (e.key === 'Enter') saveEdit(); if (e.key === 'Escape') cancelEdit(); }}
                  onClick={(e) => e.stopPropagation()}
                  style={{ ...inlineInputStyle, flex: 1, fontWeight: 600 }}
                  placeholder="数据库名称"
                />
              ) : (
                <span style={{ flex: 1, fontSize: 16, fontWeight: 700, color: isSelected ? '#f8fbff' : '#d8e3ec', lineHeight: 1.35 }}>
                  {db.name}
                </span>
              )}
              <div style={{ display: 'flex', gap: 4 }} onClick={(e) => e.stopPropagation()}>
                {isEditing ? (
                  <>
                    <button type="button" style={{ ...smallBtnStyle, borderColor: 'rgba(34,211,238,0.3)', color: 'var(--accent)' }} onClick={saveEdit} disabled={busy}>
                      {busy ? '...' : '保存'}
                    </button>
                    <button type="button" style={smallBtnStyle} onClick={cancelEdit} disabled={busy}>取消</button>
                  </>
                ) : (
                  <>
                    <button type="button" style={smallBtnStyle} onClick={() => startEdit(db)} title={(db.usage?.blocking_reference_count ?? 0) > 0 ? '编辑说明；若被用户场景或模板引用，改名会被阻止' : (db.usage?.built_in_only ? '允许改名；会自动同步内置示例场景引用' : '重命名 / 编辑描述')}>✎</button>
                    {isConfirmingDelete ? (
                      <>
                        <button type="button" style={dangerBtnStyle} onClick={() => handleDelete(db.id)} disabled={busy}>
                          {busy ? '...' : '确认删除'}
                        </button>
                        <button type="button" style={smallBtnStyle} onClick={() => setConfirmDeleteId(null)}>取消</button>
                      </>
                    ) : (
                      <button type="button" style={dangerBtnStyle} onClick={() => { setConfirmDeleteId(db.id); setEditingId(null); }} title="删除">✕</button>
                    )}
                  </>
                )}
              </div>
            </div>

            <div style={{ display: 'flex', flexWrap: 'wrap', gap: 8 }}>
              <span style={{ fontSize: 12, color: isSelected ? '#67e8f9' : '#d7e3ef', padding: '4px 10px', borderRadius: 999, background: 'rgba(34,211,238,0.08)', border: '1px solid rgba(34,211,238,0.14)', lineHeight: 1.45 }}>
                {formatUsageSummary(db)}
              </span>
              {db.metadata?.warnings?.length ? (
                <span style={{ fontSize: 12, color: '#fde68a', padding: '4px 10px', borderRadius: 999, background: 'rgba(251,191,36,0.08)', border: '1px solid rgba(251,191,36,0.16)', lineHeight: 1.45 }}>
                  {db.metadata.warnings.length} 条边界提示
                </span>
              ) : null}
              {isSelected ? (
                <span style={{ fontSize: 12, color: '#bbf7d0', padding: '4px 10px', borderRadius: 999, background: 'rgba(34,197,94,0.08)', border: '1px solid rgba(34,197,94,0.16)', lineHeight: 1.45 }}>
                  已载入下方编辑器
                </span>
              ) : null}
            </div>

            {/* Description (editable in edit mode) */}
            {isEditing ? (
              <input
                value={editDesc}
                onChange={(e) => setEditDesc(e.target.value)}
                onKeyDown={(e) => { if (e.key === 'Enter') saveEdit(); if (e.key === 'Escape') cancelEdit(); }}
                onClick={(e) => e.stopPropagation()}
                style={{ ...inlineInputStyle, fontSize: 11 }}
                placeholder="描述（可选）"
              />
            ) : db.description ? (
              <div style={{ fontSize: 13, color: '#c9d6e3', lineHeight: 1.65 }}>{db.description}</div>
            ) : null}

            {/* Build params summary */}
            {db.build && (
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 12px', fontSize: 12, color: '#c0cedd', lineHeight: 1.55 }}>
                {db.build.frequency_hz != null && <span>频率: {(db.build.frequency_hz / 1000).toFixed(1)}kHz</span>}
                {db.build.range_max_m != null && <span>范围: {db.build.range_max_m}m</span>}
                {db.build.depth_max_m != null && <span>深度: {db.build.depth_max_m}m</span>}
                {db.build.water_depth_m != null && <span>水深: {db.build.water_depth_m}m</span>}
                {isAnalyticalEnvironmentDatabase(db) && db.build.sound_speed_mps != null && <span>声速: {db.build.sound_speed_mps}m/s</span>}
                {db.metadata?.build_mode && <span>模式: {formatBuildModeLabel(db.metadata.build_mode)}</span>}
                {db.metadata?.data_source_type && <span>来源: {formatSourceTypeLabel(db.metadata.data_source_type)}</span>}
              </div>
            )}

            {db.metadata && (
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '4px 12px', fontSize: 12, color: '#a5f3fc', lineHeight: 1.55 }}>
                <span>状态: {formatValidationLabel(db.metadata.validation_status)}</span>
                <span>引擎: {db.metadata.engine_name || '未标记'}</span>
                {db.metadata.at_compatibility && <span>AT兼容: {db.metadata.at_compatibility}</span>}
                {db.metadata.woss_source_id && <span>WOSS源: {db.metadata.woss_source_id}</span>}
                {db.metadata.woss_profile_id && <span>WOSS剖面: {db.metadata.woss_profile_id}</span>}
              </div>
            )}

            {isEditing && (db.usage?.blocking_reference_count ?? 0) > 0 ? (
              <div style={{ fontSize: 12, color: '#fde68a', lineHeight: 1.65 }}>
                当前环境库正在被引用：可以更新说明文本，但若尝试改名，后端会返回阻断错误。
              </div>
            ) : null}

            {isEditing && db.usage?.built_in_only ? (
              <div style={{ fontSize: 12, color: '#bfdbfe', lineHeight: 1.65 }}>
                当前仅被内置示例场景引用：若改名，系统会自动把这些示例场景同步到新环境库 ID。
              </div>
            ) : null}

            {isConfirmingDelete && (db.usage?.blocking_reference_count ?? 0) > 0 ? (
              <div style={{ fontSize: 12, color: '#fecaca', lineHeight: 1.65 }}>
                当前环境库仍被引用，删除操作会被后端拒绝。建议先解除场景或模板引用。
              </div>
            ) : null}

            {isConfirmingDelete && db.usage?.built_in_only ? (
              <div style={{ fontSize: 12, color: '#bfdbfe', lineHeight: 1.65 }}>
                当前仅被内置示例场景引用：删除时会自动清理这些内置示例中的环境库绑定，不再阻断删除。
              </div>
            ) : null}

            {db.usage?.in_use ? (
              <div style={{ display: 'grid', gap: 6, padding: '10px 12px', borderRadius: 12, background: 'rgba(15,23,42,0.45)', border: '1px solid rgba(148,163,184,0.12)' }}>
                {(db.usage.user_scenario_ids?.length ?? 0) > 0 ? (
                  <div style={{ fontSize: 12, color: '#dbe7f2', lineHeight: 1.65 }}>用户场景引用: {db.usage.user_scenario_ids?.join('、')}</div>
                ) : null}
                {(db.usage.built_in_scenario_ids?.length ?? 0) > 0 ? (
                  <div style={{ fontSize: 12, color: '#bfdbfe', lineHeight: 1.65 }}>内置示例引用: {db.usage.built_in_scenario_ids?.join('、')}</div>
                ) : null}
                {db.usage.template_ids.length > 0 ? (
                  <div style={{ fontSize: 12, color: '#dbe7f2', lineHeight: 1.65 }}>模板引用: {db.usage.template_ids.join('、')}</div>
                ) : null}
              </div>
            ) : null}

            {db.metadata?.warnings?.length ? (
              <div style={{ display: 'grid', gap: 6, padding: '10px 12px', borderRadius: 12, background: 'rgba(251,191,36,0.08)', border: '1px solid rgba(251,191,36,0.16)' }}>
                {db.metadata.warnings.map((warning) => (
                  <div key={warning} style={{ fontSize: 12, color: '#fde68a', lineHeight: 1.65 }}>{warning}</div>
                ))}
              </div>
            ) : null}

            {/* Timestamp */}
            <div style={{ fontSize: 11, color: '#94a3b8', lineHeight: 1.5 }}>
              更新于 {new Date(db.updated_at * 1000).toLocaleString('zh-CN')}
            </div>
          </div>
        );
      })}
    </div>
  );
}

function buildEnvironmentDraftScenario(seedScenarioId: string): DemoScenario {
  return {
    simulation: {
      duration: 300,
      scheduler: 'default',
      seed: 1,
      time_step_ms: 1000,
    },
    scenario_metadata: {
      scenario_id: seedScenarioId,
      name: `${seedScenarioId} environment builder`,
      version: '2.0',
      project_tags: ['environment', 'offline-build'],
      description: '独立环境构建草稿，用于生成可复用的环境数据库。',
    },
    nodes: [],
    topology: {
      deployment_type: 'star',
      logical_type: 'star',
    },
    environment: {
      sound_speed_profile: 'generated',
      boundary: {
        surface: 'flat',
        bottom: 'flat',
      },
    },
    noise: {
      composition: [{ type: 'ambient_constant', value_db: 50 }],
    },
    transmission: {
      type: 'bellhop',
      params: {
        water_depth_m: 100,
        sound_speed_mps: 1500,
        absorption_db_per_km: 0.5,
        spreading_factor: 20,
        max_bounces: 3,
        source_level_db: 190,
      },
    },
    measurement: {
      outputs: ['delay', 'received_level', 'pseudo_range'],
      noise_std: 0,
      dr_noise_std: 0,
    },
    output: {
      trace: 'results/environment_builder_trace.csv',
      stats_file: 'results/environment_builder_stats.csv',
      archive_experiment: false,
    },
    ui: {
      theme: 'builder',
      enable_3d_view: false,
      default_dashboard: 'environment',
    },
  };
}

export function EnvironmentPage({ activeScenario }: EnvironmentPageProps) {
  const [workingScenario, setWorkingScenario] = useState<DemoScenario>(() => buildEnvironmentDraftScenario(activeScenario));
  const [gridFiles, setGridFiles] = useState<string[]>([]);
  const [bathymetryFiles, setBathymetryFiles] = useState<string[]>([]);
  const [environmentDatabases, setEnvironmentDatabases] = useState<EnvironmentDatabase[]>([]);

  const refreshDataFiles = useCallback(() => {
    fetchDataFiles('grid').then(setGridFiles).catch(() => {});
    fetchDataFiles('bathymetry').then(setBathymetryFiles).catch(() => {});
    fetchEnvironmentDatabases().then(setEnvironmentDatabases).catch(() => {});
  }, []);

  useEffect(() => {
    refreshDataFiles();
  }, [refreshDataFiles]);

  useEffect(() => {
    setWorkingScenario((prev) => ({
      ...prev,
      scenario_metadata: {
        ...prev.scenario_metadata,
        scenario_id: activeScenario,
        name: `${activeScenario} environment builder`,
      },
    }));
  }, [activeScenario]);

  const handleUpdateTransmissionParam = useCallback((field: string, value: string) => {
    setWorkingScenario((prev) => {
      if (String(prev.transmission.params[field] ?? '') === value) {
        return prev;
      }

      return {
        ...prev,
        transmission: {
          ...prev.transmission,
          params: {
            ...prev.transmission.params,
            [field]: value,
          },
        },
      };
    });
  }, []);

  const handleSelectEnvironmentDatabase = useCallback((databaseId: string) => {
    setWorkingScenario((prev) => {
      const nextId = String(databaseId ?? '');
      const currentId = String(prev.transmission.params.environment_database_id ?? '');
      if (currentId === nextId) {
        return prev;
      }

      const nextParams = { ...prev.transmission.params };
      if (!nextId) {
        delete nextParams.environment_database_id;
      } else {
        nextParams.environment_database_id = nextId;
      }
      return {
        ...prev,
        transmission: {
          ...prev.transmission,
          params: nextParams,
        },
      };
    });
  }, []);

  const selectedDbId = String(workingScenario.transmission.params.environment_database_id ?? '');

  return (
    <div className="page-grid page-grid--environment environment-page">
      <section className="hero-panel environment-page__hero">
        <p className="eyebrow">Bellhop 环境管理</p>
        <h2>Bellhop 环境管理</h2>
        <p className="hero-panel__lead">
          这里统一完成环境库的构建、选择和管理：准备 SSP、地形与传播网格，生成后存入环境库，再由多个网络场景复用。列表会直接显示边界 warning 和引用关系，避免误删或误改。
        </p>
      </section>

      {/* ── 环境库管理 ── */}
      <section className="section-card environment-page__library" style={{ padding: '22px 24px' }}>
        <div style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', marginBottom: 10 }}>
          <div>
            <p className="eyebrow" style={{ margin: 0 }}>环境库列表</p>
            <h3 style={{ margin: '6px 0 0', fontSize: 22, fontWeight: 700, color: '#f8fbff', lineHeight: 1.3 }}>
              环境库管理
              <span style={{ fontSize: 14, fontWeight: 500, color: '#c3d1df', marginLeft: 10 }}>
                {environmentDatabases.length} 个数据库
              </span>
            </h3>
          </div>
          <button
            type="button"
            onClick={refreshDataFiles}
            style={{ ...smallBtnStyle, padding: '5px 12px', fontSize: 11 }}
          >
            刷新
          </button>
        </div>
        {selectedDbId ? (
          <div style={{ marginBottom: 14, padding: '12px 14px', borderRadius: 14, background: 'rgba(34,211,238,0.08)', border: '1px solid rgba(34,211,238,0.16)', fontSize: 13, color: '#e0eef8', lineHeight: 1.75 }}>
            当前选中的环境库会同步载入下方构建器，便于查看参数来源、调整说明，并在修改名称后生成新的环境库。
          </div>
        ) : (
          <div style={{ marginBottom: 14, padding: '12px 14px', borderRadius: 14, background: 'rgba(148,163,184,0.08)', border: '1px solid rgba(148,163,184,0.14)', fontSize: 13, color: '#d2dce6', lineHeight: 1.75 }}>
            点击任一环境库可直接载入下方构建器；被场景或模板引用的环境库，后端会阻止删除或重命名。
          </div>
        )}
        <EnvironmentDatabaseLibrary
          databases={environmentDatabases}
          selectedId={selectedDbId}
          onSelect={handleSelectEnvironmentDatabase}
          onRefresh={refreshDataFiles}
        />
      </section>

      <div className="environment-page__builder">
        <BellhopParamsPanel
          workingScenario={workingScenario}
          gridFiles={gridFiles}
          bathymetryFiles={bathymetryFiles}
          environmentDatabases={environmentDatabases}
          onUpdateTransmissionParam={handleUpdateTransmissionParam}
          onRefreshDataFiles={refreshDataFiles}
          onSelectEnvironmentDatabase={handleSelectEnvironmentDatabase}
          mode="builder"
        />
      </div>
    </div>
  );
}
