import { useEffect, useState } from 'react';

import { SectionCard } from '../components/SectionCard';
import {
  deleteScenario,
  fetchExperimentArchiveDetail,
  fetchExperimentArchives,
  fetchExperimentReport,
  fetchHistory,
  fetchResults,
  fetchScenarios,
} from '../services/api';
import type { ExperimentArchiveDetail, ExperimentArchiveSummary, ExperimentReport, HistoryRecord, ResultsResponse } from '../services/api';
import type { DemoDataset } from '../types';

interface AssetsPageProps {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
  runVersion?: number;
}

export function AssetsPage({ activeScenario, loadScenario, runVersion = 0 }: AssetsPageProps) {
  const [scenarios, setScenarios] = useState<string[]>([]);
  const [results, setResults] = useState<ResultsResponse | null>(null);
  const [history, setHistory] = useState<HistoryRecord[]>([]);
  const [archives, setArchives] = useState<ExperimentArchiveSummary[]>([]);
  const [selectedArchiveId, setSelectedArchiveId] = useState<string | null>(null);
  const [selectedArchive, setSelectedArchive] = useState<ExperimentArchiveDetail | null>(null);
  const [comparedArchiveIds, setComparedArchiveIds] = useState<string[]>([]);
  const [report, setReport] = useState<ExperimentReport | null>(null);
  const [loading, setLoading] = useState(true);
  const [archiveLoading, setArchiveLoading] = useState(false);
  const [reportLoading, setReportLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const [reportError, setReportError] = useState<string | null>(null);
  const [loadingScenario, setLoadingScenario] = useState<string | null>(null);
  const [deletingScenario, setDeletingScenario] = useState<string | null>(null);
  const [deleteError, setDeleteError] = useState<string | null>(null);

  function refreshScenarios() {
    setLoading(true);
    setError(null);
    Promise.all([fetchScenarios(), fetchResults(), fetchHistory(), fetchExperimentArchives()])
      .then(([scns, res, hist, archiveList]) => {
        setScenarios(scns);
        setResults(res);
        setHistory(hist);
        setArchives(archiveList);
        setSelectedArchiveId((current) => {
          if (archiveList.length === 0) return null;
          if (current && archiveList.some((item) => item.id === current)) return current;
          return archiveList[0].id;
        });
        setComparedArchiveIds((current) => current.filter((id) => archiveList.some((item) => item.id === id)));
      })
      .catch((e: Error) => setError(e.message))
      .finally(() => setLoading(false));
  }

  useEffect(() => {
    refreshScenarios();
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [runVersion]);

  useEffect(() => {
    if (!selectedArchiveId) {
      setSelectedArchive(null);
      return;
    }
    setArchiveLoading(true);
    fetchExperimentArchiveDetail(selectedArchiveId)
      .then((detail) => setSelectedArchive(detail))
      .catch((e: Error) => setError(e.message))
      .finally(() => setArchiveLoading(false));
  }, [selectedArchiveId]);

  useEffect(() => {
    if (comparedArchiveIds.length === 0) {
      setReport(null);
      setReportError(null);
      setReportLoading(false);
      return;
    }
    setReportLoading(true);
    setReportError(null);
    fetchExperimentReport(comparedArchiveIds)
      .then((nextReport) => setReport(nextReport))
      .catch((e: Error) => {
        setReport(null);
        setReportError(e.message);
      })
      .finally(() => setReportLoading(false));
  }, [comparedArchiveIds]);

  async function handleLoad(name: string) {
    setLoadingScenario(name);
    try {
      await loadScenario(name);
    } finally {
      setLoadingScenario(null);
    }
  }

  async function handleDelete(name: string) {
    if (!confirm(`确认删除场景「${name}」？此操作不可撤销。`)) return;
    setDeletingScenario(name);
    setDeleteError(null);
    try {
      await deleteScenario(name);
      refreshScenarios();
    } catch (e) {
      setDeleteError((e as Error).message);
    } finally {
      setDeletingScenario(null);
    }
  }

  function handleToggleArchiveCompare(archiveId: string, checked: boolean) {
    setComparedArchiveIds((current) => {
      if (checked) {
        return current.includes(archiveId) ? current : [...current, archiveId];
      }
      return current.filter((id) => id !== archiveId);
    });
  }

  function downloadTextFile(filename: string, content: string, mimeType: string) {
    const blob = new Blob([content], { type: mimeType });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.click();
    URL.revokeObjectURL(url);
  }

  function handleExportReportMarkdown() {
    if (!report) return;
    const stamp = new Date(report.generated_at * 1000).toISOString().replace(/[:.]/g, '-');
    downloadTextFile(`experiment-report-${stamp}.md`, report.markdown, 'text/markdown;charset=utf-8');
  }

  function handleExportReportJson() {
    if (!report) return;
    const stamp = new Date(report.generated_at * 1000).toISOString().replace(/[:.]/g, '-');
    downloadTextFile(`experiment-report-${stamp}.json`, JSON.stringify(report, null, 2), 'application/json;charset=utf-8');
  }

  function formatNullableNumber(value: number | null | undefined, digits = 3) {
    return typeof value === 'number' && Number.isFinite(value) ? value.toFixed(digits) : '—';
  }

  const delayWinner = report?.rankings['lowest_avg_delay_s']?.[0] ?? null;
  const powerWinner = report?.rankings['highest_avg_received_level_db']?.[0] ?? null;
  const snrWinner = report?.rankings['highest_avg_snr_db']?.[0] ?? null;

  return (
    <div className="page-grid page-grid--assets">
      <section className="hero-panel" style={{ gridColumn: 'span 12' }}>
        <p className="eyebrow">场景管理与归档</p>
        <h2>场景管理与归档</h2>
        <p className="hero-panel__lead">
          这里集中查看场景库、最新结果、试验归档和运行历史，避免在多个页面之间来回查找场景与归档数据。
        </p>
      </section>

      <SectionCard title="场景库" eyebrow="场景资产">
        {loading && <div className="log-list__item">正在加载...</div>}
        {error && <div style={{ color: 'var(--danger)', fontSize: 13 }}>{error}</div>}
        {deleteError && <div style={{ color: 'var(--danger)', fontSize: 13, marginBottom: 8 }}>删除失败：{deleteError}</div>}
        {!loading && scenarios.map((name) => {
          const isActive = name === activeScenario;
          const isLoadingThis = loadingScenario === name;
          const isDeletingThis = deletingScenario === name;
          const isBuiltIn = name.endsWith('_demo') || name.endsWith('_demo_simple');
          return (
            <div key={name} className="asset-card" style={{ marginBottom: 10 }}>
              <strong>{name}</strong>
              <span style={{ color: isActive ? 'var(--accent)' : 'var(--text-dim)' }}>
                {isActive ? '★ 当前活跃场景' : '可载入场景'}
                {isBuiltIn && ' · 内置'}
              </span>
              <div style={{ display: 'flex', gap: 8, marginTop: 6, flexWrap: 'wrap' }}>
                {!isActive && (
                  <button
                    className="btn btn--sm"
                    disabled={loadingScenario !== null}
                    onClick={() => void handleLoad(name)}
                  >
                    {isLoadingThis ? '载入中...' : '载入此场景'}
                  </button>
                )}
                {!isBuiltIn && !isActive && (
                  <button
                    className="ghost-button"
                    style={{ padding: '4px 10px', fontSize: 12, borderRadius: 8, color: 'var(--danger)', borderColor: 'rgba(251,113,133,0.3)' }}
                    disabled={deletingScenario !== null}
                    onClick={() => void handleDelete(name)}
                  >
                    {isDeletingThis ? '删除中...' : '删除'}
                  </button>
                )}
              </div>
            </div>
          );
        })}
      </SectionCard>

      <SectionCard title="最新结果" eyebrow="结果快照">
        {loading && <div className="log-list__item">正在加载...</div>}
        {!loading && results && results.file ? (
          <div className="asset-card">
            <strong>{results.file}</strong>
            <span>共 {results.rows.length} 条链路记录</span>
            {results.rows.map((r, i) => (
              <span key={i} style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                链路 {r.tx_id} → {r.rx_id}：delay {Number(r.delay_s).toFixed(4)} s，
                power {Number(r.received_level_db).toFixed(2)} dB
              </span>
            ))}
          </div>
        ) : (
          !loading && <div className="log-list__item">暂无结果文件。请先运行仿真。</div>
        )}
      </SectionCard>

      <SectionCard
        title="试验归档"
        eyebrow="归档与对比"
        actions={
          <div style={{ display: 'flex', gap: 8, flexWrap: 'wrap', alignItems: 'center' }}>
            <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>已选 {comparedArchiveIds.length} 个归档</span>
            <button type="button" className="ghost-button" onClick={() => setComparedArchiveIds([])} disabled={comparedArchiveIds.length === 0}>清除已选</button>
            <button type="button" className="ghost-button" onClick={handleExportReportMarkdown} disabled={!report}>导出 Markdown</button>
            <button type="button" className="ghost-button" onClick={handleExportReportJson} disabled={!report}>导出 JSON</button>
          </div>
        }
      >
        {loading && <div className="log-list__item">正在加载...</div>}
        {!loading && archives.length === 0 ? (
          <div className="log-list__item">暂无实验归档。请先为当前场景开启“结果归档”并运行仿真。</div>
        ) : null}
        {!loading && archives.length > 0 ? (
          <>
            <div style={{ display: 'grid', gap: 12, gridTemplateColumns: 'minmax(260px, 320px) minmax(0, 1fr)', alignItems: 'start' }}>
              <div style={{ display: 'grid', gap: 10 }}>
                {archives.map((archive) => {
                  const selected = archive.id === selectedArchiveId;
                  const compared = comparedArchiveIds.includes(archive.id);
                  return (
                    <div
                      key={archive.id}
                      className="asset-card"
                      onClick={() => setSelectedArchiveId(archive.id)}
                      style={{
                        textAlign: 'left',
                        cursor: 'pointer',
                        borderColor: selected ? 'rgba(56, 189, 248, 0.45)' : 'rgba(148,163,184,0.16)',
                        boxShadow: selected ? '0 0 0 1px rgba(56, 189, 248, 0.2)' : 'none',
                      }}
                    >
                      <div style={{ display: 'flex', justifyContent: 'space-between', gap: 8, alignItems: 'center' }}>
                        <strong>{archive.scenario}</strong>
                        <label
                          style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 12, color: 'var(--text-dim)' }}
                          onClick={(event) => event.stopPropagation()}
                        >
                          <input
                            type="checkbox"
                            checked={compared}
                            onChange={(event) => handleToggleArchiveCompare(archive.id, event.target.checked)}
                          />
                          对比
                        </label>
                      </div>
                      <span style={{ color: 'var(--text-dim)' }}>{new Date(archive.created_at * 1000).toLocaleString('zh-CN')}</span>
                      <span style={{ color: archive.status === 'success' ? 'var(--success)' : 'var(--danger)' }}>
                        {archive.status === 'success' ? '归档成功' : `归档失败记录 · exit=${archive.exit_code}`}
                      </span>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                        链路 {archive.summary.rows} 条 · 事件 {archive.summary.event_rows} 条 · 射线 {archive.summary.ray_rows} 条
                      </span>
                      <span style={{ fontSize: 11, color: 'var(--text-dim)' }}>ID: {archive.id}</span>
                    </div>
                  );
                })}
              </div>

              <div className="asset-card" style={{ minHeight: 240 }}>
                {archiveLoading ? <div className="log-list__item">正在加载归档详情...</div> : null}
                {!archiveLoading && selectedArchive ? (
                  <>
                    <strong>{selectedArchive.scenario_metadata.name}</strong>
                    <span style={{ color: 'var(--text-dim)' }}>
                      {selectedArchive.scenario} · {new Date(selectedArchive.created_at * 1000).toLocaleString('zh-CN')}
                    </span>
                    <span style={{ color: selectedArchive.status === 'success' ? 'var(--success)' : 'var(--danger)' }}>
                      {selectedArchive.status === 'success' ? '运行成功' : `运行失败 · exit=${selectedArchive.exit_code}`}
                    </span>
                    {selectedArchive.scenario_metadata.description ? (
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>{selectedArchive.scenario_metadata.description}</span>
                    ) : null}

                    <div className="settings-grid" style={{ marginTop: 8 }}>
                      <div className="settings-row"><span>传输模型</span><strong>{selectedArchive.transmission_type || '—'}</strong></div>
                      <div className="settings-row"><span>输出模式</span><strong>{selectedArchive.trace_mode || '—'}</strong></div>
                      <div className="settings-row"><span>环境库</span><strong>{selectedArchive.environment_database_id || '—'}</strong></div>
                      <div className="settings-row"><span>发射节点数</span><strong>{selectedArchive.summary.transmitter_count}</strong></div>
                      <div className="settings-row"><span>接收节点数</span><strong>{selectedArchive.summary.receiver_count}</strong></div>
                      <div className="settings-row"><span>平均时延</span><strong>{selectedArchive.summary.avg_delay_s !== null ? `${selectedArchive.summary.avg_delay_s.toFixed(4)} s` : '—'}</strong></div>
                      <div className="settings-row"><span>最大时延</span><strong>{selectedArchive.summary.max_delay_s !== null ? `${selectedArchive.summary.max_delay_s.toFixed(4)} s` : '—'}</strong></div>
                      <div className="settings-row"><span>平均接收电平</span><strong>{selectedArchive.summary.avg_received_level_db !== null ? `${selectedArchive.summary.avg_received_level_db.toFixed(2)} dB` : '—'}</strong></div>
                    </div>

                    <div style={{ marginTop: 12, display: 'grid', gap: 6 }}>
                      <strong style={{ fontSize: 13 }}>归档文件</strong>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>场景快照：{selectedArchive.files.scenario_snapshot ?? '—'}</span>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>执行场景：{selectedArchive.files.execution_scene ?? '—'}</span>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>结果 CSV：{selectedArchive.files.results_csv ?? '—'}</span>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>事件 JSON：{selectedArchive.files.events_json ?? '—'}</span>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>射线 JSON：{selectedArchive.files.rays_json ?? '—'}</span>
                    </div>

                    <div style={{ marginTop: 12, display: 'grid', gap: 6 }}>
                      <strong style={{ fontSize: 13 }}>事件码预览</strong>
                      <span style={{ fontSize: 12, color: 'var(--text-dim)' }}>
                        {selectedArchive.summary.event_codes.length > 0
                          ? selectedArchive.summary.event_codes.join(' / ')
                          : '当前归档没有事件码摘要。'}
                      </span>
                    </div>

                    <div style={{ marginTop: 12, display: 'grid', gap: 6 }}>
                      <strong style={{ fontSize: 13 }}>运行日志尾部</strong>
                      <div className="log-list" style={{ maxHeight: 220, overflowY: 'auto' }}>
                        {selectedArchive.logs.length > 0 ? selectedArchive.logs.map((line, index) => (
                          <div key={`${selectedArchive.id}-${index}`} className="log-list__item">{line}</div>
                        )) : <div className="log-list__item">该归档未保存日志。</div>}
                      </div>
                    </div>
                  </>
                ) : null}
              </div>
            </div>

            <div className="asset-card" style={{ marginTop: 12 }}>
              <strong>对比报告</strong>
              <span style={{ color: 'var(--text-dim)' }}>
                {report ? `生成于 ${new Date(report.generated_at * 1000).toLocaleString('zh-CN')} · ${report.archive_count} 个归档` : '勾选归档后自动生成摘要与对比报告。'}
              </span>
              {reportError ? <div style={{ color: 'var(--danger)', fontSize: 13 }}>报告生成失败：{reportError}</div> : null}
              {reportLoading ? <div className="log-list__item">正在生成报告...</div> : null}
              {!reportLoading && report ? (
                <>
                  <div className="settings-grid" style={{ marginTop: 8 }}>
                    <div className="settings-row"><span>场景范围</span><strong>{report.scenario ?? '多场景混合'}</strong></div>
                    <div className="settings-row"><span>共有事件码</span><strong>{report.common_event_codes.length}</strong></div>
                    <div className="settings-row"><span>最低平均时延</span><strong>{delayWinner ? `${delayWinner.id} (${formatNullableNumber(delayWinner.value, 6)} s)` : '—'}</strong></div>
                    <div className="settings-row"><span>最高平均接收电平</span><strong>{powerWinner ? `${powerWinner.id} (${formatNullableNumber(powerWinner.value, 3)} dB)` : '—'}</strong></div>
                    <div className="settings-row"><span>最高平均 SNR</span><strong>{snrWinner ? `${snrWinner.id} (${formatNullableNumber(snrWinner.value, 3)} dB)` : '—'}</strong></div>
                    <div className="settings-row"><span>已选归档</span><strong>{report.archive_ids.join(' / ')}</strong></div>
                  </div>

                  <div className="data-table" style={{ marginTop: 12, overflowX: 'auto' }}>
                    <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
                      <thead>
                        <tr style={{ borderBottom: '1px solid rgba(148,163,184,0.18)', textAlign: 'left' }}>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>Archive</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>状态</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>链路</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>事件</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>平均时延</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>平均接收电平</th>
                          <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>平均 SNR</th>
                        </tr>
                      </thead>
                      <tbody>
                        {report.archives.map((item) => (
                          <tr key={item.id} style={{ borderBottom: '1px solid rgba(148,163,184,0.08)' }}>
                            <td style={{ padding: '6px 10px' }}>{item.id}</td>
                            <td style={{ padding: '6px 10px', color: item.status === 'success' ? 'var(--success)' : 'var(--danger)' }}>{item.status}</td>
                            <td style={{ padding: '6px 10px' }}>{item.rows}</td>
                            <td style={{ padding: '6px 10px' }}>{item.event_rows}</td>
                            <td style={{ padding: '6px 10px' }}>{formatNullableNumber(item.avg_delay_s, 6)}</td>
                            <td style={{ padding: '6px 10px' }}>{formatNullableNumber(item.avg_received_level_db, 3)}</td>
                            <td style={{ padding: '6px 10px' }}>{formatNullableNumber(item.avg_snr_db, 3)}</td>
                          </tr>
                        ))}
                      </tbody>
                    </table>
                  </div>

                  {report.pairwise_deltas.length > 0 ? report.pairwise_deltas.map((delta) => (
                    <div key={`${delta.baseline_id}-${delta.target_id}`} className="asset-card" style={{ marginTop: 12 }}>
                      <strong>{delta.target_id} 相对 {delta.baseline_id}</strong>
                      <div className="data-table" style={{ marginTop: 8, overflowX: 'auto' }}>
                        <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 12 }}>
                          <thead>
                            <tr style={{ borderBottom: '1px solid rgba(148,163,184,0.18)', textAlign: 'left' }}>
                              <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>指标</th>
                              <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>baseline</th>
                              <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>target</th>
                              <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>delta</th>
                              <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>delta %</th>
                            </tr>
                          </thead>
                          <tbody>
                            {delta.metrics.map((metric) => (
                              <tr key={`${delta.target_id}-${metric.metric}`} style={{ borderBottom: '1px solid rgba(148,163,184,0.08)' }}>
                                <td style={{ padding: '6px 10px' }}>{metric.metric}</td>
                                <td style={{ padding: '6px 10px' }}>{formatNullableNumber(metric.baseline, 6)}</td>
                                <td style={{ padding: '6px 10px' }}>{formatNullableNumber(metric.target, 6)}</td>
                                <td style={{ padding: '6px 10px' }}>{formatNullableNumber(metric.delta, 6)}</td>
                                <td style={{ padding: '6px 10px' }}>{metric.delta_percent !== null && metric.delta_percent !== undefined ? `${metric.delta_percent.toFixed(2)}%` : '—'}</td>
                              </tr>
                            ))}
                          </tbody>
                        </table>
                      </div>
                    </div>
                  )) : null}

                  {report.notes.length > 0 ? (
                    <ul className="bullet-panel" style={{ marginTop: 12 }}>
                      {report.notes.map((note, index) => <li key={`${note}-${index}`}>{note}</li>)}
                    </ul>
                  ) : null}

                  <div style={{ marginTop: 12, display: 'grid', gap: 6 }}>
                    <strong style={{ fontSize: 13 }}>Markdown 预览</strong>
                    <div className="log-list" style={{ maxHeight: 220, overflowY: 'auto' }}>
                      {report.markdown.split('\n').slice(0, 20).map((line, index) => (
                        <div key={`report-preview-${index}`} className="log-list__item">{line || ' '}</div>
                      ))}
                    </div>
                  </div>
                </>
              ) : null}
            </div>
          </>
        ) : null}
      </SectionCard>

      <SectionCard title="试验历史" eyebrow="运行历史">
        {history.length === 0 ? (
          <div className="log-list__item">本次会话尚无试验记录。完成仿真后会写入历史；若开启结果归档，还会同步生成实验档案。</div>
        ) : (
          <div className="data-table" style={{ overflowX: 'auto' }}>
            <table style={{ width: '100%', borderCollapse: 'collapse', fontSize: 13 }}>
              <thead>
                <tr style={{ borderBottom: '1px solid rgba(148,163,184,0.18)', textAlign: 'left' }}>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>时间</th>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>场景</th>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>状态</th>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>结果文件</th>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>链路数</th>
                  <th style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>归档</th>
                </tr>
              </thead>
              <tbody>
                {history.map((rec, i) => (
                  <tr key={i} style={{ borderBottom: '1px solid rgba(148,163,184,0.08)' }}>
                    <td style={{ padding: '6px 10px', color: 'var(--text-dim)' }}>
                      {new Date(rec.timestamp * 1000).toLocaleTimeString('zh-CN')}
                    </td>
                    <td style={{ padding: '6px 10px' }}>{rec.scenario}</td>
                    <td style={{ padding: '6px 10px', color: rec.exit_code === 0 ? 'var(--success)' : 'var(--danger)' }}>
                      {rec.exit_code === 0 ? '✓ 成功' : `✗ exit=${rec.exit_code}`}
                    </td>
                    <td style={{ padding: '6px 10px', color: 'var(--text-dim)', fontSize: 11 }}>
                      {rec.csv_file ?? '—'}
                    </td>
                    <td style={{ padding: '6px 10px' }}>{rec.rows}</td>
                    <td style={{ padding: '6px 10px', fontSize: 12, color: rec.archived ? 'var(--success)' : 'var(--text-dim)' }}>
                      {rec.archive_id ? rec.archive_id : rec.archive_error ? `失败：${rec.archive_error}` : '未归档'}
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </SectionCard>

      <SectionCard title="后续资产能力" eyebrow="后续规划">
        <ul className="bullet-panel">
          <li>场景版本对比</li>
          <li>多实验指标联动分析</li>
          <li>回放模式与注释留痕</li>
        </ul>
      </SectionCard>
    </div>
  );
}