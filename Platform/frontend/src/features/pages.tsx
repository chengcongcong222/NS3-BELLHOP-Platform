import { useEffect, useRef, useState } from "react";
import { useMutation, useQuery, useQueryClient } from "@tanstack/react-query";
import { Link, useNavigate, useParams } from "react-router-dom";
import { apiClient, ApiFailure } from "../api/client";
import { queries } from "../api/queries";
import {
  emptyProjection,
  RunEventProjection,
  type RunProjectionState,
} from "../api/runEvents";
import type { AcceptanceReportDto, ExperimentDto, ResultDto, RunEventDto } from "../api/types";
import {
  EmptyState,
  ErrorState,
  Field,
  LoadingState,
  MetricCard,
  PageHeader,
  Status,
} from "../components/common";
import { ScenarioTopology } from "../components/ScenarioTopology";
import { formatBer, formatFrequency, formatNanoseconds } from "../domain/format";
import { PRODUCT_METADATA } from "../productMetadata";

function useRequiredParams(...names: string[]): string[] | null {
  const params = useParams();
  const values = names.map((name) => params[name]);
  return values.every((value): value is string => Boolean(value))
    ? (values as string[])
    : null;
}

function QueryState({ query }: { query: { isPending: boolean; error: unknown } }) {
  if (query.isPending) return <LoadingState />;
  if (query.error) return <ErrorState error={query.error} />;
  return null;
}

export function OverviewPage() {
  const environments = useQuery(queries.environments());
  const scenarios = useQuery(queries.scenarios());
  const experiments = useQuery(queries.experiments());
  const runs = useQuery(queries.runs());
  const results = useQuery(queries.results());
  const queriesList = [environments, scenarios, experiments, runs, results];
  const error = queriesList.find((query) => query.error)?.error;
  if (queriesList.some((query) => query.isPending)) return <LoadingState />;
  if (error) return <ErrorState error={error} />;
  const runItems = runs.data ?? [];
  const recentRun = runItems.at(-1);
  const recentResult = results.data?.at(-1);
  return (
    <>
      <PageHeader title="平台概览" detail="来自后端按 Run 创建顺序发布的实时只读摘要，无浏览器伪造指标。" />
      <section className="metric-grid">
        <MetricCard label="环境" value={environments.data?.length ?? 0} />
        <MetricCard label="场景版本" value={scenarios.data?.length ?? 0} />
        <MetricCard label="实验" value={experiments.data?.length ?? 0} />
        <MetricCard label="运行" value={runItems.length} />
        <MetricCard label="Completed" value={runItems.filter((item) => item.lifecycle === "Completed").length} />
        <MetricCard label="Failed" value={runItems.filter((item) => item.lifecycle === "Failed").length} />
      </section>
      <section className="two-column">
        <article className="panel">
          <h2>最近一次 Run</h2>
          {recentRun ? <Link to={`/runs/${recentRun.run_id}`}>#{recentRun.catalog_sequence} · {recentRun.run_id} · {recentRun.lifecycle}</Link> : <p>暂无 Run</p>}
        </article>
        <article className="panel">
          <h2>最近正式 Result</h2>
          {recentResult ? <Link to={`/results/${recentResult.run_id}`}>#{recentResult.catalog_sequence} · {recentResult.run_id} · {recentResult.acceptance_overall ?? "NotFullyEvaluated"}</Link> : <p>暂无 Result</p>}
        </article>
      </section>
    </>
  );
}

export function EnvironmentCatalogPage() {
  const query = useQuery(queries.environments());
  const state = <QueryState query={query} />;
  if (query.isPending || query.error) return state;
  return (
    <>
      <PageHeader title="Environment Catalog" detail="已验证声学资产、覆盖与 provenance。" />
      {!query.data.length ? <EmptyState>暂无环境资产</EmptyState> : (
        <div className="card-grid">{query.data.map((item) => (
          <article className="panel" key={item.environment_asset_id}>
            <div className="panel-title"><Link to={`/environments/${item.environment_asset_id}`}>{item.environment_asset_id}</Link><Status value={item.validation_state} /></div>
            <dl><Field label="坐标轴">{item.coordinate_frame.vertical_axis}</Field><Field label="网格 cells">{item.cell_count}</Field><Field label="无到达">{item.no_arrival_cell_count}</Field><Field label="Producer">{item.provenance.producer}</Field></dl>
          </article>
        ))}</div>
      )}
    </>
  );
}

export function EnvironmentDetailPage() {
  const params = useRequiredParams("assetId");
  const query = useQuery({ ...queries.environment(params?.[0] ?? ""), enabled: Boolean(params) });
  if (!params) return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Environment route is malformed.")} />;
  if (query.isPending || query.error) return <QueryState query={query} />;
  const item = query.data;
  return (
    <>
      <PageHeader title={item.environment_asset_id} detail="Environment 只读详情；路径和 package identity 不属于 HTTP DTO。" />
      <section className="two-column">
        <article className="panel"><h2>资产</h2><dl><Field label="格式">{item.format}</Field><Field label="Package version">{item.package_format_version}</Field><Field label="Asset version">{item.asset_format_version}</Field><Field label="Checksum">{item.checksum.algorithm} · {item.checksum.value}</Field><Field label="Payload bytes">{item.payload_bytes}</Field></dl></article>
        <article className="panel"><h2>Provenance</h2><dl><Field label="Producer">{item.provenance.producer}</Field><Field label="Build">{item.provenance.created_by_build_version}</Field><Field label="Source">{item.provenance.source_description || "—"}</Field><Field label="Normalization">{item.provenance.normalization_policy_version || "—"}</Field></dl></article>
      </section>
      <section className="panel"><h2>Axes / Coverage</h2><table><thead><tr><th>Axis</th><th>Count</th><th>Minimum</th><th>Maximum</th><th>Unit</th></tr></thead><tbody>{Object.entries(item.axes).map(([name, axis]) => <tr key={name}><td>{name}</td><td>{axis.count}</td><td>{axis.minimum}</td><td>{axis.maximum}</td><td>{axis.unit}</td></tr>)}</tbody></table></section>
    </>
  );
}

function latestVersions<T extends { version: string }>(items: T[]): string {
  return items.reduce((latest, item) => BigInt(item.version) > BigInt(latest) ? item.version : latest, items[0]?.version ?? "0");
}

export function ScenarioCatalogPage() {
  const query = useQuery(queries.scenarios());
  if (query.isPending || query.error) return <QueryState query={query} />;
  const groups = new Map<string, typeof query.data>();
  for (const scenario of query.data) {
    groups.set(scenario.scenario_id, [...(groups.get(scenario.scenario_id) ?? []), scenario]);
  }
  return <><PageHeader title="Scenario Catalog" detail="发布版本不可变；本阶段只读。" /><div className="card-grid">{[...groups.entries()].map(([id, versions]) => <article className="panel" key={id}><div className="panel-title"><strong>{id}</strong><span>latest v{latestVersions(versions)}</span></div>{versions.map((item) => <div className="version-row" key={item.version}><Link to={`/scenarios/${id}/versions/${item.version}`}>v{item.version} · {item.name}</Link><span>{item.nodes.length} nodes · {item.environment.environment_asset_id}</span></div>)}</article>)}</div></>;
}

export function ScenarioDetailPage() {
  const params = useRequiredParams("scenarioId", "version");
  const query = useQuery({ ...queries.scenario(params?.[0] ?? "", params?.[1] ?? ""), enabled: Boolean(params) });
  if (!params) return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Scenario route is malformed.")} />;
  if (query.isPending || query.error) return <QueryState query={query} />;
  const item = query.data;
  return <><PageHeader title={`${item.name} · v${item.version}`} detail="节点能力、初始几何和环境绑定的只读版本。" /><section className="panel"><dl className="inline-fields"><Field label="ScenarioId">{item.scenario_id}</Field><Field label="Environment"><Link to={`/environments/${item.environment.environment_asset_id}`}>{item.environment.environment_asset_id}</Link> · v{item.environment.asset_format_version}</Field><Field label="Mobility">{item.mobility.model}</Field><Field label="Fusion center">Node {item.fusion_center_node_id}</Field></dl></section><section className="panel"><h2>Initial topology</h2><ScenarioTopology scenario={item} /></section><section className="panel"><h2>Nodes</h2><table><thead><tr><th>ID / role</th><th>Motion</th><th>TX / RX</th><th>Duplex</th><th>Initial position (m)</th><th>Initial velocity (m/s)</th></tr></thead><tbody>{item.nodes.map((node) => { const velocity = node.initial_velocity; const moving = velocity.x_meters_per_second !== 0 || velocity.y_meters_per_second !== 0 || velocity.z_meters_per_second !== 0; return <tr key={node.node_id}><td>{node.node_id}{node.node_id === item.fusion_center_node_id ? " · Fusion center" : " · Participant"}</td><td>{moving ? "Moving" : "Fixed"}</td><td>{node.can_transmit ? "TX" : "—"} / {node.can_receive ? "RX" : "—"}</td><td>{node.duplex_mode}</td><td>{node.initial_position.x_meters}, {node.initial_position.y_meters}, {node.initial_position.z_meters}</td><td>{velocity.x_meters_per_second}, {velocity.y_meters_per_second}, {velocity.z_meters_per_second}</td></tr>; })}</tbody></table></section></>;
}

export function ExperimentCatalogPage() {
  const query = useQuery(queries.experiments());
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Experiment Catalog" detail="配置版本绑定精确 Scenario，并可创建真实 Run。" /><div className="card-grid">{query.data.map((item) => <article className="panel" key={`${item.experiment_id}:${item.version}`}><div className="panel-title"><Link to={`/experiments/${item.experiment_id}/versions/${item.version}`}>{item.name}</Link><span>v{item.version}</span></div><dl><Field label="Profile">{item.fusion.acceptance_profile === "Acceptance4Node" ? "Acceptance4Node · 第三方验收基准" : "Extended6Node · 扩展示例（非第三方验收）"}</Field><Field label="Scenario">{item.scenario.scenario_id} · v{item.scenario.version}</Field><Field label="Routing / MAC">{item.routing.mode} / {item.mac.mode}</Field><Field label="PHY">{item.phy.bit_rate_bits_per_second} bit/s · {item.phy.rx_quality_mode}</Field><Field label="Cycles">{item.simulation_cycle_count}</Field></dl></article>)}</div></>;
}

function RunExperimentButton({ experiment }: { experiment: ExperimentDto }) {
  const navigate = useNavigate();
  const queryClient = useQueryClient();
  const mutation = useMutation({
    mutationFn: () => apiClient.createRun(experiment.experiment_id, experiment.version),
    onSuccess: async (run) => {
      await queryClient.invalidateQueries({ queryKey: ["runs"] });
      navigate(`/runs/${run.run_id}`);
    },
  });
  return <div><button type="button" disabled={mutation.isPending} onClick={() => mutation.mutate()}>{mutation.isPending ? "正在创建…" : "Run this experiment"}</button>{mutation.error && <ErrorState error={mutation.error} />}</div>;
}

export function ExperimentDetailPage() {
  const params = useRequiredParams("experimentId", "version");
  const query = useQuery({ ...queries.experiment(params?.[0] ?? "", params?.[1] ?? ""), enabled: Boolean(params) });
  if (!params) return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Experiment route is malformed.")} />;
  if (query.isPending || query.error) return <QueryState query={query} />;
  const item = query.data;
  const profile = item.fusion.acceptance_profile === "Acceptance4Node" ? "Acceptance4Node · 第三方验收基准" : "Extended6Node · 扩展示例（非第三方验收）";
  return <><PageHeader title={`${item.name} · v${item.version}`} detail="完整 backend DTO，只读；Run 仅提交 Experiment identity/version。" /><section className="panel action-panel"><div><strong>{profile}</strong><p>Experiment → Start Run → Monitor → Result → Acceptance</p></div><RunExperimentButton experiment={item} /></section><section className="two-column"><article className="panel"><h2>Protocol</h2><dl><Field label="Scenario"><Link to={`/scenarios/${item.scenario.scenario_id}/versions/${item.scenario.version}`}>{item.scenario.scenario_id} · v{item.scenario.version}</Link></Field><Field label="Routing">{item.routing.mode}</Field><Field label="MAC">{item.mac.mode}</Field><Field label="Slot / Guard">{formatNanoseconds(item.mac.slot_duration_ns)} / {formatNanoseconds(item.mac.guard_interval_ns)}</Field><Field label="Fusion period limit">{formatNanoseconds(item.fusion.maximum_fusion_period_ns)}</Field></dl></article><article className="panel"><h2>PHY / Execution</h2><dl><Field label="Rate">{item.phy.bit_rate_bits_per_second} bit/s</Field><Field label="Center / Bandwidth">{formatFrequency(item.phy.center_frequency_hz)} / {formatFrequency(item.phy.occupied_bandwidth_hz)}</Field><Field label="Source / noise">{item.phy.source_level_db_re_1upa_at_1m} dB / {item.phy.equivalent_noise_power_db_re_1upa2} dB</Field><Field label="BER evidence">{item.phy.rx_quality_mode}</Field><Field label="Cycles / Seed">{item.simulation_cycle_count} / {item.deterministic_seed}</Field></dl></article></section></>;
}

export function RunCatalogPage() {
  const query = useQuery({ ...queries.runs(), refetchInterval: 1000 });
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Run Catalog" detail="后端 in-memory Run catalog 是权威来源。" />{!query.data.length ? <EmptyState>暂无 Run</EmptyState> : <section className="panel"><table><thead><tr><th>RunId</th><th>Experiment</th><th>Lifecycle</th><th>Result</th><th>Failure</th></tr></thead><tbody>{query.data.map((item) => <tr key={item.run_id}><td><Link to={`/runs/${item.run_id}`}>{item.run_id}</Link></td><td>{item.experiment_id} · v{item.experiment_version}</td><td><Status value={item.lifecycle} /></td><td>{item.result_available ? <Link to={`/results/${item.run_id}`}>Available</Link> : "—"}</td><td>{item.failure ? `${item.failure.code}: ${item.failure.message}` : "—"}</td></tr>)}</tbody></table></section>}</>;
}

function eventIdentity(event: RunEventDto): string {
  const trace = event.trace;
  if (trace.kind === "CycleCommit") return `Cycle ${trace.payload.cycle_id}`;
  if (trace.kind === "Transmission") return `Tx ${trace.payload.transmission_id}`;
  if (trace.kind === "ChannelOutcome") return `Tx ${trace.payload.transmission_id} → N${trace.payload.receiver_node_id}`;
  return `Rx ${trace.payload.reception_id} · Tx ${trace.payload.transmission_id}`;
}

function eventDetail(event: RunEventDto): string {
  const trace = event.trace;
  if (trace.kind === "CycleCommit") {
    return `snapshot ${trace.payload.base_snapshot_version} → ${trace.payload.committed_snapshot_version}`;
  }
  if (trace.kind === "Transmission") {
    const target = trace.payload.target.type === "Broadcast" ? "Broadcast" : `Unicast N${trace.payload.target.node_id}`;
    return `sender N${trace.payload.sender_node_id} · ${target}`;
  }
  if (trace.kind === "ChannelOutcome") {
    if (trace.payload.outcome.type === "NoArrival") return "NoArrival";
    return `Signal · delay ${formatNanoseconds(trace.payload.outcome.first_arrival_delay_ns)} · loss ${trace.payload.outcome.aggregate_transmission_loss_db} dB · ${trace.payload.outcome.path_count} paths`;
  }
  const quality = trace.payload.quality
    ? ` · BER ${trace.payload.quality.bit_error_rate}（无量纲） · ${trace.payload.quality.source}`
    : " · no quality evidence";
  return `receiver N${trace.payload.receiver_node_id} · ${trace.payload.disposition}${quality}`;
}

export function RunMonitorPage() {
  const params = useRequiredParams("runId");
  const runId = params?.[0] ?? "";
  const query = useQuery({ ...queries.run(runId), enabled: Boolean(params), refetchInterval: (state) => ["Completed", "Failed"].includes(state.state.data?.lifecycle ?? "") ? false : 500 });
  const [projection, setProjection] = useState<RunProjectionState>(emptyProjection);
  const [streamError, setStreamError] = useState<ApiFailure | null>(null);
  const stream = useRef<RunEventProjection | null>(null);
  useEffect(() => {
    if (!params) return;
    const next = new RunEventProjection(apiClient.url(`/runs/${encodeURIComponent(runId)}/events`), setProjection, setStreamError);
    stream.current = next;
    next.connect();
    return () => { next.close(); stream.current = null; };
  }, [runId, Boolean(params)]);
  useEffect(() => { if (["Completed", "Failed"].includes(query.data?.lifecycle ?? "")) stream.current?.close(); }, [query.data?.lifecycle]);
  if (!params) return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Run route is malformed.")} />;
  if (query.isPending || query.error) return <QueryState query={query} />;
  const item = query.data;
  const transmissionSenders = new Map<string, string>();
  for (const event of projection.timeline) {
    if (event.trace.kind === "Transmission") {
      transmissionSenders.set(event.trace.payload.transmission_id, event.trace.payload.sender_node_id);
    }
  }
  const channelEvents = projection.timeline.filter(
    (event): event is RunEventDto & { trace: Extract<RunEventDto["trace"], { kind: "ChannelOutcome" }> } =>
      event.trace.kind === "ChannelOutcome",
  );
  return <><PageHeader title={`Run Monitor · ${item.run_id}`} detail="Run lifecycle 来自权威 Run resource；SSE 仅形成非因果、可重放的只读事件投影。" /><section className="metric-grid"><MetricCard label="Lifecycle" value={<Status value={item.lifecycle} />} /><MetricCard label="Latest simulation time" value={projection.latestSimulationTimeNs ? formatNanoseconds(projection.latestSimulationTimeNs) : "—"} /><MetricCard label="Event sequence" value={projection.latestSequence} /><MetricCard label="Transmission" value={projection.transmissionCount} /><MetricCard label="Channel Signal" value={projection.channelSignalCount} /><MetricCard label="NoArrival" value={projection.channelNoArrivalCount} /><MetricCard label="Reception" value={projection.receptionCount} /><MetricCard label="CycleCommit" value={projection.cycleCommitCount} /></section>{streamError && <ErrorState error={streamError} />}<section className="two-column"><article className="panel"><h2>Captured resources</h2><dl><Field label="Experiment"><Link to={`/experiments/${item.experiment_id}/versions/${item.experiment_version}`}>{item.experiment_id} · v{item.experiment_version}</Link></Field><Field label="Scenario"><Link to={`/scenarios/${item.scenario_id}/versions/${item.scenario_version}`}>{item.scenario_id} · v{item.scenario_version}</Link></Field><Field label="Environment"><Link to={`/environments/${item.environment_asset_id}`}>{item.environment_asset_id}</Link> · v{item.environment_format_version}</Field><Field label="Event stream complete">{item.event_stream_complete === null ? "Pending" : String(item.event_stream_complete)}</Field></dl></article><article className="panel"><h2>System evidence</h2><dl><Field label="Product baseline">{PRODUCT_METADATA.baseline}</Field><Field label="Simulation engine">{PRODUCT_METADATA.simulationEngineDisplay}</Field><Field label="Time authority">{PRODUCT_METADATA.simulationEngine} Simulator</Field><Field label="Scheduling">{PRODUCT_METADATA.schedulerAuthority}</Field><Field label="SSE authority">Observability only；传输错误不改变 Run lifecycle。</Field></dl></article></section>{item.failure && <ErrorState error={new ApiFailure(item.failure.code.includes("Protocol") ? "ProtocolFailure" : "RunFailed", item.failure.code, item.failure.message)} />}<section className="panel"><h2>Event timeline · RunEventSequence order</h2>{projection.timeline.length ? <table><thead><tr><th>Sequence</th><th>Simulation time</th><th>Kind</th><th>Stable identity</th><th>Evidence</th></tr></thead><tbody>{projection.timeline.map((event) => <tr key={event.sequence}><td>{event.sequence}</td><td>{formatNanoseconds(event.trace.occurred_at_ns)}</td><td>{event.trace.kind}</td><td>{eventIdentity(event)}</td><td>{eventDetail(event)}</td></tr>)}</tbody></table> : <p>尚未投影 Run events。</p>}</section><section className="panel"><h2>Channel outcomes</h2>{channelEvents.length ? <table><thead><tr><th>Sequence</th><th>Transmission</th><th>Sender</th><th>Receiver</th><th>Outcome</th><th>Evidence</th></tr></thead><tbody>{channelEvents.map((event) => { const payload = event.trace.payload; return <tr key={event.sequence}><td>{event.sequence}</td><td>{payload.transmission_id}</td><td>{transmissionSenders.get(payload.transmission_id) ? `N${transmissionSenders.get(payload.transmission_id)}` : "Not present in current SSE projection"}</td><td>N{payload.receiver_node_id}</td><td><Status value={payload.outcome.type} /></td><td>{payload.outcome.type === "Signal" ? `${payload.outcome.aggregate_transmission_loss_db} dB · ${payload.outcome.path_count} paths` : "Formal NoArrival trace"}</td></tr>; })}</tbody></table> : <p>尚无 ChannelOutcome trace。</p>}</section>{item.lifecycle === "Completed" && <Link className="button-link" to={`/results/${item.run_id}`}>查看正式 Result 与 Acceptance</Link>}</>;
}

export function ResultCatalogPage() {
  const query = useQuery(queries.results());
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Result Catalog" detail="仅包含已原子发布 formal Result 的 Completed Run。" />{!query.data.length ? <EmptyState>暂无正式 Result</EmptyState> : <section className="panel"><table><thead><tr><th>Order</th><th>RunId</th><th>Acceptance</th><th>Duration</th><th>Fusion results</th></tr></thead><tbody>{query.data.map((item) => <tr key={item.run_id}><td>#{item.catalog_sequence}</td><td><Link to={`/results/${item.run_id}`}>{item.run_id}</Link></td><td><Status value={item.acceptance_overall ?? "NotFullyEvaluated"} /></td><td>{formatNanoseconds(item.simulation_duration_ns)}</td><td>{item.fusion_result_count}</td></tr>)}</tbody></table></section>}</>;
}

function AcceptanceEvidence({ report, result, experiment }: {
  report: AcceptanceReportDto;
  result: ResultDto;
  experiment: ExperimentDto;
}) {
  const source = experiment.phy.rx_quality_mode === "ModeledBpskAwgn"
    ? "Modeled · 仿真模型 BER（BPSK/AWGN），不是硬件实测"
    : "NotEvaluated · Experiment 未配置 BER quality model";
  const rows = [
    {
      metric: "Network nodes",
      requirement: "4 nodes（Acceptance4Node）",
      actual: `${result.projection.node_count} nodes`,
      verdict: report.network_node_count,
      evidence: "Formal Result projection.node_count",
      reason: "Backend AcceptanceReport verdict",
    },
    {
      metric: "Communication rate",
      requirement: "60 bit/s（Acceptance4Node）",
      actual: `${experiment.phy.bit_rate_bits_per_second} bit/s`,
      verdict: report.communication_rate,
      evidence: "Captured Experiment PHY configuration",
      reason: "Backend AcceptanceReport verdict",
    },
    {
      metric: "BER",
      requirement: `≤ ${report.required_maximum_ber}（无量纲）`,
      actual: report.maximum_ber === null ? "NotEvaluated" : `max ${formatBer(report.maximum_ber)} · mean ${formatBer(report.mean_ber)}`,
      verdict: report.bit_error_rate,
      evidence: `${source}; evaluated ${report.evaluated_target_receptions}, missing ${report.missing_ber_evidence_count}`,
      reason: report.ber_reason || "Backend AcceptanceReport verdict",
    },
    {
      metric: "Feature-level fusion",
      requirement: "Feature-level fusion workload",
      actual: `${experiment.fusion.workload}; ${result.fusion_results.length} formal FusionResult records`,
      verdict: report.feature_level_fusion,
      evidence: "Captured Experiment + formal Result",
      reason: "Backend AcceptanceReport verdict",
    },
    {
      metric: "Bearing points",
      requirement: `≥ ${report.required_minimum_bearing_points} points`,
      actual: report.minimum_bearing_points === null ? "NotEvaluated" : `${report.minimum_bearing_points} points`,
      verdict: report.bearing_point_count,
      evidence: "Backend AcceptanceReport minimum_bearing_points",
      reason: "Backend AcceptanceReport verdict",
    },
    {
      metric: "Fusion period",
      requirement: `≤ ${formatNanoseconds(report.required_maximum_fusion_period_ns)}`,
      actual: report.maximum_fusion_period_ns === null ? "NotEvaluated" : formatNanoseconds(report.maximum_fusion_period_ns),
      verdict: report.fusion_period,
      evidence: "Backend AcceptanceReport maximum_fusion_period_ns",
      reason: "Backend AcceptanceReport verdict",
    },
    {
      metric: "Overall",
      requirement: "Backend third-party acceptance aggregation",
      actual: "Formal AcceptanceReport",
      verdict: report.overall,
      evidence: "Backend acceptance_report.overall",
      reason: "Frontend does not recompute this verdict",
    },
  ];
  return <table><thead><tr><th>Metric</th><th>Requirement</th><th>Actual</th><th>Verdict</th><th>Evidence / source</th><th>Reason</th></tr></thead><tbody>{rows.map((row) => <tr key={row.metric}><td>{row.metric}</td><td>{row.requirement}</td><td>{row.actual}</td><td><Status value={row.verdict} /></td><td>{row.evidence}</td><td>{row.reason}</td></tr>)}</tbody></table>;
}

function FusionEstimatePlot({ result }: { result: ResultDto }) {
  if (!result.fusion_results.length) return <p>无 FusionResult records；不补画 observation 或 target truth。</p>;
  const xs = result.fusion_results.map((item) => item.estimated_target_x_meters);
  const ys = result.fusion_results.map((item) => item.estimated_target_y_meters);
  const minimumX = Math.min(...xs);
  const maximumX = Math.max(...xs);
  const minimumY = Math.min(...ys);
  const maximumY = Math.max(...ys);
  const spanX = Math.max(maximumX - minimumX, 1);
  const spanY = Math.max(maximumY - minimumY, 1);
  return <svg className="fusion-plot" viewBox="0 0 600 260" role="img" aria-label="Fusion estimate coordinates"><rect x="1" y="1" width="598" height="258" rx="4" className="plot-frame" />{result.fusion_results.map((item) => { const x = 40 + ((item.estimated_target_x_meters - minimumX) / spanX) * 520; const y = 220 - ((item.estimated_target_y_meters - minimumY) / spanY) * 180; return <g key={item.fusion_sequence} transform={`translate(${x} ${y})`}><circle r="6" className="fusion-estimate" /><text x="10" y="4" className="node-label">F{item.fusion_sequence}</text></g>; })}<text x="520" y="246" className="plot-label">x (m)</text><text x="12" y="22" className="plot-label">y (m)</text></svg>;
}

export function ResultDetailPage() {
  const params = useRequiredParams("runId");
  const runId = params?.[0] ?? "";
  const result = useQuery({ ...queries.result(runId), enabled: Boolean(params) });
  const experiment = useQuery({ ...queries.experiment(result.data?.experiment_id ?? "", result.data?.experiment_version ?? ""), enabled: Boolean(result.data) });
  if (!params) return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Result route is malformed.")} />;
  if (result.isPending || result.error) return <QueryState query={result} />;
  if (experiment.isPending || experiment.error) return <QueryState query={experiment} />;
  const item = result.data;
  const report = item.acceptance_report;
  const acceptance4 = experiment.data.fusion.acceptance_profile === "Acceptance4Node";
  return <><PageHeader title={`Result · ${item.run_id}`} detail="Formal Result、验收证据与资源 provenance 的只读视图。" /><section className="profile-banner"><strong>{acceptance4 ? "Acceptance4Node · 第三方验收基准" : "Extended6Node · 扩展示例（非第三方验收）"}</strong><span>verdict 完全来自 backend AcceptanceReport，前端不重算。</span></section><section className="metric-grid"><MetricCard label="Overall" value={<Status value={report?.overall ?? "NotFullyEvaluated"} />} /><MetricCard label="Nodes" value={`${item.projection.node_count} nodes`} /><MetricCard label="Communication rate" value={`${experiment.data.phy.bit_rate_bits_per_second} bit/s`} /><MetricCard label="Duration" value={formatNanoseconds(item.projection.simulation_duration_ns)} /><MetricCard label="Bearing points" value={report?.minimum_bearing_points ?? "NotEvaluated"} /><MetricCard label="Fusion period" value={report?.maximum_fusion_period_ns ? formatNanoseconds(report.maximum_fusion_period_ns) : "NotEvaluated"} /></section><section className="panel"><h2>Acceptance evidence</h2>{acceptance4 && report ? <AcceptanceEvidence report={report} result={item} experiment={experiment.data} /> : <div className="state-panel">{acceptance4 ? "Backend 未发布 AcceptanceReport；不得推导 verdict。" : "Extended6Node 仅为扩展示例，不混入 Acceptance4Node 第三方验收要求。"}</div>}</section><section className="two-column"><article className="panel"><h2>Formal projection</h2><dl><Field label="Cycle commits">{item.projection.cycle_count}</Field><Field label="Transmissions">{item.projection.transmission_count}</Field><Field label="Channel Signal">{item.projection.channel_signal_count}</Field><Field label="NoArrival">{item.projection.channel_no_arrival_count}</Field><Field label="Receptions">{item.projection.reception_count}</Field><Field label="Local delivery">{item.projection.local_delivery_count}</Field></dl><p className="data-boundary">Result DTO 未提供 unsupported reception aggregate；本页不从其他计数推导。</p></article><article className="panel"><h2>Traceability</h2><dl><Field label="Run"><Link to={`/runs/${item.run_id}`}>{item.run_id}</Link></Field><Field label="Experiment"><Link to={`/experiments/${item.experiment_id}/versions/${item.experiment_version}`}>{item.experiment_id} · v{item.experiment_version}</Link></Field><Field label="Scenario"><Link to={`/scenarios/${item.scenario_id}/versions/${item.scenario_version}`}>{item.scenario_id} · v{item.scenario_version}</Link></Field><Field label="Environment"><Link to={`/environments/${item.environment_asset_id}`}>{item.environment_asset_id}</Link> · v{item.environment_format_version}</Field></dl></article></section><section className="panel"><h2>Fusion estimate coordinates</h2><FusionEstimatePlot result={item} />{item.fusion_results.length > 0 && <table><thead><tr><th>Sequence</th><th>Start</th><th>Complete</th><th>Period</th><th>Observations</th><th>Estimate (m)</th></tr></thead><tbody>{item.fusion_results.map((fusion) => <tr key={fusion.fusion_sequence}><td>{fusion.fusion_sequence}</td><td>{formatNanoseconds(fusion.started_at_ns)}</td><td>{formatNanoseconds(fusion.completed_at_ns)}</td><td>{formatNanoseconds(fusion.fusion_period_ns)}</td><td>{fusion.observation_count}</td><td>{fusion.estimated_target_x_meters}, {fusion.estimated_target_y_meters}</td></tr>)}</tbody></table>}</section><section className="panel"><h2>System evidence</h2><dl><Field label="Product baseline">{PRODUCT_METADATA.baseline}</Field><Field label="Simulation engine">{PRODUCT_METADATA.simulationEngineDisplay}</Field><Field label="Scheduler authority">{PRODUCT_METADATA.schedulerAuthority}</Field><Field label="BER terminology">Modeled 表示仿真模型证据；只有正式 DTO 提供时才可标为 Measured / External。</Field></dl></section></>;
}

export function NotFoundPage() {
  return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Frontend route was not found.")} />;
}
