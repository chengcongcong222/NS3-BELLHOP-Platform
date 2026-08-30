import environmentFixture from "../../../backend/tests/fixtures/environment_detail.json";
import experimentFixture from "../../../backend/tests/fixtures/experiment_detail.json";
import resultFixture from "../../../backend/tests/fixtures/result_detail.json";
import runFixture from "../../../backend/tests/fixtures/run_detail.json";
import scenarioFixture from "../../../backend/tests/fixtures/scenario_detail.json";
import type {
  EnvironmentDto,
  ExperimentDto,
  ResultDto,
  ResultSummaryDto,
  RunDto,
  RunSummaryDto,
  ScenarioDto,
} from "../api/types";

export const environment = environmentFixture as EnvironmentDto;
export const scenario = scenarioFixture as ScenarioDto;
export const experiment = experimentFixture as ExperimentDto;
export const run = runFixture as RunDto;
export const result = resultFixture as ResultDto;
export const runSummary: RunSummaryDto = {
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
