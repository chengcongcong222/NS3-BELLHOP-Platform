import environmentFixture from "../../../backend/tests/fixtures/environment_detail.json";
import experimentFixture from "../../../backend/tests/fixtures/experiment_detail.json";
import resultFixture from "../../../backend/tests/fixtures/result_detail.json";
import runFixture from "../../../backend/tests/fixtures/run_detail.json";
import scenarioFixture from "../../../backend/tests/fixtures/scenario_detail.json";
import type {
  AcceptanceEvidenceDto,
  EnvironmentDto,
  ExperimentDto,
  ResultDto,
  ResultSummaryDto,
  RunDto,
  RunSummaryDto,
  ScenarioDto,
  SystemInfoDto,
} from "../api/types";

export const environment = environmentFixture as EnvironmentDto;
export const scenario = scenarioFixture as ScenarioDto;
export const experiment = experimentFixture as ExperimentDto;
export const run = runFixture as RunDto;
export const result = resultFixture as ResultDto;
export const runSummary: RunSummaryDto = {
  catalog_sequence: "1",
  run_id: run.run_id,
  experiment_id: run.experiment_id,
  experiment_version: run.experiment_version,
  scenario_id: run.scenario_id,
  scenario_version: run.scenario_version,
  environment_asset_id: run.environment_asset_id,
  environment_format_version: run.environment_format_version,
  lifecycle: run.lifecycle,
  event_stream_complete: run.event_stream_complete,
  result_available: true,
  failure: null,
};
export const resultSummary: ResultSummaryDto = {
  catalog_sequence: "1",
  run_id: result.run_id,
  experiment_id: result.experiment_id,
  experiment_version: result.experiment_version,
  scenario_id: result.scenario_id,
  scenario_version: result.scenario_version,
  environment_asset_id: result.environment_asset_id,
  environment_format_version: result.environment_format_version,
  acceptance_overall: result.acceptance_report?.overall ?? null,
  simulation_duration_ns: result.projection.simulation_duration_ns,
  fusion_result_count: String(result.fusion_results.length),
};
export const systemInfo: SystemInfoDto = {
  schema_version: 1,
  platform_name: "NS3-BELLHOP Platform",
  platform_version: "0.1.0",
  product_baseline: "P0-S5-02",
  build: { source_revision: "test-revision", configuration: "test", cxx_standard: "23" },
  simulation: { engine: "ns-3", version: "3.47", time_authority: "ns3::Simulator", scheduler_authority: "ns3::Simulator", scheduling_gateway: "M1 / Ns3KernelGateway" },
  interfaces: { api_schema_version: "1", worker_wire_schema_version: "1", acceptance_evidence_schema_version: "1", frontend_release: "test" },
  runtime_mode: "test",
};
export const acceptanceEvidence: AcceptanceEvidenceDto = {
  schema_version: 1,
  immutable_snapshot: true,
  baseline: {
    baseline_id: "Acceptance4Node",
    baseline_version: "1",
    classification: "third-party-acceptance-baseline",
    hard_requirements: {
      network_node_count_minimum: "3",
      network_node_count_maximum: "4",
      communication_rate_bits_per_second: "60",
      maximum_bit_error_rate: 0.0001,
      feature_level_fusion_required: true,
      minimum_bearing_points: "5",
      maximum_fusion_period_ns: "180000000000",
    },
    demo_parameters: {},
  },
  manifest: { run_id: result.run_id, system: systemInfo, environment, scenario, experiment },
  run: { run_id: result.run_id, lifecycle: "Completed", event_stream_complete: true },
  projection: result.projection,
  acceptance_report: result.acceptance_report!,
  fusion_results: result.fusion_results,
  nodes: result.nodes,
  semantics: {
    verdict_origin: "BackendAcceptanceReport",
    environment_evidence: "Reference / modeled",
    propagation_evidence: "Bellhop-derived",
    ber_evidence_source: "Modeled",
    ber_interpretation: "A modeled numerical result, not a hardware measurement; at high SNR the computed double may reach the floating-point representation floor.",
    no_arrival: "No physical arrival; no Reception exists.",
    not_decoded: "Arrival was not decoded.",
    aggregate_policy: "No unsupported aggregate is inferred.",
  },
};
