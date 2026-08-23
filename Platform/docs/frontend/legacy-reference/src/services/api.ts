/**
 * API service — communicates with the FastAPI backend at /api/*
 * All paths are proxied by Vite dev server → http://127.0.0.1:8000
 */

import type { BathymetryData, DemoScenario, EnvironmentCapabilities, EnvironmentDatabase, EnvironmentPreviewRays, ExperimentTemplate, LinkMetric, LinkRays, ModelLibrarySection, NodeTemplate, TraceEventRecord, WossCacheRecord, WossSourceProfile, WossSourceVariant } from '../types';

const API_ORIGIN = import.meta.env.VITE_API_BASE ?? (import.meta.env.DEV ? 'http://127.0.0.1:8000' : '');
const BASE = `${API_ORIGIN}/api`;

export async function fetchScenarios(): Promise<string[]> {
  const res = await fetch(`${BASE}/scenarios`);
  if (!res.ok) throw new Error(`fetchScenarios failed: ${res.status}`);
  return res.json();
}

export async function fetchScenario(name: string): Promise<DemoScenario> {
  const res = await fetch(`${BASE}/scenario/${encodeURIComponent(name)}`);
  if (!res.ok) throw new Error(`fetchScenario(${name}) failed: ${res.status}`);
  return res.json();
}

export async function saveScenario(name: string, data: DemoScenario): Promise<void> {
  const res = await fetch(`${BASE}/scenario/${encodeURIComponent(name)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(data),
  });
  if (!res.ok) throw new Error(`saveScenario failed: ${res.status}`);
}

export interface ResultsResponse {
  file: string | null;
  rows: LinkMetric[];
}

export interface EventsResponse {
  file: string | null;
  rows: TraceEventRecord[];
}

export async function fetchResults(scenario?: string): Promise<ResultsResponse> {
  const query = scenario ? `?scenario=${encodeURIComponent(scenario)}` : '';
  const res = await fetch(`${BASE}/results${query}`);
  if (!res.ok) throw new Error(`fetchResults failed: ${res.status}`);
  return res.json();
}

export async function fetchEvents(scenario?: string): Promise<EventsResponse> {
  const query = scenario ? `?scenario=${encodeURIComponent(scenario)}` : '';
  const res = await fetch(`${BASE}/events${query}`);
  if (!res.ok) throw new Error(`fetchEvents failed: ${res.status}`);
  return res.json();
}

export async function fetchBathymetry(path: string): Promise<BathymetryData | null> {
  try {
    const res = await fetch(`${BASE}/bathymetry?path=${encodeURIComponent(path)}`);
    if (!res.ok) return null;
    return res.json();
  } catch {
    return null;
  }
}

export async function fetchRays(scenario: string): Promise<LinkRays[]> {
  try {
    const res = await fetch(`${BASE}/rays?scenario=${encodeURIComponent(scenario)}`);
    if (!res.ok) return [];
    return res.json();
  } catch {
    return [];
  }
}

export async function fetchEnvironmentPreviewRays(
  arrJsonFile: string,
  txPos: [number, number, number],
  rxPos: [number, number, number],
  mode: 'interpolate' | 'nearest' = 'interpolate',
): Promise<EnvironmentPreviewRays | null> {
  try {
    const params = new URLSearchParams({
      arr_json_file: arrJsonFile,
      tx_x: String(txPos[0]),
      tx_y: String(txPos[1]),
      tx_z: String(txPos[2]),
      rx_x: String(rxPos[0]),
      rx_y: String(rxPos[1]),
      rx_z: String(rxPos[2]),
      mode,
    });
    const res = await fetch(`${BASE}/environment-preview-rays?${params.toString()}`);
    if (!res.ok) return null;
    return res.json();
  } catch {
    return null;
  }
}

export async function fetchDataFiles(kind: 'grid' | 'bathymetry' | 'ssp' | 'arrivals'): Promise<string[]> {
  try {
    const res = await fetch(`${BASE}/data-files?kind=${kind}`);
    if (!res.ok) return [];
    return res.json();
  } catch {
    return [];
  }
}

export interface CreateEnvironmentDatabaseParams {
  name: string;
  description?: string;
  ssp_file?: string;
  bathymetry_file?: string;
  grid_file: string;
  arr_json_file?: string;
  arr_file_path?: string;
  env_path?: string;
  frequency_hz?: number;
  range_max_m?: number;
  range_step_m?: number;
  num_range_points?: number;
  depth_max_m?: number;
  water_depth_m?: number;
  source_depth_m?: number;
  source_depths?: number[];
  focus_depth_min_m?: number;
  focus_depth_max_m?: number;
  sound_speed_mps?: number;
  source_level_db?: number;
  spreading_factor?: number;
  absorption_db_per_km?: number;
  max_bounces?: number;
  receiver_depths?: number[];
  run_type?: string;
  build_mode?: string;
  sampling_profile?: string;
  engine_name?: string;
  engine_version?: string;
  engine_path?: string;
  at_compatibility?: string;
  arr_syntax?: string;
  data_source_type?: string;
  woss_source_id?: string;
  woss_profile_id?: string;
  validation_status?: string;
  validated_at?: number;
  dataset_versions?: Record<string, string>;
  geo_reference?: {
    latitude_deg?: number | null;
    longitude_deg?: number | null;
    region?: string | null;
    transect_label?: string | null;
  };
  time_reference?: {
    month?: number | null;
    season?: string | null;
    label?: string | null;
    timestamp_utc?: string | null;
  };
  notes?: string;
  ssp_analysis?: {
    surface_sound_speed?: number | null;
    mld?: number | null;
    axis_depth?: number | null;
    bottom_depth?: number | null;
    thermocline_gradient_max?: number | null;
  };
}

export async function fetchEnvironmentDatabases(): Promise<EnvironmentDatabase[]> {
  const res = await fetch(`${BASE}/environment-databases`);
  if (!res.ok) throw new Error(`fetchEnvironmentDatabases failed: ${res.status}`);
  return res.json();
}

export async function fetchEnvironmentCapabilities(): Promise<EnvironmentCapabilities> {
  const res = await fetch(`${BASE}/environment-capabilities`);
  if (!res.ok) throw new Error(`fetchEnvironmentCapabilities failed: ${res.status}`);
  return res.json();
}

export async function createEnvironmentDatabase(params: CreateEnvironmentDatabaseParams): Promise<EnvironmentDatabase> {
  const res = await fetch(`${BASE}/environment-databases`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; database: EnvironmentDatabase } = await res.json();
  return data.database;
}

export async function deleteEnvironmentDatabase(name: string): Promise<void> {
  const res = await fetch(`${BASE}/environment-database/${encodeURIComponent(name)}`, { method: 'DELETE' });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
}

export async function updateEnvironmentDatabase(name: string, updates: { new_name?: string; description?: string }): Promise<EnvironmentDatabase> {
  const res = await fetch(`${BASE}/environment-database/${encodeURIComponent(name)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(updates),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; database: EnvironmentDatabase } = await res.json();
  return data.database;
}

export interface CreateWossSourceParams {
  name: string;
  description?: string;
  ssp_file?: string;
  bathymetry_file?: string;
  location?: {
    latitude_deg?: number | null;
    longitude_deg?: number | null;
    region?: string | null;
    transect_label?: string | null;
  };
  time_reference?: {
    month?: number | null;
    season?: string | null;
    label?: string | null;
    timestamp_utc?: string | null;
  };
  datasets?: Record<string, string>;
  profiles?: WossSourceVariant[];
  notes?: string;
}

export async function fetchWossSources(): Promise<WossSourceProfile[]> {
  const res = await fetch(`${BASE}/woss-sources`);
  if (!res.ok) throw new Error(`fetchWossSources failed: ${res.status}`);
  return res.json();
}

export async function createWossSource(params: CreateWossSourceParams): Promise<WossSourceProfile> {
  const res = await fetch(`${BASE}/woss-sources`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; source: WossSourceProfile } = await res.json();
  return data.source;
}

export async function deleteWossSource(name: string): Promise<void> {
  const res = await fetch(`${BASE}/woss-source/${encodeURIComponent(name)}`, {
    method: 'DELETE',
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
}

export interface ImportWossCacheIntoSourceParams {
  source_name: string;
  source_display_name?: string;
  source_description?: string;
  target_kind: 'source' | 'profile';
  profile_name?: string;
  profile_description?: string;
  ssp_file?: string;
  bathymetry_file?: string;
  location?: {
    latitude_deg?: number | null;
    longitude_deg?: number | null;
    region?: string | null;
    transect_label?: string | null;
  };
  time_reference?: {
    month?: number | null;
    season?: string | null;
    label?: string | null;
    timestamp_utc?: string | null;
  };
  datasets?: Record<string, string>;
  notes?: string;
}

export interface ImportWossCacheIntoSourceResponse {
  cache: WossCacheRecord;
  source: WossSourceProfile;
  target_kind: 'source' | 'profile';
  profile_id?: string | null;
}

export async function importWossCacheIntoSource(params: ImportWossCacheIntoSourceParams): Promise<ImportWossCacheIntoSourceResponse> {
  const res = await fetch(`${BASE}/woss-cache/import`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean } & ImportWossCacheIntoSourceResponse = await res.json();
  return {
    cache: data.cache,
    source: data.source,
    target_kind: data.target_kind,
    profile_id: data.profile_id ?? null,
  };
}

export interface ImportWossRealDataIntoSourceParams {
  source_name: string;
  source_display_name?: string;
  source_description?: string;
  target_kind: 'source' | 'profile';
  profile_name?: string;
  profile_description?: string;
  latitude_deg: number;
  longitude_deg: number;
  region?: string;
  month?: number;
  season?: string;
  datasets?: Record<string, string>;
  notes?: string;
  range_max_m?: number;
  depth_max_m?: number;
  transect_bearing_deg?: number;
  sample_count?: number;
}

export interface ImportWossRealDataIntoSourceResponse extends ImportWossCacheIntoSourceResponse {
  generated_artifacts: {
    ssp_file?: string | null;
    bathymetry_file?: string | null;
  };
  real_data: {
    woa_grid_latitude_deg: number;
    woa_grid_longitude_deg: number;
    ssp_depth_max_m?: number | null;
    requested_depth_limit_m?: number | null;
    transect_bearing_deg: number;
    range_max_m: number;
    sample_count: number;
    time_code: string;
    time_label: string;
  };
}

export async function importWossRealDataIntoSource(params: ImportWossRealDataIntoSourceParams): Promise<ImportWossRealDataIntoSourceResponse> {
  const res = await fetch(`${BASE}/woss-cache/import-real-data`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean } & ImportWossRealDataIntoSourceResponse = await res.json();
  return {
    cache: data.cache,
    source: data.source,
    target_kind: data.target_kind,
    profile_id: data.profile_id ?? null,
    generated_artifacts: data.generated_artifacts ?? {},
    real_data: data.real_data,
  };
}

export interface BuildEnvironmentFromWossSourceParams {
  name?: string;
  description?: string;
  profile_id?: string;
  build_mode?: 'bellhop' | 'analytical';
  frequency_hz?: number;
  range_max_m?: number;
  depth_max_m?: number;
  water_depth_m?: number;
  source_depth_m?: number;
  source_depths?: number[];
  sound_speed_mps?: number;
  source_level_db?: number;
  spreading_factor?: number;
  absorption_db_per_km?: number;
  max_bounces?: number;
  receiver_depths?: number[];
  run_type?: string;
  bellhop_exe?: string;
  engine_version?: string;
  at_compatibility?: string;
  arr_syntax?: string;
  notes?: string;
  overwrite?: boolean;
}

export async function buildEnvironmentFromWossSource(sourceId: string, params: BuildEnvironmentFromWossSourceParams): Promise<EnvironmentDatabase> {
  const res = await fetch(`${BASE}/woss-source/${encodeURIComponent(sourceId)}/build-environment`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; database: EnvironmentDatabase } = await res.json();
  return data.database;
}

export interface BuildEnvironmentBatchFromWossSourceParams extends Omit<BuildEnvironmentFromWossSourceParams, 'name' | 'profile_id'> {
  profile_ids?: string[];
  build_all_profiles?: boolean;
  name_prefix?: string;
}

export async function buildEnvironmentBatchFromWossSource(sourceId: string, params: BuildEnvironmentBatchFromWossSourceParams): Promise<EnvironmentDatabase[]> {
  const res = await fetch(`${BASE}/woss-source/${encodeURIComponent(sourceId)}/build-environments`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; databases: EnvironmentDatabase[] } = await res.json();
  return data.databases;
}

export async function importBathymetry(file: File, name?: string): Promise<{ path: string; points: number }> {
  const form = new FormData();
  form.append('file', file);
  if (name) form.append('name', name);
  const res = await fetch(`${BASE}/import-bathymetry`, { method: 'POST', body: form });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export interface GenerateGridParams {
  name: string;
  max_range_m?: number;
  max_depth_m?: number;
  frequency_khz?: number;
  source_level_db?: number;
  sound_speed_mps?: number;
  spreading_factor?: number;
}

export async function generateGrid(params: GenerateGridParams): Promise<string | null> {
  const res = await fetch(`${BASE}/generate-grid`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { path: string } = await res.json();
  return data.path;
}

export interface GenerateBathymetryParams {
  name: string;
  max_range_m?: number;
  base_depth_m?: number;
  profile?: 'flat' | 'ridge' | 'slope' | 'trench';
  feature_range_m?: number;
  feature_height_m?: number;
}

export async function generateBathymetry(params: GenerateBathymetryParams): Promise<string | null> {
  const res = await fetch(`${BASE}/generate-bathymetry`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { path: string } = await res.json();
  return data.path;
}

export interface RunStatus {
  phase: 'idle' | 'running' | 'done' | 'error';
  scenario: string | null;
  started_at: number | null;
  ended_at: number | null;
  exit_code: number | null;
}

export async function fetchStatus(): Promise<RunStatus> {
  const res = await fetch(`${BASE}/status`);
  if (!res.ok) throw new Error(`fetchStatus failed: ${res.status}`);
  return res.json();
}

export interface HistoryRecord {
  timestamp: number;
  scenario: string;
  exit_code: number;
  csv_file: string | null;
  event_file?: string | null;
  rows: number;
  event_rows?: number;
  archive_id?: string | null;
  archived?: boolean;
  archive_error?: string | null;
}

export async function fetchHistory(): Promise<HistoryRecord[]> {
  const res = await fetch(`${BASE}/history`);
  if (!res.ok) throw new Error(`fetchHistory failed: ${res.status}`);
  return res.json();
}

export interface ExperimentArchiveSummary {
  id: string;
  scenario: string;
  created_at: number;
  status: 'success' | 'error';
  exit_code: number;
  trace_mode: string;
  transmission_type: string;
  environment_database_id: string | null;
  scenario_metadata: {
    name: string;
    version: string;
    description: string;
  };
  summary: {
    rows: number;
    event_rows: number;
    ray_rows: number;
    transmitter_count: number;
    receiver_count: number;
    avg_delay_s: number | null;
    max_delay_s: number | null;
    avg_received_level_db: number | null;
    min_received_level_db: number | null;
    avg_snr_db: number | null;
    unique_event_code_count: number;
    event_codes: string[];
  };
  files: {
    manifest: string;
    scenario_snapshot: string | null;
    execution_scene: string | null;
    results_csv: string | null;
    events_json: string | null;
    rays_json: string | null;
  };
}

export interface ExperimentArchiveDetail extends ExperimentArchiveSummary {
  logs: string[];
  metrics: LinkMetric[];
  events: TraceEventRecord[];
  rays: LinkRays[];
}

export interface ExperimentReportArchiveSnapshot {
  id: string;
  scenario: string;
  created_at: number;
  status: string;
  exit_code: number;
  trace_mode: string;
  transmission_type: string;
  environment_database_id: string | null;
  scenario_metadata: {
    name: string;
    version: string;
    description: string;
  };
  rows: number;
  event_rows: number;
  ray_rows: number;
  transmitter_count: number;
  receiver_count: number;
  avg_delay_s: number | null;
  max_delay_s: number | null;
  avg_received_level_db: number | null;
  min_received_level_db: number | null;
  avg_snr_db: number | null;
  unique_event_code_count: number;
  event_codes: string[];
}

export interface ExperimentReportDeltaMetric {
  metric: string;
  baseline: number | null;
  target: number | null;
  delta: number | null;
  delta_percent: number | null;
}

export interface ExperimentReportPairwiseDelta {
  baseline_id: string;
  target_id: string;
  metrics: ExperimentReportDeltaMetric[];
}

export interface ExperimentReport {
  generated_at: number;
  archive_count: number;
  archive_ids: string[];
  scenario: string | null;
  archives: ExperimentReportArchiveSnapshot[];
  common_event_codes: string[];
  rankings: Record<string, Array<{ id: string; scenario: string; value: number | null }>>;
  pairwise_deltas: ExperimentReportPairwiseDelta[];
  notes: string[];
  markdown: string;
}

export async function fetchExperimentArchives(): Promise<ExperimentArchiveSummary[]> {
  const res = await fetch(`${BASE}/experiment-archives`);
  if (!res.ok) throw new Error(`fetchExperimentArchives failed: ${res.status}`);
  return res.json();
}

export async function fetchExperimentArchiveDetail(archiveId: string): Promise<ExperimentArchiveDetail> {
  const res = await fetch(`${BASE}/experiment-archive/${encodeURIComponent(archiveId)}`);
  if (!res.ok) throw new Error(`fetchExperimentArchiveDetail(${archiveId}) failed: ${res.status}`);
  return res.json();
}

export async function fetchExperimentReport(archiveIds: string[]): Promise<ExperimentReport> {
  const res = await fetch(`${BASE}/experiment-report`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ archive_ids: archiveIds }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function fetchComponents(): Promise<ModelLibrarySection[]> {
  const res = await fetch(`${BASE}/components`);
  if (!res.ok) throw new Error(`fetchComponents failed: ${res.status}`);
  return res.json();
}

export async function fetchNodeTemplates(): Promise<NodeTemplate[]> {
  const res = await fetch(`${BASE}/node-templates`);
  if (!res.ok) throw new Error(`fetchNodeTemplates failed: ${res.status}`);
  return res.json();
}

export async function createNodeTemplate(template: Omit<NodeTemplate, 'id'> & { id?: string }): Promise<NodeTemplate> {
  const res = await fetch(`${BASE}/node-templates`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(template),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function updateNodeTemplate(templateId: string, template: Partial<NodeTemplate>): Promise<NodeTemplate> {
  const res = await fetch(`${BASE}/node-templates/${encodeURIComponent(templateId)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(template),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function deleteNodeTemplate(templateId: string): Promise<void> {
  const res = await fetch(`${BASE}/node-templates/${encodeURIComponent(templateId)}`, {
    method: 'DELETE',
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
}

export interface UpsertExperimentTemplateParams {
  name: string;
  description?: string;
  source_scenario_id?: string;
  tags?: string[];
  bindings?: {
    transmission_type?: string | null;
    environment_database_id?: string | null;
    measurement_engine_name?: string | null;
  };
  runtime?: {
    duration?: number | null;
    seed?: number | null;
    time_step_ms?: number | null;
    archive_experiment?: boolean | null;
    default_dashboard?: string | null;
  };
  summary?: {
    node_count?: number | null;
    topology_type?: string | null;
    transmission_type?: string | null;
  };
  notes?: string;
}

export async function fetchExperimentTemplates(): Promise<ExperimentTemplate[]> {
  const res = await fetch(`${BASE}/experiment-templates`);
  if (!res.ok) throw new Error(`fetchExperimentTemplates failed: ${res.status}`);
  return res.json();
}

export async function createExperimentTemplate(template: UpsertExperimentTemplateParams): Promise<ExperimentTemplate> {
  const res = await fetch(`${BASE}/experiment-templates`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(template),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; template: ExperimentTemplate } = await res.json();
  return data.template;
}

export async function updateExperimentTemplate(templateId: string, template: Partial<UpsertExperimentTemplateParams>): Promise<ExperimentTemplate> {
  const res = await fetch(`${BASE}/experiment-template/${encodeURIComponent(templateId)}`, {
    method: 'PATCH',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(template),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; template: ExperimentTemplate } = await res.json();
  return data.template;
}

export async function deleteExperimentTemplate(templateId: string): Promise<void> {
  const res = await fetch(`${BASE}/experiment-template/${encodeURIComponent(templateId)}`, {
    method: 'DELETE',
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
}

export async function applyExperimentTemplate(templateId: string, scenarioName: string): Promise<DemoScenario> {
  const res = await fetch(`${BASE}/experiment-template/${encodeURIComponent(templateId)}/apply`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ scenario_name: scenarioName }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  const data: { ok: boolean; scenario: DemoScenario } = await res.json();
  return data.scenario;
}

export async function deriveExperimentTemplate(
  templateId: string,
  name: string,
  sourceScenarioName?: string,
): Promise<{ ok: boolean; scenario_name: string; scenario: DemoScenario }> {
  const res = await fetch(`${BASE}/experiment-template/${encodeURIComponent(templateId)}/derive`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ name, source_scenario_name: sourceScenarioName }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function deriveScenario(source: string, name: string): Promise<{ ok: boolean; name: string }> {
  const res = await fetch(`${BASE}/scenario/derive`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ source, name }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export interface GenerateScenarioParams {
  name: string;
  purpose?: 'nlos_test' | 'los_baseline' | 'general';
  node_count?: number;
  topology_type?: 'p2p' | 'star' | 'full_mesh';
  terrain_type?: 'flat' | 'ridge' | 'slope' | 'trench';
  water_depth_m?: number;
  area_range_m?: number;
  node_depth_m?: number;
  nlos_ratio?: number;
}

export async function generateScenario(params: GenerateScenarioParams): Promise<{ ok: boolean; name: string; scenario: DemoScenario }> {
  const res = await fetch(`${BASE}/generate-scenario`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function deleteScenario(name: string): Promise<void> {
  const res = await fetch(`${BASE}/scenario/${encodeURIComponent(name)}`, {
    method: 'DELETE',
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
}

/* ─── Bellhop Pipeline ─── */

export interface RunBellhopParams {
  name: string;
  freq_hz?: number;
  source_depth_m?: number;
  source_depths?: number[];
  receiver_depths?: number[];
  range_max_m?: number;
  depth_max_m?: number;
  source_level_db?: number;
  ssp?: number[][];
  ssp_file?: string;
  bathymetry_file?: string;
  run_type?: string;
  skip_run?: boolean;
  bellhop_exe?: string;
}

export interface RunBellhopResult {
  arr_json_path?: string;
  grid_path?: string;
  arr_file_path?: string;
  env_path?: string;
  env_only?: boolean;
  env?: string;
  bty?: string;
  ati?: string;
  source_depths?: number[];
  receiver_depths?: number[];
  warnings?: string[];
  recommendations?: {
    ssp_depth_max_m?: number | null;
    bathymetry_range_max_m?: number | null;
    bathymetry_depth_max_m?: number | null;
    recommended_range_max_m?: number | null;
    recommended_depth_max_m?: number | null;
  };
}

export async function runBellhop(params: RunBellhopParams): Promise<RunBellhopResult> {
  const res = await fetch(`${BASE}/run-bellhop`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

/* ─── SSP Management ─── */

export async function fetchSspData(path: string): Promise<number[][]> {
  const res = await fetch(`${BASE}/ssp-data?path=${encodeURIComponent(path)}`);
  if (!res.ok) return [];
  return res.json();
}

export async function uploadSsp(file: File, name?: string): Promise<{ path: string; lines: number }> {
  const form = new FormData();
  form.append('file', file);
  if (name) form.append('name', name);
  const res = await fetch(`${BASE}/upload-ssp`, { method: 'POST', body: form });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export interface GenerateSspParams {
  name: string;
  formula: 'munk' | 'isovelocity' | 'linear_gradient' | 'thermocline' | 'deep_channel';
  depth_max?: number;
  c0?: number;
  step?: number;
  gradient?: number;
  thermocline_depth?: number;
  thermocline_thickness?: number;
  surface_speed?: number;
  deep_speed?: number;
}

export async function generateSsp(params: GenerateSspParams): Promise<{ path: string; points: number }> {
  const res = await fetch(`${BASE}/generate-ssp`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify(params),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export async function saveSsp(path: string, rows: number[][]): Promise<{ path: string; points: number }> {
  const res = await fetch(`${BASE}/save-ssp`, {
    method: 'POST',
    headers: { 'Content-Type': 'application/json' },
    body: JSON.stringify({ path, rows }),
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ detail: res.statusText }));
    throw new Error((err as { detail: string }).detail ?? res.statusText);
  }
  return res.json();
}

export type SseEvent =
  | { type: 'start'; scenario: string }
  | { type: 'log'; message: string }
  | { type: 'done'; exit_code: number; results: LinkMetric[]; events?: TraceEventRecord[]; archive_id?: string | null; archive_error?: string | null }
  | { type: 'error'; exit_code: number; message?: string; archive_id?: string | null; archive_error?: string | null }
  | { type: 'cancelled' };

/**
 * POST /api/run then stream the SSE response.
 * Calls onEvent for each parsed event; calls onClose when stream ends.
 * Returns an AbortController so the caller can cancel.
 */
export function startRun(
  scenario: string,
  onEvent: (event: SseEvent) => void,
  onClose: () => void,
): AbortController {
  const controller = new AbortController();

  (async () => {
    let res: Response;
    try {
      res = await fetch(`${BASE}/run`, {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ scenario }),
        signal: controller.signal,
      });
    } catch {
      onEvent({ type: 'error', exit_code: -1, message: '无法连接后端运行服务。' });
      onClose();
      return;
    }

    if (!res.ok || !res.body) {
      let message = `启动失败：HTTP ${res.status}`;
      try {
        const err = await res.json() as { detail?: string };
        if (err.detail) {
          message = err.detail;
        }
      } catch {
        // ignore parse failure
      }
      onEvent({ type: 'error', exit_code: res.status, message });
      onClose();
      return;
    }

    const reader = res.body.getReader();
    const decoder = new TextDecoder();
    let buffer = '';

    while (true) {
      let done: boolean;
      let value: Uint8Array | undefined;
      try {
        ({ done, value } = await reader.read());
      } catch {
        break;
      }
      if (done) break;

      buffer += decoder.decode(value, { stream: true });
      const parts = buffer.split('\n\n');
      buffer = parts.pop() ?? '';

      for (const part of parts) {
        for (const line of part.split('\n')) {
          if (line.startsWith('data: ')) {
            try {
              const parsed = JSON.parse(line.slice(6)) as SseEvent;
              onEvent(parsed);
            } catch {
              // ignore malformed line
            }
          }
        }
      }
    }
    onClose();
  })();

  return controller;
}
