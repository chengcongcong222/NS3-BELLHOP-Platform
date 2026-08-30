import type {
  EnvironmentDto,
  ExperimentDto,
  ResultDto,
  ResultSummaryDto,
  RunDto,
  RunSummaryDto,
  ScenarioDto,
} from "./types";

export type ApiFailureKind =
  | "NotFound"
  | "BackendBusy"
  | "RunNotReady"
  | "RunFailed"
  | "ProtocolFailure"
  | "TransportUnavailable"
  | "ApiFailure";

export class ApiFailure extends Error {
  constructor(
    readonly kind: ApiFailureKind,
    readonly code: string,
    message: string,
  ) {
    super(message);
  }
}

type Validator<T> = (value: unknown) => value is T;

const isObject = (value: unknown): value is Record<string, unknown> =>
  typeof value === "object" && value !== null && !Array.isArray(value);
const isString = (value: unknown): value is string => typeof value === "string";
const isArrayOf = <T>(validator: Validator<T>): Validator<T[]> =>
  (value): value is T[] => Array.isArray(value) && value.every(validator);

const environment = (value: unknown): value is EnvironmentDto =>
  isObject(value) &&
  isString(value.environment_asset_id) &&
  isString(value.asset_format_version) &&
  isObject(value.axes) &&
  isObject(value.provenance);
const scenario = (value: unknown): value is ScenarioDto =>
  isObject(value) &&
  isString(value.scenario_id) &&
  isString(value.version) &&
  Array.isArray(value.nodes) &&
  isObject(value.environment);
const experiment = (value: unknown): value is ExperimentDto =>
  isObject(value) &&
  isString(value.experiment_id) &&
  isString(value.version) &&
  isObject(value.scenario) &&
  isObject(value.phy);
const run = ((value: unknown): value is RunDto =>
  isObject(value) &&
  isString(value.run_id) &&
  isString(value.experiment_id) &&
  isString(value.lifecycle)) as Validator<RunDto>;
const runSummary = ((value: unknown): value is RunSummaryDto =>
  isObject(value) &&
  run(value) &&
  typeof (value as unknown as Record<string, unknown>).result_available ===
    "boolean") as Validator<RunSummaryDto>;
const result = ((value: unknown): value is ResultDto =>
  isObject(value) &&
  isString(value.run_id) &&
  isObject(value.projection) &&
  isString(value.projection.simulation_duration_ns)) as Validator<ResultDto>;
const resultSummary = ((value: unknown): value is ResultSummaryDto =>
  isObject(value) &&
  isString(value.run_id) &&
  isString(value.simulation_duration_ns) &&
  isString(value.fusion_result_count)) as Validator<ResultSummaryDto>;

function failureKind(code: string, status: number): ApiFailureKind {
  if (status === 404 || code.endsWith("NotFound") || code === "NotFound") {
    return "NotFound";
  }
  if (code === "BackendBusy") return "BackendBusy";
  if (code === "RunNotReady") return "RunNotReady";
  if (code === "RunFailed") return "RunFailed";
  if (code.includes("Protocol")) return "ProtocolFailure";
  return "ApiFailure";
}

export class ApiClient {
  constructor(
    private readonly baseUrl = import.meta.env.VITE_API_BASE_URL ?? "",
  ) {}

  url(path: string): string {
    return `${this.baseUrl}${path}`;
  }

  private async request<T>(
    path: string,
    validator: Validator<T>,
    init?: RequestInit,
  ): Promise<T> {
    let response: Response;
    try {
      response = await fetch(this.url(path), init);
    } catch (error) {
      throw new ApiFailure(
        "TransportUnavailable",
        "TransportUnavailable",
        "Backend transport is unavailable.",
      );
    }
    let body: unknown;
    try {
      body = await response.json();
    } catch {
      throw new ApiFailure(
        "ProtocolFailure",
        "ProtocolFailure",
        "Backend returned non-JSON data.",
      );
    }
    if (!response.ok) {
      const error = isObject(body) && isObject(body.error) ? body.error : null;
      const code = error && isString(error.code) ? error.code : "ApiFailure";
      const message =
        error && isString(error.message) ? error.message : "Backend request failed.";
      throw new ApiFailure(failureKind(code, response.status), code, message);
    }
    if (!validator(body)) {
      throw new ApiFailure(
        "ProtocolFailure",
        "ProtocolFailure",
        "Backend response does not match the Frontend V2 DTO.",
      );
    }
    return body;
  }

  listEnvironments = () => this.request("/environments", isArrayOf(environment));
  getEnvironment = (id: string) =>
    this.request(`/environments/${encodeURIComponent(id)}`, environment);
  listScenarios = () => this.request("/scenarios", isArrayOf(scenario));
  getScenario = (id: string, version: string) =>
    this.request(
      `/scenarios/${encodeURIComponent(id)}/versions/${encodeURIComponent(version)}`,
      scenario,
    );
  listExperiments = () => this.request("/experiments", isArrayOf(experiment));
  getExperiment = (id: string, version: string) =>
    this.request(
      `/experiments/${encodeURIComponent(id)}/versions/${encodeURIComponent(version)}`,
      experiment,
    );
  listRuns = () => this.request("/runs", isArrayOf(runSummary));
  getRun = (id: string) => this.request(`/runs/${encodeURIComponent(id)}`, run);
  createRun = (experimentId: string, version: string) =>
    this.request("/runs", run, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        experiment_id: experimentId,
        experiment_version: version,
      }),
    });
  listResults = () => this.request("/results", isArrayOf(resultSummary));
  getResult = (runId: string) =>
    this.request(`/runs/${encodeURIComponent(runId)}/results`, result);
}

export const apiClient = new ApiClient();
