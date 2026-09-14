import { useEffect, useMemo, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Link, useParams } from "react-router-dom";
import { apiClient, ApiFailure } from "../api/client";
import { queries } from "../api/queries";
import { emptyProjection, RunEventProjection } from "../api/runEvents";
import type { PositionSample, ResultDto, RunEventDto, ScenarioDto } from "../api/types";
import { ErrorState, LoadingState, Status } from "../components/common";
import { MiniTrend } from "../components/MiniTrend";
import { formatNanoseconds } from "../domain/format";
import { cumulativeTrends, eventTechnicalDetail, latestActiveLink, projectionCounts } from "../domain/runAnalytics";
import { createWorldViewTransform } from "../domain/worldViewTransform";
import { eventCurve, positionAt, positionSamples, replayDelay, seekTime, studioEvents, timePercent, type StudioEvent } from "../domain/studio";

function useEvents(runId: string) {
  const [projection, update] = useState(emptyProjection);
  const [error, fail] = useState<ApiFailure | null>(null);
  useEffect(() => {
    update(emptyProjection); fail(null);
    const source = new RunEventProjection(apiClient.url(`/runs/${encodeURIComponent(runId)}/events`), update, fail);
    source.connect(); return () => source.close();
  }, [runId]);
  return { projection, error };
}

function StudioCanvas({ scenario, samples, row, events, selected, select, range }: {
  scenario: ScenarioDto; samples: PositionSample[]; row?: StudioEvent; events: readonly RunEventDto[];
  selected: string | null; select: (id: string) => void; range?: number;
}) {
  const positions = scenario.nodes.map(node => ({ node, p: row ? positionAt(samples, node.node_id, row.at) : null }));
  const exact = positions.every(item => item.p !== null);
  const transform = createWorldViewTransform([...scenario.nodes.map(n => n.initial_position), ...samples], range, { width: 980, height: 540, padding: 50 });
  const points = new Map(positions.map(({ node, p }) => [node.node_id, transform.point(p ?? node.initial_position)]));
  const activity = row && ["Transmission", "SignalArrival", "Reception"].includes(row.kind) ? latestActiveLink(events) : null;
  const tx = activity?.sender ? points.get(activity.sender) : null;
  const rx = activity?.receiver ? points.get(activity.receiver) : null;
  return <><div className="canvas-toolbar"><strong>二维运行场景</strong><span>{exact ? "正式轨迹位置 · ns-3 仿真时刻" : "场景初始几何 · 当前通信事件"}</span>{activity && <b>{activity.outcome}</b>}</div>
    <svg className={`run-topology phase-${activity?.phase.toLowerCase() ?? "idle"}`} viewBox="0 0 980 540" role="img" aria-label="运行画布">
      <defs><pattern id="studio-grid" width="49" height="54" patternUnits="userSpaceOnUse"><path d="M49 0H0V54" fill="none" stroke="#20364c" /></pattern><marker id="studio-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="8" markerHeight="8" orient="auto"><path d="M0 0L10 5L0 10Z" fill="#38bdf8" /></marker></defs>
      <rect width="980" height="540" fill="#071827" /><rect width="980" height="540" fill="url(#studio-grid)" />
      <text x="20" y="28" fill="#8da9bf">局部笛卡尔坐标 · 米 · XY 等比例</text>
      {scenario.nodes.map(node => <polyline key={`track-${node.node_id}`} data-testid={`track-${node.node_id}`} points={samples.filter(p => p.node_id === node.node_id).map(p => { const xy = transform.point(p); return `${xy.x},${xy.y}`; }).join(" ")} fill="none" stroke="#5a829a" strokeDasharray="4 4" strokeWidth="2" />)}
      {tx && rx && <path data-testid="communication-arrow" d={eventCurve(tx, rx)} fill="none" stroke={activity?.outcome === "NotDecoded" ? "#fb7185" : activity?.phase === "Reception" ? "#34d399" : activity?.phase === "SignalArrival" ? "#fbbf24" : "#38bdf8"} strokeWidth="3" markerEnd="url(#studio-arrow)" />}
      {positions.map(({ node, p }) => { const xy = points.get(node.node_id)!; const fusion = row?.kind === "Fusion" && node.node_id === scenario.fusion_center_node_id;
        return <g key={node.node_id} transform={`translate(${xy.x} ${xy.y})`} onClick={() => select(node.node_id)} onKeyDown={e => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); select(node.node_id); } }} role="button" tabIndex={0} aria-label={`节点 ${node.node_id}`}>
          {(selected === node.node_id || fusion) && <circle r="29" fill="none" stroke={fusion ? "#fbbf24" : "#22d3ee"} strokeWidth="2" />}
          <circle r="14" fill={node.node_id === activity?.receiver && activity.phase === "SignalArrival" ? "#fbbf24" : node.node_id === activity?.receiver && activity.outcome === "NotDecoded" ? "#fb7185" : node.node_id === activity?.receiver && activity.phase === "Reception" ? "#34d399" : node.node_id === activity?.sender ? "#38bdf8" : node.node_id === scenario.fusion_center_node_id ? "#22d3ee" : "#f59e0b"} />
          <text x="22" y="-3" fill="#edf6ff">N{node.node_id}{fusion ? " · 融合完成" : ""}</text><text x="22" y="15" fill="#9bb0c5" fontSize="11">{(p ?? node.initial_position).z_meters.toFixed(1)} m</text>
        </g>; })}
    </svg><div className="canvas-caption">虚线：正式轨迹查询样本。{exact ? "端点来自 Runtime 轨迹查询；仅允许在相邻正式样本间做显示插值，不产生仿真状态。" : "当前位置尚未投影，暂显示初始几何；不根据初始速度推算运动。"}</div></>;
}

export function StudioRunPage() {
  const runId = useParams().runId ?? "";
  const run = useQuery({ ...queries.run(runId), refetchInterval: q => q.state.data?.lifecycle === "Completed" || q.state.data?.lifecycle === "Failed" ? false : 500 });
  const scenario = useQuery({ ...queries.scenario(run.data?.scenario_id ?? "", run.data?.scenario_version ?? ""), enabled: !!run.data });
  const experiment = useQuery({ ...queries.experiment(run.data?.experiment_id ?? "", run.data?.experiment_version ?? ""), enabled: !!run.data });
  const environment = useQuery({ ...queries.environment(run.data?.environment_asset_id ?? ""), enabled: !!run.data });
  const result = useQuery({ ...queries.result(runId), enabled: run.data?.lifecycle === "Completed" });
  const { projection, error } = useEvents(runId);
  const rows = useMemo(() => studioEvents(projection.timeline), [projection.timeline]);
  const streamedSamples = useMemo(() => positionSamples(projection.timeline), [projection.timeline]);
  const samples = streamedSamples.length ? streamedSamples : result.data?.visualization?.positions ?? [];
  const [cursor, setCursor] = useState<number | null>(null), [playing, play] = useState(false), [speed, setSpeed] = useState(1), [compressed, setCompressed] = useState(false);
  const [selected, select] = useState<string | null>(null);
  const index = Math.min(cursor ?? Math.max(0, rows.length - 1), Math.max(0, rows.length - 1));
  const row = rows[index];
  useEffect(() => {
    if (!playing || !rows[index + 1]) { if (playing) play(false); return; }
    const delay = replayDelay(rows[index].at, rows[index + 1].at, speed); setCompressed(delay.compressed);
    const timer = window.setTimeout(() => setCursor(index + 1), delay.milliseconds); return () => clearTimeout(timer);
  }, [playing, index, rows, speed]);
  if (run.error || scenario.error || experiment.error) return <ErrorState error={run.error ?? scenario.error ?? experiment.error} />;
  if (!run.data || !scenario.data || !experiment.data) return <LoadingState />;
  const visible = row ? projection.timeline.filter(event => BigInt(event.sequence) <= BigInt(row.event.sequence)) : [];
  const counts = projectionCounts(visible), trends = cumulativeTrends(visible);
  const node = scenario.data.nodes.find(n => n.node_id === selected);
  const p = node && row ? positionAt(samples, node.node_id, row.at) : null;
  const progress = Number(BigInt(counts.cycles) * 100n / (BigInt(experiment.data.simulation_cycle_count) || 1n));
  return <div className="studio-product">
    <header className="studio-command"><div><small>仿真工作台 / {runId}</small><h1>{experiment.data.name}</h1></div><Status value={run.data.lifecycle} /><strong>{row ? formatNanoseconds(row.at) : "等待正式事件"}</strong><Link to={`/results/${runId}`}>查看结果 →</Link></header>
    <div className="studio-context">环境 {run.data.environment_asset_id} · 周期 {counts.cycles}/{experiment.data.simulation_cycle_count} · {progress}% · ns-3 事件时间</div>
    {error && (error.kind !== "TransportUnavailable" || run.data.lifecycle !== "Completed") && <ErrorState error={error} />}{result.error && <ErrorState error={result.error} />}
    <section className="r4-studio-layout"><article className="studio-surface"><StudioCanvas scenario={scenario.data} samples={samples} row={row} events={visible} selected={selected} select={select} range={environment.data?.axes.horizontal_range.maximum} /></article>
      <aside className="studio-inspector link-inspector"><h2>{node ? "节点位置" : "事件检查器"}</h2>{node ? <><h3>N{node.node_id}</h3><p>{p ? `${p.x_meters.toFixed(3)}, ${p.y_meters.toFixed(3)}, ${p.z_meters.toFixed(3)} m` : "该时刻无位置投影"}</p><p>位置来自正式轨迹查询事件；相邻样本间仅做视觉插值。</p></> : <><h3>{row?.title ?? "等待事件"}</h3><p>{row ? `原始序列 ${row.event.sequence}` : ""}</p><p>{row ? eventTechnicalDetail(row.event) : ""}</p></>}
      <div className="studio-node-buttons">{scenario.data.nodes.map(n => <button key={n.node_id} onClick={() => select(n.node_id)}>N{n.node_id}</button>)}</div><button onClick={() => select(null)}>查看当前通信</button>
      <h3>观测边界</h3><p>ChannelQuery 只表示信道求解；SignalArrival 才表示信号在传播时延后实际到达。</p><p>播放控制只改变观看位置和速度，不推进、暂停或修改 ns-3 仿真。</p></aside></section>
    <section className="studio-history"><div className="studio-history-controls"><strong>离散事件时间轴</strong><button disabled={!rows.length} onClick={() => { play(false); setCursor(index); }}>暂停观察</button><button disabled={!rows.length || index === 0} onClick={() => { play(false); setCursor(Math.max(0, index - 1)); }}>上一事件</button><button disabled={!rows.length || index >= rows.length - 1} onClick={() => { play(false); setCursor(Math.min(rows.length - 1, index + 1)); }}>下一事件</button><button disabled={!rows.length} onClick={() => { if (playing) { play(false); return; } setCursor(index >= rows.length - 1 ? 0 : index); play(true); }}>{playing ? "暂停回放" : "播放事件"}</button><button onClick={() => { play(false); setCursor(null); }}>实时跟随</button><label>播放速度 <select aria-label="回放速度" value={speed} onChange={e => setSpeed(Number(e.target.value))}>{[0.5, 1, 2, 4].map(s => <option key={s} value={s}>{s}×</option>)}</select></label><span>仿真时间 {row ? formatNanoseconds(row.at) : "等待事件"}</span><span>{compressed ? "空闲间隔已压缩" : cursor === null ? "正在跟随事件流" : "回放不控制 ns-3"}</span></div>
      <input aria-label="按仿真时间定位" type="range" min="0" max="100" step="0.01" value={row ? timePercent(row.at, rows[0].at, rows.at(-1)!.at) : 0} onChange={e => { play(false); setCursor(seekTime(rows, Number(e.target.value))); }} />
      {(["Transmission", "ChannelOutcome", "SignalArrival", "Reception", "Fusion", "CycleCommit"] as const).map((kind, lane) => <div className="event-lane" key={kind}><span>{["发送", "信道求解", "实际到达", "解调/接收", "融合", "周期提交"][lane]}</span><div>{rows.map((item, i) => item.kind === kind && <button key={item.key} title={`${item.title} · ${formatNanoseconds(item.at)}`} aria-label={`${item.title} · ${item.key}`} className={i === index ? "selected" : ""} style={{ left: `${timePercent(item.at, rows[0].at, rows.at(-1)!.at)}%` }} onClick={() => { play(false); setCursor(i); }} />)}</div></div>)}
    </section><section className="studio-trends"><MiniTrend title="累计发送" unit="次" points={trends.transmissions} empty="暂无发送记录" /><MiniTrend title="接收处理" unit="次" points={trends.receptions} empty="暂无接收记录" /><MiniTrend title="传播时延" unit="s" points={trends.delay} empty="暂无信道记录" /></section>
  </div>;
}

function ValueTable({ rows }: { rows: [string, string][] }) { return <table className="studio-values"><tbody>{rows.map(([name, value]) => <tr key={name}><th>{name}</th><td>{value}</td></tr>)}</tbody></table>; }

export function StudioResultPage() {
  const runId = useParams().runId ?? "";
  const result = useQuery(queries.result(runId));
  const experiment = useQuery({ ...queries.experiment(result.data?.experiment_id ?? "", result.data?.experiment_version ?? ""), enabled: !!result.data });
  const { projection, error } = useEvents(runId);
  const [fusionId, selectFusion] = useState<string | null>(null);
  if (result.error) return <ErrorState error={result.error} />;
  if (!result.data) return <LoadingState />;
  const item = result.data, p = item.projection, v = item.visualization, a = item.acceptance_report;
  const trends = cumulativeTrends(projection.timeline), counts = projectionCounts(projection.timeline);
  const fused = item.fusion_results.find(f => f.fusion_sequence === fusionId) ?? item.fusion_results[0];
  const sources = v?.fusion_sources.filter(s => s.fusion_sequence === fused?.fusion_sequence);
  const distinctSources = sources ? [...new Set(sources.map(s => s.node_id))] : null;
  const missing = "未提供正式数据";
  return <div className="studio-product studio-results"><header className="studio-command"><div><small>实验结果 / {runId}</small><h1>{experiment.data?.name ?? "仿真结果分析"}</h1></div><Link to={`/runs/${runId}`}>打开历史回放</Link></header>
    <nav className="studio-tabs" aria-label="结果分区">{["运行概览", "网络性能", "水声链路", "波形通信", "探测与融合"].map((name, i) => <a key={name} href={`#result-${i}`}>{name}</a>)}</nav>
    <section id="result-0" className="studio-section"><h2>运行概览</h2><ValueTable rows={[["仿真时间", formatNanoseconds(p.simulation_duration_ns)], ["节点规模", p.node_count], ["发送次数", p.transmission_count], ["接收处理次数", p.reception_count], ["本地交付次数", p.local_delivery_count], ["完成周期", p.cycle_count], ["当前/累计活动链路", missing]]} /></section>
    <section id="result-1" className="studio-section"><h2>网络性能</h2><div className="studio-result-columns"><ValueTable rows={[["成功交付率 / 丢包", "缺少正式业务包总数与去重交付统计"], ["端到端时延", "缺少业务包产生时间投影"], ["吞吐量 / 有效吞吐量", "缺少交付字节统计；配置速率不作为吞吐量"], ["重传 / 碰撞", missing], ["配置通信速率", experiment.data ? `${experiment.data.phy.bit_rate_bits_per_second} bit/s` : missing]]} /><div><MiniTrend title="累计发送" unit="次" points={trends.transmissions} empty="等待事件历史" /><p>接收处理历史：交付 {counts.localDeliveries} · 旁听 {counts.overheard} · 未解码 {counts.notDecoded} · 中继入队 {counts.relayEnqueue}。只统计已读取记录，不将接收次数与发送次数相减作为丢包。</p></div></div></section>
    <section id="result-2" className="studio-section"><h2>水声链路</h2>{error && error.kind !== "TransportUnavailable" && <ErrorState error={error} />}<div className="studio-trends"><MiniTrend title="传播时延" unit="s" points={trends.delay} empty="无传播时延记录" /><MiniTrend title="传播损失" unit="dB" points={trends.loss} empty="无传播损失记录" /><MiniTrend title="接收质量 BER" unit="" points={trends.ber} empty="无接收质量记录" /></div><details><summary>逐链路真实数据：传播时延 / TL / 多径数量 / SNR</summary><table><thead><tr><th>时间</th><th>Tx</th><th>接收节点</th><th>物理量</th></tr></thead><tbody>{projection.timeline.map(e => e.trace.kind === "ChannelOutcome" && e.trace.payload.outcome.type === "Signal" ? <tr key={e.sequence}><td>{formatNanoseconds(e.trace.occurred_at_ns)}</td><td>{e.trace.payload.transmission_id}</td><td>N{e.trace.payload.receiver_node_id}</td><td>{formatNanoseconds(e.trace.payload.outcome.first_arrival_delay_ns)} · TL {e.trace.payload.outcome.aggregate_transmission_loss_db} dB · {e.trace.payload.outcome.path_count} 路径</td></tr> : e.trace.kind === "Reception" && e.trace.payload.quality ? <tr key={e.sequence}><td>{formatNanoseconds(e.trace.occurred_at_ns)}</td><td>{e.trace.payload.transmission_id}</td><td>N{e.trace.payload.receiver_node_id}</td><td>SNR {e.trace.payload.quality.signal_to_noise_ratio_db} dB · BER {e.trace.payload.quality.bit_error_rate} · {e.trace.payload.quality.source}</td></tr> : null)}</tbody></table></details><p>最大时延扩展未投影。Modeled BER 是仿真模型证据，不是硬件实测；也不等于 Channel2 实际误码。</p></section>
    <section id="result-3" className="studio-section"><h2>波形通信</h2>{v?.waveform.length ? <table><thead><tr><th>Tx / 接收节点</th><th>实际 bit errors / payload bits</th><th>实际 BER</th><th>CRC</th></tr></thead><tbody>{v.waveform.map((w, i) => <tr key={`${w.transmission_id}-${w.receiver_node_id}-${i}`}><td>{w.transmission_id} / N{w.receiver_node_id}</td><td>{w.bit_error_count} / {w.payload_bit_count}</td><td>{BigInt(w.payload_bit_count) ? Number(w.bit_error_count) / Number(w.payload_bit_count) : "无 payload bits"}</td><td>{w.crc_ok ? "通过" : "失败"}</td></tr>)}</tbody></table> : <p>本次运行没有正式波形接收记录。当前公开实验入口未选择 Channel2；不把抽象 PHY 的 BER 当作实际波形误码。</p>}<p>波形样本、独立解调成功率尚未投影。</p></section>
    <section id="result-4" className="studio-section"><h2>探测与融合</h2><ValueTable rows={[["产生的特征报告数", v?.generated_feature_count ?? missing], ["成功交付特征数", "未单独投影；本地交付次数不是有效特征数"], ["融合轮次", String(item.fusion_results.length)]]} />
      <div className="studio-result-columns"><div><h3>融合历史</h3>{item.fusion_results.map(f => <button className={`fusion-history-row ${f === fused ? "selected" : ""}`} key={f.fusion_sequence} onClick={() => selectFusion(f.fusion_sequence)}>轮次 {f.fusion_sequence} · {formatNanoseconds(f.completed_at_ns)} · {f.observation_count} 个有效观测</button>)}</div><article>{fused ? <><h3>轮次 {fused.fusion_sequence} 详情</h3><ValueTable rows={[["有效方位观测", fused.observation_count], ["来源节点", distinctSources ? distinctSources.map(n => `N${n}`).join("、") : missing], ["方位来源节点数", distinctSources ? String(distinctSources.length) : missing], ["本轮融合耗时", formatNanoseconds(fused.fusion_period_ns)], ["融合估计位置", `(${fused.estimated_target_x_meters}, ${fused.estimated_target_y_meters}) m`]]} /><p>采用的观测身份：{sources ? sources.map(s => `N${s.node_id} / #${s.observation_sequence}`).join("，") : missing}</p><p>逐条方位值、采样时间与无效观测明细未保留在融合结果投影中，不从发送端报告补造。</p></> : <p>本次没有形成融合结果。</p>}</article></div>
      {a && <details><summary>参考要求与本次量化值</summary><table><thead><tr><th>项目</th><th>参考要求</th><th>本次值</th></tr></thead><tbody><tr><td>有效方位观测（各轮最小值）</td><td>≥{a.required_minimum_bearing_points}</td><td>{a.minimum_bearing_points ?? missing}</td></tr><tr><td>融合耗时（各轮最大值）</td><td>≤{formatNanoseconds(a.required_maximum_fusion_period_ns)}</td><td>{a.maximum_fusion_period_ns ? formatNanoseconds(a.maximum_fusion_period_ns) : missing}</td></tr><tr><td>目标链路 BER（模型证据）</td><td>≤{a.required_maximum_ber}</td><td>{a.maximum_ber ?? missing}</td></tr></tbody></table><p>由用户或验收人员结合数据来源与覆盖情况判断。</p></details>}
    </section><section className="studio-section"><h2>资源来源</h2><Link to={`/experiments/${item.experiment_id}/versions/${item.experiment_version}`}>{item.experiment_id} · v{item.experiment_version}</Link><p>场景 {item.scenario_id} · 环境 {item.environment_asset_id} · 最终快照 {p.final_snapshot_version}</p></section>
  </div>;
}
