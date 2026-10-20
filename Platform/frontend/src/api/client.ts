import type {
  AuthoringDraftDto,
  DraftKind,
  EnvironmentBuildDto,
  AcceptanceEvidenceDto,
  EnvironmentDto,
  ExperimentDto,
  ResultDto,
  ResultSummaryDto,
  RunDto,
  RunGroupDto,
  RunActionsDto,
  RunDefinitionContextDto,
  RunSummaryDto,
  ScenarioDto,
  SystemInfoDto,
  TrajectoryAssetDto,
  RecordedChannelDto,
  RecordedNoiseDto,
  ProxyBindings,
  RecordedProxyResolved,
} from "./types";
import { acquireWithProgress, type AcquisitionProgress } from "./acquisition";
import { isAlgorithmCatalog } from "../domain/algorithms";
import { isExperimentPresets } from "../domain/experimentPresets";

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
  isString(value.catalog_sequence) &&
  typeof (value as unknown as Record<string, unknown>).result_available ===
    "boolean") as Validator<RunSummaryDto>;
const result = ((value: unknown): value is ResultDto =>
  isObject(value) &&
  isString(value.run_id) &&
  isObject(value.projection) &&
  isString(value.projection.simulation_duration_ns)) as Validator<ResultDto>;
const runGroup = (value: unknown): value is RunGroupDto =>
  isObject(value) && isString(value.group_id) && isString(value.state) && typeof value.archived === "boolean"
  && isString(value.common_snapshot_sha256) && isString(value.workload_sha256)
  && isObject(value.workload) && isString(value.workload.count)
  && Array.isArray(value.members) && (value.members.length === 2 || (value.members.length === 1 && value.members[0]?.member === "C")) && value.members.every(m =>
    isObject(m) && ["A", "B", "C"].includes(String(m.member)) && isString(m.run_id) && isString(m.state)
    && (m.result === null || result(m.result)));
const resultSummary = ((value: unknown): value is ResultSummaryDto =>
  isObject(value) &&
  isString(value.catalog_sequence) &&
  isString(value.run_id) &&
  isString(value.simulation_duration_ns) &&
    isString(value.fusion_result_count)) as Validator<ResultSummaryDto>;
const systemInfo = ((value: unknown): value is SystemInfoDto =>
  isObject(value) && isString(value.platform_name) &&
  isString(value.product_baseline) && isObject(value.build) &&
  isString(value.build.source_revision) && isObject(value.simulation) &&
  isString(value.simulation.engine) && isString(value.simulation.version) &&
  isString(value.simulation.time_authority) &&
  isString(value.simulation.scheduler_authority) &&
  isString(value.simulation.scheduling_gateway)) as Validator<SystemInfoDto>;
const acceptanceEvidence = ((value: unknown): value is AcceptanceEvidenceDto =>
  isObject(value) && value.immutable_snapshot === true &&
  isObject(value.baseline) && isObject(value.manifest) &&
  isObject(value.acceptance_report) && isObject(value.semantics) &&
  value.semantics.verdict_origin === "BackendAcceptanceReport") as Validator<AcceptanceEvidenceDto>;
const authoringDraft = ((value: unknown): value is AuthoringDraftDto =>
  isObject(value) && isString(value.kind) && isString(value.draft_id) &&
  isString(value.name) && isObject(value.payload) &&
  typeof value.revision === "number" &&
  Array.isArray(value.validation_issues)) as Validator<AuthoringDraftDto>;
const environmentBuild = ((value: unknown): value is EnvironmentBuildDto =>
  isObject(value) && isString(value.build_id) && isString(value.draft_id) &&
  isString(value.state) && Array.isArray(value.steps)) as Validator<EnvironmentBuildDto>;
const anyObject = ((value: unknown): value is Record<string, unknown> =>
  isObject(value)) as Validator<Record<string, unknown>>;
const trajectory = ((value: unknown): value is TrajectoryAssetDto => isObject(value) && isString(value.asset_id) && typeof value.version === "number" && isString(value.name) && Array.isArray(value.tracks) && isString(value.checksum_sha256)) as Validator<TrajectoryAssetDto>;

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
  listRunGroups = () => this.request("/run-groups", isArrayOf(runGroup));
  getRunGroup = (id: string) => this.request(`/run-groups/${encodeURIComponent(id)}`, runGroup);
  cancelPendingGroup = (id: string) => this.request(`/run-groups/${encodeURIComponent(id)}/cancel-pending`, runGroup, { method: "POST" });
  repeatRunGroup = (id: string, key: string) => this.request(`/run-groups/${encodeURIComponent(id)}/rerun`, runGroup, {
    method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ request_idempotency_key: key }),
  });
  retryGroupArchive = (id: string) => this.request(`/run-groups/${encodeURIComponent(id)}/retry-archive`, runGroup, { method: "POST" });
  createRunGroup = (draftId: string, revision: number, key: string) => this.request("/run-groups", runGroup, {
    method: "POST", headers: { "Content-Type": "application/json" },
    body: JSON.stringify({ draft_id: draftId, expected_revision: String(revision), request_idempotency_key: key }),
  });
  recordedChannels = () => this.request("/recorded-channels", isArrayOf((v: unknown): v is RecordedChannelDto =>
    isObject(v) && isString(v.manifest_sha256) && isObject(v.manifest) && isString(v.manifest.asset_id)
    && typeof v.manifest.duration_s === "number" && typeof v.manifest.fc === "number"));
  recordedNoise = () => this.request("/recorded-noise", isArrayOf((v: unknown): v is RecordedNoiseDto =>
    isObject(v) && isObject(v.manifest) && isString(v.manifest.asset_id) && typeof v.manifest.alpha === "number"));
  validateRecordedProxy = (body: ProxyBindings) => this.request("/authoring/recorded-proxy/validate",
    (v: unknown): v is RecordedProxyResolved => isObject(v) && Array.isArray(v.nodes) &&
      Array.isArray(v.resolved_links) && Array.isArray(v.rx_noise) && isObject(v.assumptions),
    { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  listTrajectories = () => this.request("/trajectories", isArrayOf(trajectory));
  translateTrajectory = (body: { source_asset_id: string; source_version: number; source_track_id: string; asset_id: string; name: string; x_m: number; y_m: number; z_m: number }) =>
    this.request("/authoring/trajectories/translate", trajectory, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  generateTrajectory = (body: Record<string, unknown>) => this.request("/authoring/trajectories/generate", trajectory, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
  importTrajectoryCsv = (body: Record<string, unknown>) => this.request("/authoring/trajectories/import-csv", trajectory, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(body) });
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
  getRunActions = (id: string) => this.request(`/runs/${encodeURIComponent(id)}/actions`,
    (value: unknown): value is RunActionsDto => isObject(value) && isString(value.run_id) &&
      isObject(value.edit) && typeof value.edit.available === "boolean" && isObject(value.rerun) && typeof value.rerun.available === "boolean");
  repeatRun = (id: string) => this.request(`/runs/${encodeURIComponent(id)}/rerun`, run, { method: "POST" });
  authoringCapabilities = () => this.request("/authoring/capabilities", anyObject);
  experimentPresets = () => this.request("/authoring/experiment-presets", isExperimentPresets);
  algorithmCatalog = () => this.request("/algorithms", isAlgorithmCatalog);
  getRunDefinitionContext = (id: string) => this.request(
    `/runs/${encodeURIComponent(id)}/definition-context`,
    (value: unknown): value is RunDefinitionContextDto => isObject(value) &&
      isString(value.run_id) && scenario(value.scenario) && experiment(value.experiment) &&
      (value.source === "AuthoredDefinition" || value.source === "Preset") &&
      isObject(value.execution) && Array.isArray(value.limitations),
  );
  createRun = (experimentId: string, version: string) =>
    this.request("/runs", run, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({
        experiment_id: experimentId,
        experiment_version: version,
      }),
    });
  createAuthoringRun = (draftId: string, revision?: number) =>
    this.request(`/authoring/experiments/${encodeURIComponent(draftId)}/runs`, run, {
      method: "POST",
      headers: revision === undefined ? undefined : { "If-Match": String(revision) },
    });
  listResults = () => this.request("/results", isArrayOf(resultSummary));
  getResult = (runId: string) =>
    this.request(`/runs/${encodeURIComponent(runId)}/results`, result);
  getSystemInfo = () => this.request("/system/info", systemInfo);
  getAcceptanceEvidence = (runId: string) =>
    this.request(`/runs/${encodeURIComponent(runId)}/acceptance-evidence`, acceptanceEvidence);
  acceptanceEvidenceTextUrl = (runId: string) =>
    this.url(`/runs/${encodeURIComponent(runId)}/acceptance-evidence.txt`);
  listDrafts = (kind?: DraftKind) =>
    this.request(`/authoring/drafts${kind ? `?kind=${kind}` : ""}`, isArrayOf(authoringDraft));
  createDraft = (kind: DraftKind, name: string, payload: Record<string, unknown>) =>
    this.request("/authoring/drafts", authoringDraft, {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ kind, name, payload }),
    });
  updateDraft = (draft: AuthoringDraftDto, name: string, payload: Record<string, unknown>) =>
    this.request(`/authoring/drafts/${draft.kind}/${encodeURIComponent(draft.draft_id)}`, authoringDraft, {
      method: "PUT", headers: { "Content-Type": "application/json", "If-Match": String(draft.revision) },
      body: JSON.stringify({ kind: draft.kind, name, payload }),
    });
  importSsp = (filename: string, content: string) =>
    this.request("/authoring/import/ssp", anyObject, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ filename, content }) });
  importTerrain = (filename: string, content: string) =>
    this.request("/authoring/import/terrain", anyObject, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ filename, content }) });
  acquireWoa = (request: Record<string, unknown>) =>
    this.request("/authoring/acquisition/woa", anyObject, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(request) });
  acquireWoaProgress = (request: Record<string, unknown>, progress: (value: AcquisitionProgress) => void, signal: AbortSignal) =>
    acquireWithProgress(this.url("/authoring/acquisition/woa/stream"), request, progress, signal);
  environmentCapabilities = () => this.request("/authoring/environment-capabilities", anyObject);
  hydrologyCache = () => this.request("/authoring/environment/hydrology-cache", isArrayOf(anyObject));
  hydrologyProfile = (id: string) => this.request(`/authoring/environment/hydrology-cache/${encodeURIComponent(id)}`, anyObject);
  environmentInputs = (id: string) => this.request(`/environments/${encodeURIComponent(id)}/authoring-inputs`, anyObject);
  environmentSummaries = () => this.request("/environments/authoring-summaries", anyObject);
  acquireTerrain = (request: Record<string, unknown>) =>
    this.request("/authoring/acquisition/terrain", anyObject, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify(request) });
  createEnvironmentBuild = (draftId: string, revision?: number) =>
    this.request("/authoring/environment-builds", environmentBuild, { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ draft_id: draftId, expected_revision: revision }) });
  publishEnvironmentBuild = (buildId: string) =>
    this.request(`/authoring/environment-builds/${encodeURIComponent(buildId)}/publish`, anyObject, { method: "POST" });
  getEnvironmentBuild = (buildId: string) =>
    this.request(`/authoring/environment-builds/${encodeURIComponent(buildId)}`, environmentBuild);
  deleteDraft = async (draft: AuthoringDraftDto) => {
    const response = await fetch(this.url(`/authoring/drafts/${draft.kind}/${encodeURIComponent(draft.draft_id)}`), { method: "DELETE" });
    if (!response.ok) throw new ApiFailure("ApiFailure", "DeleteFailed", "草稿删除失败");
  };
}

export const apiClient = new ApiClient();
