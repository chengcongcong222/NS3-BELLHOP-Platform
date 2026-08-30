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
import type { ExperimentDto } from "../api/types";
import {
  EmptyState,
  ErrorState,
  Field,
  LoadingState,
  MetricCard,
  PageHeader,
  Status,
} from "../components/common";

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
      <PageHeader title="平台概览" detail="来自后端稳定目录的实时只读摘要，无浏览器伪造指标。" />
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
          <h2>最近 Run（按后端稳定顺序）</h2>
          {recentRun ? <Link to={`/runs/${recentRun.run_id}`}>{recentRun.run_id} · {recentRun.lifecycle}</Link> : <p>暂无 Run</p>}
        </article>
        <article className="panel">
          <h2>最近正式 Result</h2>
          {recentResult ? <Link to={`/results/${recentResult.run_id}`}>{recentResult.run_id} · {recentResult.acceptance_overall ?? "NotFullyEvaluated"}</Link> : <p>暂无 Result</p>}
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
  return <><PageHeader title={`${item.name} · v${item.version}`} detail="节点能力、初始几何和环境绑定的只读版本。" /><section className="panel"><dl className="inline-fields"><Field label="ScenarioId">{item.scenario_id}</Field><Field label="Environment"><Link to={`/environments/${item.environment.environment_asset_id}`}>{item.environment.environment_asset_id}</Link> · v{item.environment.asset_format_version}</Field><Field label="Mobility">{item.mobility.model}</Field><Field label="Fusion center">Node {item.fusion_center_node_id}</Field></dl></section><section className="panel"><h2>Nodes</h2><table><thead><tr><th>ID</th><th>TX / RX</th><th>Duplex</th><th>Initial position (m)</th><th>Velocity (m/s)</th></tr></thead><tbody>{item.nodes.map((node) => <tr key={node.node_id}><td>{node.node_id}{node.node_id === item.fusion_center_node_id ? " · Fusion" : ""}</td><td>{node.can_transmit ? "TX" : "—"} / {node.can_receive ? "RX" : "—"}</td><td>{node.duplex_mode}</td><td>{node.initial_position.x_meters}, {node.initial_position.y_meters}, {node.initial_position.z_meters}</td><td>{node.initial_velocity.x_meters_per_second}, {node.initial_velocity.y_meters_per_second}, {node.initial_velocity.z_meters_per_second}</td></tr>)}</tbody></table></section></>;
}

export function ExperimentCatalogPage() {
  const query = useQuery(queries.experiments());
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Experiment Catalog" detail="配置版本绑定精确 Scenario，并可创建真实 Run。" /><div className="card-grid">{query.data.map((item) => <article className="panel" key={`${item.experiment_id}:${item.version}`}><div className="panel-title"><Link to={`/experiments/${item.experiment_id}/versions/${item.version}`}>{item.name}</Link><span>v{item.version}</span></div><dl><Field label="Scenario">{item.scenario.scenario_id} · v{item.scenario.version}</Field><Field label="Routing / MAC">{item.routing.mode} / {item.mac.mode}</Field><Field label="PHY">{item.phy.bit_rate_bits_per_second} bit/s · {item.phy.rx_quality_mode}</Field><Field label="Cycles">{item.simulation_cycle_count}</Field></dl></article>)}</div></>;
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
  return <><PageHeader title={`${item.name} · v${item.version}`} detail="完整 backend DTO，只读；Run 仅提交 Experiment identity/version。" /><RunExperimentButton experiment={item} /><section className="two-column"><article className="panel"><h2>Protocol</h2><dl><Field label="Scenario"><Link to={`/scenarios/${item.scenario.scenario_id}/versions/${item.scenario.version}`}>{item.scenario.scenario_id} · v{item.scenario.version}</Link></Field><Field label="Routing">{item.routing.mode}</Field><Field label="MAC">{item.mac.mode}</Field><Field label="Slot / Guard">{item.mac.slot_duration_ns} ns / {item.mac.guard_interval_ns} ns</Field></dl></article><article className="panel"><h2>PHY / Execution</h2><dl><Field label="Rate">{item.phy.bit_rate_bits_per_second} bit/s</Field><Field label="Center / Bandwidth">{item.phy.center_frequency_hz} Hz / {item.phy.occupied_bandwidth_hz} Hz</Field><Field label="BER evidence">{item.phy.rx_quality_mode}</Field><Field label="Cycles / Seed">{item.simulation_cycle_count} / {item.deterministic_seed}</Field></dl></article></section></>;
}

export function RunCatalogPage() {
  const query = useQuery({ ...queries.runs(), refetchInterval: 1000 });
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Run Catalog" detail="后端 in-memory Run catalog 是权威来源。" />{!query.data.length ? <EmptyState>暂无 Run</EmptyState> : <section className="panel"><table><thead><tr><th>RunId</th><th>Experiment</th><th>Lifecycle</th><th>Result</th><th>Failure</th></tr></thead><tbody>{query.data.map((item) => <tr key={item.run_id}><td><Link to={`/runs/${item.run_id}`}>{item.run_id}</Link></td><td>{item.experiment_id} · v{item.experiment_version}</td><td><Status value={item.lifecycle} /></td><td>{item.result_available ? <Link to={`/results/${item.run_id}`}>Available</Link> : "—"}</td><td>{item.failure ? `${item.failure.code}: ${item.failure.message}` : "—"}</td></tr>)}</tbody></table></section>}</>;
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
  return <><PageHeader title={`Run Monitor · ${item.run_id}`} detail="只读 lifecycle、captured inputs 与 RunEventSequence 投影。" /><section className="metric-grid"><MetricCard label="Lifecycle" value={<Status value={item.lifecycle} />} /><MetricCard label="Events" value={projection.eventCount} /><MetricCard label="Latest sequence" value={projection.latestSequence} /><MetricCard label="Latest simulation time" value={`${projection.latestSimulationTimeNs ?? "—"} ns`} /></section>{streamError && <ErrorState error={streamError} />}<section className="two-column"><article className="panel"><h2>Captured resources</h2><dl><Field label="Experiment">{item.experiment_id} · v{item.experiment_version}</Field><Field label="Scenario">{item.scenario_id} · v{item.scenario_version}</Field><Field label="Environment">{item.environment_asset_id} · v{item.environment_format_version}</Field><Field label="Event stream complete">{String(item.event_stream_complete)}</Field></dl></article><article className="panel"><h2>Event counters</h2><dl><Field label="Transmission">{projection.transmissionCount}</Field><Field label="ChannelOutcome">{projection.channelOutcomeCount}</Field><Field label="Reception">{projection.receptionCount}</Field><Field label="CycleCommit">{projection.cycleCommitCount}</Field></dl></article></section>{item.failure && <ErrorState error={new ApiFailure(item.failure.code.includes("Protocol") ? "ProtocolFailure" : "RunFailed", item.failure.code, item.failure.message)} />}{item.lifecycle === "Completed" && <Link className="button-link" to={`/results/${item.run_id}`}>查看正式 Result</Link>}</>;
}

export function ResultCatalogPage() {
  const query = useQuery(queries.results());
  if (query.isPending || query.error) return <QueryState query={query} />;
  return <><PageHeader title="Result Catalog" detail="仅包含已原子发布 formal Result 的 Completed Run。" />{!query.data.length ? <EmptyState>暂无正式 Result</EmptyState> : <section className="panel"><table><thead><tr><th>RunId</th><th>Acceptance</th><th>Duration</th><th>Fusion results</th></tr></thead><tbody>{query.data.map((item) => <tr key={item.run_id}><td><Link to={`/results/${item.run_id}`}>{item.run_id}</Link></td><td><Status value={item.acceptance_overall ?? "NotFullyEvaluated"} /></td><td>{item.simulation_duration_ns} ns</td><td>{item.fusion_result_count}</td></tr>)}</tbody></table></section>}</>;
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
  const berSource = experiment.data.phy.rx_quality_mode === "ModeledBpskAwgn"
    ? "Modeled（BPSK/AWGN 模型，不是硬件测量）"
    : "NotEvaluated（未提供 BER evidence）";
  return <><PageHeader title={`Result · ${item.run_id}`} detail="正式只读结果及资源 provenance。" /><section className="metric-grid"><MetricCard label="Overall" value={<Status value={report?.overall ?? "NotFullyEvaluated"} />} /><MetricCard label="Nodes" value={item.projection.node_count} /><MetricCard label="Communication rate" value={`${experiment.data.phy.bit_rate_bits_per_second} bit/s`} /><MetricCard label="Duration" value={`${item.projection.simulation_duration_ns} ns`} /><MetricCard label="Bearing points" value={report?.minimum_bearing_points ?? "NotEvaluated"} /><MetricCard label="Fusion period" value={report?.maximum_fusion_period_ns ? `${report.maximum_fusion_period_ns} ns` : "NotEvaluated"} /></section><section className="two-column"><article className="panel"><h2>Acceptance</h2><dl><Field label="Network nodes">{report?.network_node_count ?? "NotEvaluated"}</Field><Field label="Communication rate">{report?.communication_rate ?? "NotEvaluated"}</Field><Field label="BER status">{report?.bit_error_rate ?? "NotEvaluated"}</Field><Field label="BER source">{berSource}</Field><Field label="BER reason">{report?.ber_reason || "—"}</Field><Field label="Maximum / Mean BER">{report?.maximum_ber ?? "—"} / {report?.mean_ber ?? "—"}</Field><Field label="Feature fusion">{report?.feature_level_fusion ?? "NotEvaluated"}</Field><Field label="Bearing count">{report?.bearing_point_count ?? "NotEvaluated"}</Field><Field label="Fusion period">{report?.fusion_period ?? "NotEvaluated"}</Field></dl></article><article className="panel"><h2>Projection</h2><dl><Field label="Delivery">{item.projection.local_delivery_count}</Field><Field label="No arrival">{item.projection.channel_no_arrival_count}</Field><Field label="Receptions">{item.projection.reception_count}</Field><Field label="Signals">{item.projection.channel_signal_count}</Field><Field label="Transmissions">{item.projection.transmission_count}</Field></dl></article></section><section className="panel"><h2>Fusion results</h2>{item.fusion_results.length ? <table><thead><tr><th>Sequence</th><th>Period ns</th><th>Observations</th><th>Estimate (m)</th></tr></thead><tbody>{item.fusion_results.map((fusion) => <tr key={fusion.fusion_sequence}><td>{fusion.fusion_sequence}</td><td>{fusion.fusion_period_ns}</td><td>{fusion.observation_count}</td><td>{fusion.estimated_target_x_meters}, {fusion.estimated_target_y_meters}</td></tr>)}</tbody></table> : <p>无 fusion result records</p>}</section></>;
}

export function NotFoundPage() {
  return <ErrorState error={new ApiFailure("NotFound", "NotFound", "Frontend route was not found.")} />;
}
