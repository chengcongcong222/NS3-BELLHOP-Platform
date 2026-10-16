import { useEffect, useMemo, useState } from "react";
import { useQuery } from "@tanstack/react-query";
import { Link, useParams } from "react-router-dom";
import { apiClient } from "../api/client";
import { queries } from "../api/queries";
import { emptyProjection, RunEventProjection } from "../api/runEvents";
import type { ExperimentDto, ResultDto, RunEventDto } from "../api/types";
import { ErrorState, LoadingState } from "../components/common";
import { formatNanoseconds } from "../domain/format";
import { acceptanceReference, matchingRunExperiment, physicalMode, resultEventStatistics, sampleSummary, targetBerReference, waveformSummary, type BerEvidenceSource } from "../domain/resultAnalysis";
import type { RecordedChannelConfig } from "../api/types";
import { RecordedSource } from "./recordedExperiment";
import { cumulativeTrends, type TrendPoint } from "../domain/runAnalytics";
import "./resultAnalysis.css";

export interface ResultAnalysisProps {
  result: ResultDto;
  events?: readonly RunEventDto[];
  experiment?: ExperimentDto | null;
  physicalLayer?: string | null;
  eventStreamComplete?: boolean;
}

export function StudioResultPage() {
  const runId = useParams().runId ?? "";
  const result = useQuery(queries.result(runId));
  const run = useQuery(queries.run(runId));
  const context = useQuery({ queryKey: ["run-definition-context", runId], queryFn: () => apiClient.getRunDefinitionContext(runId) });
  const [projection, update] = useState(emptyProjection);
  const [eventIssue, setEventIssue] = useState(false);
  useEffect(() => {
    update(emptyProjection); setEventIssue(false);
    const source = new RunEventProjection(apiClient.url(`/runs/${encodeURIComponent(runId)}/events`), update, error => setEventIssue(error.kind !== "TransportUnavailable"));
    source.connect(); return () => source.close();
  }, [runId]);
  if (result.error) return <ErrorState error={result.error} />;
  if (!result.data) return <LoadingState />;
  const implementation = context.data?.execution.physical_layer;
  return <div className="analysis-page"><header className="analysis-page-toolbar"><h1>仿真结果分析</h1><Link to={`/runs/${encodeURIComponent(runId)}`}>返回运行与回放</Link></header>
    {context.error && <p className="analysis-page-warning">本次运行的配置快照暂未读取；结果仍可查看，不从其他实验补用配置。</p>}
    {eventIssue && <p className="analysis-page-warning">事件明细连接暂不可用；保留已有事件与正式结果汇总，不把缺失记录计为零。</p>}
    <ResultAnalysis result={result.data} events={projection.timeline} experiment={context.data?.experiment} physicalLayer={typeof implementation === "string" ? implementation : context.data?.experiment.phy.physical_layer} eventStreamComplete={run.data?.event_stream_complete === true} />
  </div>;
}

const number = (value: number, digits = 3) => value === 0 ? "0" : Math.abs(value) < 10 ** -digits ? value.toExponential(3) : value.toLocaleString("zh-CN", { maximumFractionDigits: digits });
const berNumber = (value: number) => value !== 0 && Math.abs(value) < 0.001 ? value.toExponential(3) : number(value, 4);
const quantity = (value: number, unit: string) => unit === "BER" || unit === "无量纲" ? berNumber(value) : number(value, unit === "dB" ? 2 : 3);
const percent = (value: number) => `${number(value * 100)}%`;
const seconds = (nanoseconds: string) => `${number(Number(BigInt(nanoseconds)) / 1e9)} s`;
const berSourceLabel: Record<BerEvidenceSource, string> = {
  Modeled: "模型估计", Measured: "接收质量实测记录", External: "外部质量记录",
  Waveform: "接收波形逐比特统计（仿真，非硬件实测）", Unspecified: "正式报告（未附证据来源）",
};

function Facts({ rows }: { rows: Array<[string, string | number, string?]> }) {
  return <table className="analysis-facts"><thead><tr><th>统计量</th><th>本次值</th><th>单位 / 统计口径</th></tr></thead><tbody>{rows.map(([label, value, unit]) => <tr key={label}><th scope="row">{label}</th><td>{value}</td><td>{unit ?? "—"}</td></tr>)}</tbody></table>;
}

function ExplainedValues({ rows }: { rows: Array<{ label: string; value: string | number; unit?: string; explanation: string }> }) {
  return <dl className="analysis-explained-values">{rows.map(row => <div key={row.label}><dt>{row.label}<small>{row.explanation}</small></dt><dd>{row.value}{row.unit && <span> {row.unit}</span>}</dd></div>)}</dl>;
}

function SampleTable({ rows }: { rows: Array<[string, string, readonly number[]]> }) {
  const populated = rows.flatMap(([label, unit, values]) => { const summary = sampleSummary(values); return summary ? [{ label, unit, ...summary }] : []; });
  if (!populated.length) return <p className="analysis-note">正在载入事件记录；下方只展示已保存的汇总，不补造缺失样本。</p>;
  return <table className="analysis-samples"><thead><tr><th>统计量 / 单位</th><th>样本数 n</th><th>最小值</th><th>均值</th><th>最大值</th></tr></thead><tbody>{populated.map(row => <tr key={row.label}><th scope="row">{row.label}<small>{row.unit}</small></th><td>{row.count}</td><td>{quantity(row.minimum, row.unit)}</td><td>{quantity(row.mean, row.unit)}</td><td>{quantity(row.maximum, row.unit)}</td></tr>)}</tbody></table>;
}

function ObservedChart({ title, unit, points, step = false }: { title: string; unit: string; points: readonly TrendPoint[]; step?: boolean }) {
  if (!points.length) return null;
  const start = points.reduce((first, point) => BigInt(point.timeNs) < first ? BigInt(point.timeNs) : first, BigInt(points[0].timeNs));
  const end = points.reduce((last, point) => BigInt(point.timeNs) > last ? BigInt(point.timeNs) : last, start);
  const summary = sampleSummary(points.map(point => point.value))!;
  const low = step ? 0 : summary.minimum, high = summary.maximum;
  const coords = points.map(point => ({ x: 64 + Number(BigInt(point.timeNs) - start) / Number(end - start || 1n) * 422, y: 161 - (point.value - low) / (high - low || 1) * 132 }));
  const path = coords.map((point, i) => i === 0 ? `M${point.x},${point.y}` : step ? `H${point.x}V${point.y}` : `L${point.x},${point.y}`).join(" ");
  return <figure className="analysis-figure"><figcaption>{title} <span>{unit}</span></figcaption><svg viewBox="0 0 520 210" role="img" aria-label={`${title}趋势`}>
    {[0, 0.5, 1].map(ratio => <g key={ratio}><line x1="64" x2="486" y1={161 - ratio * 132} y2={161 - ratio * 132} className="analysis-grid-line" /><text x="54" y={165 - ratio * 132} textAnchor="end">{quantity(low + (high - low) * ratio, unit)}</text></g>)}
    <path d="M64 20V161H486" className="analysis-axis" />{step && <path d={path} className="analysis-curve" />}
    {coords.map((point, index) => <circle key={index} cx={point.x} cy={point.y} r={step ? 2.3 : 3} className="analysis-point"><title>{`${quantity(points[index].value, unit)} ${unit} · ${formatNanoseconds(points[index].timeNs)}`}</title></circle>)}
    <text x="64" y="184">{number(Number(start) / 1e9)}</text><text x="486" y="184" textAnchor="end">{number(Number(end) / 1e9)}</text><text x="274" y="203" textAnchor="middle">仿真时间 / s</text>
  </svg></figure>;
}

export function ResultAnalysis({ result, events = [], experiment: experimentInput, physicalLayer, eventStreamComplete = false }: ResultAnalysisProps) {
  const [fusionId, selectFusion] = useState<string | null>(null);
  const stats = useMemo(() => resultEventStatistics(result, events, eventStreamComplete), [result, events, eventStreamComplete]);
  const trends = useMemo(() => cumulativeTrends(stats.events), [stats.events]);
  const experiment = matchingRunExperiment(result, experimentInput);
  const mode = physicalMode(result, physicalLayer ?? experiment?.phy.physical_layer), waveform = waveformSummary(result), p = result.projection;
  const referenceBer = useMemo(() => targetBerReference(result, stats, mode), [result, stats, mode]);
  const fused = result.fusion_results.find(item => item.fusion_sequence === fusionId) ?? result.fusion_results[0];
  const sources = result.visualization?.fusion_sources.filter(item => item.fusion_sequence === fused?.fusion_sequence);
  const distinctSources = sources ? [...new Set(sources.map(item => item.node_id))] : null;
  const communicationOnly = experiment?.fusion.workload === "CommunicationOnly";
  const observedSnr = (mode === "B" || mode === "C") ? stats.waveformSnr : mode === "A" ? stats.snr : [];
  const delaySummary = sampleSummary(stats.delay), lossSummary = sampleSummary(stats.loss), snrSummary = sampleSummary(observedSnr);
  const unavailableSnr = (mode === "B" || mode === "C") && stats.waveformNoNoise > 0 && stats.waveformNoSignal === 0 && stats.waveformPowerMissing === 0
    ? "未加噪声" : (mode === "B" || mode === "C") && stats.waveformNoSignal > 0 && stats.waveformNoNoise === 0 && stats.waveformPowerMissing === 0 ? "无信号功率" : "尚无可计算记录";
  const modeled = stats.events.flatMap(event => event.trace.kind === "Reception" && event.trace.payload.quality?.source === "Modeled" ? [{ timeNs: event.trace.occurred_at_ns, value: event.trace.payload.quality.bit_error_rate }] : []);
  const sections = [["overview", "运行概览"], ["network", "网络性能"], ["acoustic", mode === "C" ? "录音依据" : "水声链路"], ...((mode === "B" || mode === "C") ? [["waveform", "波形通信"]] : []), ["fusion", "探测与融合"], ["reference", "参考要求"]];
  return <article className="result-analysis-paper">
    <header className="analysis-heading"><h2>{experiment?.name ?? "本次仿真结果"}</h2><div className="analysis-mode">{mode === "A" ? "模式 A · 快速抽象仿真" : mode === "C" ? "模式 C · 实测信道回放" : mode === "B" ? "模式 B · 波形级仿真" : "物理模式未随结果提供"}</div></header>
    <nav className="analysis-nav" aria-label="结果分区">{sections.map(([id, title]) => <a key={id} href={`#analysis-${id}`}>{title}</a>)}</nav>
    <section id="analysis-overview" className="analysis-section"><h3><span aria-hidden="true">01</span>运行概览</h3><div className="analysis-summary">
      <div><small>仿真时长</small><strong title={formatNanoseconds(p.simulation_duration_ns)}>{seconds(p.simulation_duration_ns)}</strong></div><div><small>参与节点</small><strong>{p.node_count}<em> 个</em></strong></div><div><small>发送次数</small><strong>{p.transmission_count}<em> 次</em></strong></div><div><small>数据送达</small><strong>{p.local_delivery_count}<em> 次</em></strong></div>
    </div><p className="analysis-note">汇总覆盖完整运行。{stats.complete ? "图表所需记录已载入。" : `图表目前载入 ${stats.events.length} 条事件，尚未载入的记录不当作零。`}下方解释每个数字的含义，详细记录可按需展开。</p></section>
    {mode === "C" && <section id="analysis-acoustic" className="analysis-section"><h3>录音资源与实验假设</h3>
      {result.provenance?.recorded_channel ? <RecordedSource source={result.provenance.recorded_channel as RecordedChannelConfig} /> : <p>此记录没有完整录音来源清单。</p>}
      <p>下方误码与 CRC 来自本次波形回放解码，不是数据采集时的原始通信结果。录音为归一化信道估计，不提供绝对传播损失或声压级。</p>
      <SampleTable rows={[["本次接收样本信号 / 噪声功率比（非标定）", "dB", stats.waveformSnr]]} />
      <p>{stats.waveformNoNoise} 次未额外加噪声。脉冲噪声的样本功率比不解释为理论 SNR。</p>
    </section>}
    <div className="analysis-layout">
      <section id="analysis-network" className="analysis-section"><h3><span aria-hidden="true">02</span>网络性能</h3><div className="analysis-two-column"><ExplainedValues rows={[
        { label: "节点发送速率", value: experiment?.phy.bit_rate_bits_per_second ?? "配置快照未提供", unit: experiment ? "bit/s" : undefined, explanation: "本次运行冻结的实验配置：节点发送时每秒传出的数据位数，不是整场实验的有效吞吐量。" },
        { label: "发出了多少次？", value: p.transmission_count, unit: "次", explanation: "所有节点实际发出的次数。一次广播也只记一次；它不是新建业务消息的数量。" },
        { label: "有多少次数据真正送达？", value: p.local_delivery_count, unit: "次", explanation: "接收的数据交给了目标节点的业务程序。不含旁听或等待中继转发；同一数据再次送达仍可能再计一次。" },
      ]} /><ObservedChart title="发送次数随时间增加" unit="次" points={trends.transmissions} step /></div>
        <p className="analysis-note">不能用“送达次数 ÷ 发送次数”直接计算业务成功率：广播、多跳转发和重复送达的计数不同。</p>
        <details className="analysis-diagnostic"><summary>查看接收分类与网络详细统计</summary><Facts rows={[
        ["接收处理", p.reception_count, "次；含旁听与未解码"], ["完成通信周期", p.cycle_count, "周期"],
        ...(stats.events.length ? [["本地交付 / 旁听", `${stats.localDelivery} / ${stats.overheard}`, "已载入接收事件"], ["未解码 / 中继入队", `${stats.notDecoded} / ${stats.relay}`, "已载入接收事件"], ["累计目标有向链路", stats.links.size, "已载入单播发送；去重节点对"], ["去重本地交付", stats.deliveredPairs, "已载入记录；数据包 × 接收节点"]] as Array<[string, string | number, string]> : []),
        ...(stats.targetDecodeRatio !== null ? [["目标链路解码比例", percent(stats.targetDecodeRatio), `${stats.targetsDecoded} / ${stats.targetAttempts} 次单播发送；不是业务交付率`]] as Array<[string, string, string]> : []),
        ...(experiment ? [["配置发送比特率", `${experiment.phy.bit_rate_bits_per_second} bit/s`, "发送参数，不等于有效吞吐量"]] as Array<[string, string, string]> : []),
      ]} />
        {stats.receiveDurationSeconds.length > 0 && <><SampleTable rows={[["目标链路发送至接收处理结束", "s", stats.receiveDurationSeconds]]} /><p className="analysis-note">上表含发送时长、传播和接收处理，包含成功与失败；并非从业务产生到最终交付的端到端时延。</p></>}
        <p className="analysis-note">旁听：数据不是发给该节点，但该节点听到了并解出了它。中继入队：节点将数据放入待转发队列，还不等于送达最终业务。</p></details>
      </section>
      {mode !== "C" && <section id="analysis-acoustic" className="analysis-section"><h3><span aria-hidden="true">03</span>水声链路</h3>
        <div className={mode === "B" ? "analysis-acoustic-overview" : "analysis-acoustic-overview analysis-two-column"}><ExplainedValues rows={[
          { label: "平均信号传播用时", value: delaySummary ? number(delaySummary.mean) : "尚无记录", unit: delaySummary ? "s" : undefined, explanation: "声波从发出到第一条传播路径抵达的平均用时，不含排队和发送数据所需时间。" },
          ...(lossSummary ? [{ label: "平均信号衰减", value: number(lossSummary.mean, 2), unit: "dB", explanation: "信号经过水中环境后的衰减。同样的发送条件下，数值越大，接收信号通常越弱。" }] : []),
          { label: "信号比背景噪声强多少？", value: snrSummary ? number(snrSummary.mean, 2) : unavailableSnr, unit: snrSummary ? "dB" : undefined,
            explanation: mode === "B" ? "由接收波形记录的信号功率与本次生成噪声功率计算；只对可计算的接收帧取平均，不用抽象模型代替。" : "接收信号与噪声的平均强弱差，数值越大通常越容易辨认，但不单独决定能否解码。" },
        ]} />
        {trends.delay.length > 0 && <ObservedChart title="信号传播用时" unit="s" points={trends.delay} />}</div>
        <p className="analysis-note">每个点对应一次接收节点的传播记录；统计也包括不是本次发送目标的节点，不把不同链路连成一条变化曲线。</p>
        {mode === "B" && <p className="analysis-note">波形信噪比：{stats.waveformSnr.length} 次可计算，{stats.waveformNoNoise} 次未加噪声，{stats.waveformNoSignal} 次无信号功率，{stats.waveformPowerMissing} 次未保存功率；未加噪声和缺失记录均不按 0 dB 计入均值。</p>}
        <details className="analysis-diagnostic"><summary>查看传播记录、误码模型与详细统计</summary><div className="analysis-inline-stats"><span>存在传播路径的接收节点记录 <b>{p.channel_signal_count}</b></span><span>没有传播路径的记录 <b>{p.channel_no_arrival_count}</b></span></div>
        <p className="analysis-note">“没有传播路径”表示声波未能有效到达，不等于“声波到了但数据没有解出来”。两种情况分开记录。</p>
        <SampleTable rows={[["首条路径传播时延", "s", stats.delay], ["传播损失", "dB", stats.loss], ["多径数量", "条", stats.paths], ["接收信噪比", "dB", observedSnr], ...(mode === "A" ? [["理论误码率（BPSK 白噪声模型）", "无量纲", stats.modeledBer] as [string, string, readonly number[]]] : [])]} />
        <div className="analysis-charts"><ObservedChart title="传播损失" unit="dB" points={trends.loss} />{mode === "A" && modeled.length > 0 && <ObservedChart title="理论误码率" unit="BER" points={modeled} />}</div>
        {mode === "A" && <p className="analysis-note">模式 A 使用抽象接收模型；理论误码率不是逐比特波形解调结果，因此不显示波形帧 CRC 或实际误码数。</p>}
        <details><summary>逐链路观测记录</summary><div className="analysis-scroll"><table><thead><tr><th>仿真时间 / s</th><th>发送序号</th><th>发送 → 接收节点</th><th>传播时延 / s</th><th>传播损失 / dB</th><th>多径 / 条</th></tr></thead><tbody>{stats.events.map(event => {
          if (event.trace.kind !== "ChannelOutcome" || event.trace.payload.outcome.type !== "Signal") return null;
          const item = event.trace.payload, outcome = event.trace.payload.outcome, tx = stats.transmissions.get(item.transmission_id);
          return <tr key={event.sequence}><td>{number(Number(BigInt(event.trace.occurred_at_ns)) / 1e9)}</td><td>{item.transmission_id}</td><td>{tx ? `N${tx.payload.sender_node_id}` : "未载入发送记录"} → N{item.receiver_node_id}</td><td>{number(Number(BigInt(outcome.first_arrival_delay_ns)) / 1e9)}</td><td>{number(outcome.aggregate_transmission_loss_db, 2)}</td><td>{outcome.path_count}</td></tr>;
        })}</tbody></table></div><p className="analysis-note">来源为信道查询结果，不将查询时间称为信号到达时间；含非目标接收节点。</p></details></details>
      </section>}
      {(mode === "B" || mode === "C") && <section id="analysis-waveform" className="analysis-section"><h3><span aria-hidden="true">04</span>波形通信</h3>{waveform.frames ? <><ExplainedValues rows={[
        { label: "解出的数据中有多少错误比特？", value: String(waveform.errors), unit: "bit", explanation: `把收到的 0、1 和发送内容逐位比较；这次共比较 ${waveform.bits} bit。` },
        { label: "错误比特占比", value: waveform.ber === null ? "无法计算" : percent(waveform.ber), explanation: "错误比特数除以比较比特总数。它描述数据位的错误，不是消息丢失率。" },
        { label: "数据完整性检查", value: `${waveform.crcPassed} 次通过 / ${waveform.crcFailed} 次未通过`, explanation: "使用随数据携带的校验码（CRC）检查是否损坏。通过只表示校验未检出问题，不表示已交给业务程序。" },
      ]} /><p className="analysis-note">这里统计所有接收节点，包括旁听。因此校验通过次数与上面的“数据送达”次数可能不同。</p>
      <details className="analysis-diagnostic"><summary>查看逐次接收的数据校验记录</summary><Facts rows={[
        ["接收帧记录", waveform.frames, "每次发送 × 接收节点，含旁听"], ["实际误码数 / 比较比特数", `${waveform.errors} / ${waveform.bits}`, "各接收帧相加"], ["加权实际误码率 BER", waveform.ber === null ? "—" : berNumber(waveform.ber), "Σ 误码数 / Σ 比较比特数"], ["帧 CRC 通过 / 失败", `${waveform.crcPassed} / ${waveform.crcFailed}`, "帧完整性检查，不等于上层交付"], ["帧 CRC 通过比例", percent(waveform.crcPassed / waveform.frames), "已记录接收帧"],
      ]} /><h4>逐帧误码与校验</h4><p className="analysis-note">一帧是一次接收的数据块；同一次发送在两个节点被接收，会留下两行记录。</p><div className="analysis-scroll"><table><thead><tr><th>发送 / 接收节点</th><th>实际误码数 / 比较比特数（bit）</th><th>实际 BER</th><th>帧 CRC</th></tr></thead><tbody>{result.visualization?.waveform.map((item, index) => <tr key={`${item.transmission_id}-${item.receiver_node_id}-${index}`}><td>{item.transmission_id} / N{item.receiver_node_id}</td><td>{item.bit_error_count} / {item.payload_bit_count}</td><td>{BigInt(item.payload_bit_count) ? berNumber(Number(item.bit_error_count) / Number(item.payload_bit_count)) : "—"}</td><td><span className={item.crc_ok ? "crc-intact" : "crc-damaged"}>{item.crc_ok ? "通过" : "失败"}</span></td></tr>)}</tbody></table></div></details></> : <p className="analysis-note">本次选择模式 {mode}，但结果未保存波形接收帧；没有记录不表示误码为零。</p>}<p className="analysis-note">这些数值来自接收波形解调与发送比特比较，不是硬件实测。</p></section>}
      <section id="analysis-fusion" className="analysis-section"><h3><span aria-hidden="true">{(mode === "B" || mode === "C") ? "05" : "04"}</span>探测与融合</h3>{communicationOnly ? <p className="analysis-note">本实验未启用融合业务。网络、声学与适用模式的波形统计正常展示，无融合不代表运行失败。</p> : <>
        <div className="analysis-process"><span>产生特征报告 <b>{result.visualization?.generated_feature_count ?? "—"}</b></span><i>→</i><span>网络交付后筛选有效观测</span><i>→</i><span>形成融合结果 <b>{result.fusion_results.length}</b> 轮</span></div>
        {fused ? <div className="analysis-fusion-layout"><div className="analysis-fusion-list">{result.fusion_results.map(item => <button key={item.fusion_sequence} type="button" aria-pressed={item.fusion_sequence === fused.fusion_sequence} onClick={() => selectFusion(item.fusion_sequence)}><strong>第 {item.fusion_sequence} 轮结果</strong><span title={formatNanoseconds(item.completed_at_ns)}>{seconds(item.completed_at_ns)} · {item.observation_count} 条观测</span></button>)}</div><div><ExplainedValues rows={[
          { label: "用了多少条有效观测？", value: fused.observation_count, unit: "条", explanation: "这一轮实际采用的方位观测。同一个节点可以贡献多条观测，因此它不等于参与节点数。" },
          { label: "观测来自哪些节点？", value: distinctSources?.length ? distinctSources.map(node => `N${node}`).join("、") : "尚无来源记录", explanation: distinctSources?.length ? `共有 ${distinctSources.length} 个不同节点。这些来源对应实际被采用的观测。` : "结果未提供来源明细，不根据场景中的节点数推算。" },
          { label: "这一轮得到结果用了多久？", value: seconds(fused.fusion_period_ns), explanation: "从首条观测采样到本轮融合完成，包括网络传输与等待；不是算法单独计算的耗时。" },
          { label: "估计目标在哪里？", value: `(${number(fused.estimated_target_x_meters)}, ${number(fused.estimated_target_y_meters)})`, unit: "m", explanation: "本轮估计的目标平面位置（x，y），采用场景的局部坐标；这是估计值，不是真实目标位置。" },
        ]} /><details className="analysis-diagnostic"><summary>查看本轮观测来源与时间记录</summary><p className="analysis-note">观测窗口开始：{fused.started_at_ns} ns；完成：{fused.completed_at_ns} ns；耗时：{fused.fusion_period_ns} ns。</p>{sources?.length ? <p className="analysis-note">采用的观测身份：{sources.map(source => `N${source.node_id} / #${source.observation_sequence}`).join("，")}</p> : null}<p className="analysis-note">逐条方位值、采样时间和无效观测未保存在当前结果中，不从发送端报告补造。</p></details></div></div> : <p className="analysis-note">尚未形成融合结果；有效交付数量或观测几何可能不足，不能据此推断具体失败原因。</p>}
      </>}</section>
    </div>
    <section id="analysis-reference" className="analysis-section analysis-reference"><h3>参考要求对照</h3><p className="analysis-note">参考项目验收基线，供实验人员核对；普通实验不会因此被改成验收案例，页面不生成自动结论。</p>
      <div className="analysis-reference-table"><table aria-label="参考要求与本次记录"><thead><tr><th>对照内容</th><th>参考要求</th><th>本次记录与含义</th></tr></thead><tbody>
        <tr><th scope="row">多节点协同</th><td>{acceptanceReference.network_node_count_minimum}–{acceptanceReference.network_node_count_maximum} 个节点</td><td>{p.node_count} 个节点<small>来自本次运行结果，不代表每个节点都已成功交付数据。</small></td></tr>
        <tr><th scope="row">节点发送速率</th><td>{acceptanceReference.communication_rate_bits_per_second} bit/s</td><td>{experiment ? `${experiment.phy.bit_rate_bits_per_second} bit/s` : "本次配置快照未提供"}<small>来自本次运行冻结的实验配置，不用接收数量或吞吐量替代。</small></td></tr>
        <tr><th scope="row">误码率</th><td>BER ≤ {acceptanceReference.maximum_bit_error_rate.toExponential()}</td><td>{referenceBer.groups.length ? referenceBer.groups.map(group => <div key={group.source}>{berSourceLabel[group.source]}：最大 <b>{berNumber(group.maximum)}</b><small>{group.count} 个已记录目标接收；不含旁听。</small></div>) : "尚无可核对的目标接收误码记录"}{!referenceBer.complete && <small>目标接收证据尚不完整，已有样本不能代表整次运行。</small>}</td></tr>
        <tr><th scope="row">特征级融合</th><td>网络传输后融合特征</td><td>{communicationOnly ? "本实验未启用融合业务" : result.fusion_results.length ? `已形成 ${result.fusion_results.length} 轮结果` : "尚未形成融合结果"}<small>输入是成功送达融合中心的方位与位置特征，不是直接把各节点的原始波形合并。</small></td></tr>
        <tr><th scope="row">有效方位观测</th><td>每轮不少于 {acceptanceReference.minimum_bearing_points} 点</td><td>{result.fusion_results.length ? <div className="analysis-reference-rounds">{result.fusion_results.map(item => <span key={item.fusion_sequence}>第 {item.fusion_sequence} 轮：{item.observation_count} 条</span>)}</div> : communicationOnly ? "本实验未启用" : "尚无已完成窗口"}<small>“点”按节点编号与观测序号去重；同一节点不同时刻可以贡献多点，不要求五个不同节点。</small></td></tr>
        <tr><th scope="row">完整探测周期</th><td>≤ {String(BigInt(acceptanceReference.maximum_fusion_period_ns) / 60_000_000_000n)} min（{seconds(acceptanceReference.maximum_fusion_period_ns)}）</td><td>{communicationOnly ? "本实验未启用融合业务" : "尚未单独保存完整探测周期"}{result.fusion_results.length > 0 && <small>已记录的是融合窗口耗时：{result.fusion_results.map(item => `第 ${item.fusion_sequence} 轮 ${seconds(item.fusion_period_ns)}`).join("；")}。</small>}</td></tr>
      </tbody></table></div>
      <p className="analysis-note">融合窗口耗时从本轮最早的有效观测采样时刻算起，到通信周期结束时产生融合结果为止，包含传输和等待；不是算法计算耗时。当前没有独立的完整探测周期记录，不能把上述时间或相邻结果的时间差冒充探测周期。</p>
    </section>
    <details className="analysis-methods"><summary>统计口径与当前数据覆盖</summary><p>业务交付率、业务丢包率、端到端时延和有效吞吐量需要业务包产生总数、产生时间、有效字节及最终去重交付；当前记录不齐全，因此暂不计算。这是业务统计缺口，与选择模式 A 或 B 无关。</p><p>未发送的排队数据不会出现在发送事件中。重传与多跳转发也不能仅凭重复数据包编号区分；不从未解码次数推导碰撞。最大多径时延扩展、完整波形样本目前未随结果提供。</p><p>各类信道与接收统计覆盖已保存的真实接收节点，包括旁听。只有累计计数绘制阶梯线；来自不同链路的观测使用散点，不冒充单条链路的连续变化。模式 C 回放实测信道估计；不将仿真解调统计称为海试实测误码。</p>
      <p>参考要求读取版本化项目基线（Acceptance4Node v1）。误码对照按每次单播的目标接收分组，展示各证据来源的最大值，不把模型估计、仿真逐比特结果或实测记录混为同一种证据；没有路径或没有解码证据时不补零。</p>
    </details>
    <details className="analysis-methods"><summary>本次实验的资源版本</summary><Facts rows={[["实验", result.experiment_id, `版本 ${result.experiment_version}`], ["场景", result.scenario_id, `版本 ${result.scenario_version}`], ["环境", result.environment_asset_id, `格式版本 ${result.environment_format_version}`], ["最终状态快照", p.final_snapshot_version], ["运行编号", result.run_id], ["仿真时长原始值", p.simulation_duration_ns, "ns"]]} /></details>
  </article>;
}
