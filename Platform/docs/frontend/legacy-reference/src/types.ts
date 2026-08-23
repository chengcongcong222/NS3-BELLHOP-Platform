export type NavKey =
  | 'overview'
  | 'studio'
  | 'templates'
  | 'config'
  | 'monitor'
  | 'results'
  | 'assets'
  | 'settings';

export type ModelAssetKind =
  | 'node-base'
  | 'node-model'
  | 'topology'
  | 'link-profile'
  | 'environment'
  | 'communication-technique'
  | 'mac-protocol'
  | 'routing-protocol'
  | 'noise-model'
  | 'runtime';

export interface ModelLibraryAsset {
  id: string;
  name: string;
  kind: ModelAssetKind;
  summary: string;
  parameters: string[];
  replaceableScopes: string[];
  defaults?: Record<string, unknown>;
}

export interface ModelLibrarySection {
  id: string;
  title: string;
  eyebrow: string;
  assets: ModelLibraryAsset[];
}

export interface StudioPreviewFrame {
  id: string;
  label: string;
  detail: string;
  technique: 'broadcast' | 'many_to_one' | 'one_to_one';
  sourceIds: number[];
  targetIds: number[];
}

export interface StudioNodeBinding {
  nodeId: number;
  nodeTemplateId?: string;
  nodeBaseAssetId?: string;
  nodeModelAssetId?: string;
  macProtocolAssetId?: string;
  routingProtocolAssetId?: string;
  overridesByAssetId: Record<string, Record<string, string>>;
}

export interface StudioEdgeBinding {
  edgeKey: string;
  linkProfileAssetId?: string;
  overridesByAssetId: Record<string, Record<string, string>>;
}

export interface StudioSceneBinding {
  topologyAssetId?: string;

  communicationTechniqueAssetId?: string;
  overridesByAssetId: Record<string, Record<string, string>>;
}

export interface StudioBindingsSnapshot {
  scene: StudioSceneBinding;
  nodes: Record<number, StudioNodeBinding>;
  edges: Record<string, StudioEdgeBinding>;
}

export interface NodeTemplateConfig {
  role: string;
  communication_range_m?: number;
  tx_power_db?: number;
  center_frequency_hz?: number;
  mobility?: DemoNode['mobility'];
  application?: DemoNode['application'];
  mac?: DemoNode['mac'];
  routing?: DemoNode['routing'];
  bridge?: DemoNode['bridge'];
  binding?: {
    nodeBaseAssetId?: string;
    nodeModelAssetId?: string;
    macProtocolAssetId?: string;
    routingProtocolAssetId?: string;
  };
}

export interface NodeTemplate {
  id: string;
  name: string;
  description?: string;
  builtIn?: boolean;
  config: NodeTemplateConfig;
}

export interface DemoNode {
  id: number;
  role: string;
  position: [number, number, number];
  communication_range_m?: number;
  tx_power_db?: number;
  center_frequency_hz?: number;
  mobility: {
    model: string;
    speed_min?: number;
    speed_max?: number;
    pause_s?: number;
    area_x?: number;
    area_y?: number;
    vx?: number;
    vy?: number;
    vz?: number;
  };
  application?: {
    type: string;
    period_seconds?: number;
    packet_size?: number;
    beacon_interval_s?: number;
    destination_port?: number;
    listen_port?: number;
  };
  mac?: {
    protocol: string;
    max_retries?: number;
    slot_duration_ms?: number;
    sense_duration_ms?: number;
    backoff_min_ms?: number;
    backoff_max_ms?: number;
    guard_time_ms?: number;
    guard_ms?: number;
    poll_interval_s?: number;
    poll_cycle_s?: number;
  };
  routing?: {
    protocol: string;
    next_hop?: number;
    ttl?: number;
    hello_interval_s?: number;
    route_timeout_s?: number;
    tc_interval_s?: number;
  };
  bridge?: {
    type: string;
  };
}

export interface DemoScenario {
  simulation: {
    duration: number;
    scheduler: string;
    seed: number;
    time_step_ms: number;
  };
  scenario_metadata: {
    scenario_id: string;
    name: string;
    version: string;
    project_tags: string[];
    description: string;
  };
  nodes: DemoNode[];
  topology: {
    deployment_type: string;
    logical_type: string;
    center?: number;
    pairs?: Array<[number, number]>;
  };
  environment: {
    sound_speed_profile: string;
    boundary: {
      surface: string;
      bottom: string;
    };
  };
  noise: {
    composition: Array<{
      type: string;
      value_db?: number;
      shipping_factor?: number;
      wind_speed_mps?: number;
      center_frequency_hz?: number;
      [key: string]: unknown;
    }>;
  };
  transmission: {
    type: string;
    params: Record<string, string | number>;
  };
  measurement: {
    engine_name?: string;
    outputs: string[];
    noise_std?: number;
    dr_noise_std?: number;
  };
  output: {
    trace: string;
    stats_file: string;
    archive_experiment: boolean;
  };
  ui: {
    theme: string;
    enable_3d_view: boolean;
    default_dashboard: string;
    playback_speed?: number;
    studio_bindings?: StudioBindingsSnapshot;
  };
}

export interface EnvironmentDatabaseArtifacts {
  ssp_file: string | null;
  bathymetry_file?: string | null;
  grid_file: string;
  arr_json_file: string | null;
  arr_file_path?: string | null;
  env_path?: string | null;
}

export interface EnvironmentGeoReference {
  latitude_deg?: number | null;
  longitude_deg?: number | null;
  region?: string | null;
  transect_label?: string | null;
}

export interface EnvironmentTimeReference {
  month?: number | null;
  season?: string | null;
  label?: string | null;
  timestamp_utc?: string | null;
}

export interface WossCacheArtifact {
  path: string;
  size_bytes?: number | null;
  modified_at?: number | null;
}

export interface WossCacheSummary {
  cache_id: string;
  source_kind: string;
  target_kind: string;
  imported_at?: number | null;
  artifacts: {
    ssp_file?: WossCacheArtifact | null;
    bathymetry_file?: WossCacheArtifact | null;
  };
  location: EnvironmentGeoReference;
  time_reference: EnvironmentTimeReference;
  datasets: Record<string, string>;
  notes: string;
}

export interface WossCacheRecord {
  id: string;
  name: string;
  created_at: number;
  updated_at: number;
  imported_at: number;
  source_kind: string;
  target_kind: string;
  source_id?: string | null;
  profile_id?: string | null;
  artifacts: {
    ssp_file?: string | null;
    bathymetry_file?: string | null;
  };
  artifact_stats: {
    ssp_file?: WossCacheArtifact | null;
    bathymetry_file?: WossCacheArtifact | null;
  };
  location: EnvironmentGeoReference;
  time_reference: EnvironmentTimeReference;
  datasets: Record<string, string>;
  notes: string;
}

export interface EnvironmentDatabaseMetadata {
  schema_version: number;
  build_mode: string;
  sampling_profile?: string | null;
  engine_name?: string | null;
  engine_version?: string | null;
  engine_path?: string | null;
  at_compatibility?: string | null;
  arr_syntax?: string | null;
  data_source_type?: string | null;
  woss_source_id?: string | null;
  woss_profile_id?: string | null;
  validation_status?: string | null;
  validated_at?: number | null;
  has_real_arrivals?: boolean;
  dataset_versions?: Record<string, string>;
  geo_reference?: EnvironmentGeoReference;
  time_reference?: EnvironmentTimeReference;
  notes?: string | null;
  ssp_analysis?: {
    surface_sound_speed?: number | null;
    mld?: number | null;
    axis_depth?: number | null;
    bottom_depth?: number | null;
    thermocline_gradient_max?: number | null;
  };
  warnings?: string[];
  recommendations?: {
    ssp_depth_max_m?: number | null;
    bathymetry_range_max_m?: number | null;
    bathymetry_depth_max_m?: number | null;
    recommended_range_max_m?: number | null;
    recommended_depth_max_m?: number | null;
  };
}

export interface EnvironmentDatabaseBuildInfo {
  frequency_hz?: number | null;
  range_max_m?: number | null;
  range_step_m?: number | null;
  num_range_points?: number | null;
  depth_max_m?: number | null;
  water_depth_m?: number | null;
  source_depth_m?: number | null;
  source_depths?: number[];
  focus_depth_min_m?: number | null;
  focus_depth_max_m?: number | null;
  sound_speed_mps?: number | null;
  source_level_db?: number | null;
  spreading_factor?: number | null;
  absorption_db_per_km?: number | null;
  max_bounces?: number | null;
  receiver_depths: number[];
  run_type?: string | null;
}

export interface EnvironmentDatabase {
  id: string;
  name: string;
  description: string;
  created_at: number;
  updated_at: number;
  artifacts: EnvironmentDatabaseArtifacts;
  build: EnvironmentDatabaseBuildInfo;
  metadata: EnvironmentDatabaseMetadata;
  usage?: {
    scenario_ids: string[];
    user_scenario_ids?: string[];
    built_in_scenario_ids?: string[];
    template_ids: string[];
    reference_count: number;
    blocking_reference_count?: number;
    non_blocking_reference_count?: number;
    built_in_only?: boolean;
    in_use: boolean;
  };
}

export interface WossSourceVariant {
  id: string;
  name: string;
  description: string;
  artifacts: {
    ssp_file?: string | null;
    bathymetry_file?: string | null;
  };
  location: EnvironmentGeoReference;
  time_reference: EnvironmentTimeReference;
  datasets: Record<string, string>;
  notes: string;
  cache?: WossCacheSummary | null;
}

export interface WossSourceProfile {
  id: string;
  name: string;
  description: string;
  created_at: number;
  updated_at: number;
  provider: string;
  mode: string;
  artifacts: {
    ssp_file?: string | null;
    bathymetry_file?: string | null;
  };
  location: EnvironmentGeoReference;
  time_reference: EnvironmentTimeReference;
  datasets: Record<string, string>;
  profiles: WossSourceVariant[];
  notes: string;
  cache?: WossCacheSummary | null;
}

export interface EnvironmentCapabilities {
  bellhop: {
    configured_path?: string | null;
    resolved_path?: string | null;
    available: boolean;
    source: string;
  };
  bellhop_candidates?: Array<{
    path: string;
    source: string;
    label: string;
  }>;
  woss: {
    available: boolean;
    integration_mode: string;
    direct_run_supported: boolean;
    profiles_count: number;
    cache_records_count?: number;
    notes?: string;
  };
  counts: {
    environment_databases: number;
    ssp_files: number;
    bathymetry_files: number;
  };
}

export interface LinkMetric {
  tx_id: number;
  rx_id: number;
  time_s?: number;
  delay_s: number;
  receive_power_db: number;
  first_arrival_delay_s: number;
  received_level_db: number;
  pseudo_range_m: number;
  multipath_count: number;
  noise_level_db?: number;
  snr_db?: number;
  is_nlos?: number;
  arc_length_m?: number;
  nlos_bias_m?: number;
  tx_x?: number;
  tx_y?: number;
  tx_z?: number;
  rx_x?: number;
  rx_y?: number;
  rx_z?: number;
  tx_dr_x?: number;
  tx_dr_y?: number;
  tx_dr_z?: number;
  rx_dr_x?: number;
  rx_dr_y?: number;
  rx_dr_z?: number;
}

export interface BathymetryData {
  range_m: number[];
  depth_m: number[];
}

export interface StudioEnvironmentBounds {
  minX: number;
  maxX: number;
  minY: number;
  maxY: number;
  rangeMax: number;
  depthMax: number;
  waterDepth: number;
  editableDepthMax: number;
  depthConstraintReason?: string | null;
}

export interface RayComponent {
  delay_s: number;
  amplitude_db: number;
  surface_bounces: number;
  bottom_bounces: number;
  launch_angle_deg: number;
  arrival_angle_deg: number;
  path_length_m: number;
}

export interface LinkRays {
  tx_id: number;
  rx_id: number;
  tx_pos: [number, number, number];
  rx_pos: [number, number, number];
  is_nlos: number;
  nlos_no_direct: number;
  nlos_below_threshold: number;
  nlos_geometric: number;
  direct_ray_amplitude_db: number;
  rays: RayComponent[];
}

export interface EnvironmentPreviewRays {
  mode?: 'nearest' | 'interpolated';
  contributing_samples?: number;
  query_distance_m: number;
  nearest_range_m: number;
  range_bounds_m?: [number, number];
  query_source_depth_m: number;
  nearest_source_depth_m: number;
  source_depth_bounds_m?: [number, number];
  query_receiver_depth_m: number;
  nearest_receiver_depth_m: number;
  receiver_depth_bounds_m?: [number, number];
  rays: RayComponent[];
}

export interface TraceEventRecord {
  time_s: number;
  tx_time_s?: number;
  rx_time_s?: number;
  tx_id: number;
  rx_id: number;
  eventCode: string;
  layer?: string;
  reason: string;
  packet_size?: number;
  sequence?: number;
  delay_s?: number;
  received_level_db?: number;
  pseudo_range_m?: number;
  snr_db?: number;
  is_nlos?: number;
  within_range?: number;
  tx_x?: number;
  tx_y?: number;
  tx_z?: number;
  rx_x?: number;
  rx_y?: number;
  rx_z?: number;
  tx_dr_x?: number;
  tx_dr_y?: number;
  tx_dr_z?: number;
  rx_dr_x?: number;
  rx_dr_y?: number;
  rx_dr_z?: number;
}

export interface CommunicationEvent {
  id: string;
  frameIndex: number;
  time_s: number | null;
  tx_id: number;
  rx_id: number;
  anchorNodeId: number;
  edgeKey: string;
  label: string;
  eventCode: string;
  layer: string;
  sourceType: 'metrics' | 'trace';
  pattern: 'broadcast' | 'many_to_one' | 'one_to_one' | 'local';
  groupKey: string;
  groupSize: number;
  groupIndex: number;
  withinRange: boolean;
  reason?: string;
  tx_time_s?: number | null;
  rx_time_s?: number | null;
  packet_size?: number;
  sequence?: number;
  delay_s: number;
  received_level_db: number;
  pseudo_range_m: number;
  snr_db?: number;
  is_nlos: boolean;
}

export interface ExperimentTemplateBindings {
  transmission_type?: string | null;
  environment_database_id?: string | null;
  measurement_engine_name?: string | null;
}

export interface ExperimentTemplateRuntime {
  duration?: number | null;
  seed?: number | null;
  time_step_ms?: number | null;
  archive_experiment?: boolean | null;
  default_dashboard?: string | null;
}

export interface ExperimentTemplateSummary {
  node_count?: number | null;
  topology_type?: string | null;
  transmission_type?: string | null;
}

export interface ExperimentTemplate {
  id: string;
  name: string;
  description: string;
  created_at: number;
  updated_at: number;
  source_scenario_id?: string | null;
  tags: string[];
  bindings: ExperimentTemplateBindings;
  runtime: ExperimentTemplateRuntime;
  summary: ExperimentTemplateSummary;
  notes: string;
}

export interface DemoDataset {
  scenario: DemoScenario;
  metrics: LinkMetric[];
}

export interface AppCtx {
  dataset: DemoDataset;
  activeScenario: string;
  loadScenario: (name: string) => Promise<void>;
  /** 仿真完成后调用，将最新 metrics 推入全局 dataset，驱动所有依赖页面刷新 */
  notifyRunDone: (metrics: LinkMetric[]) => void;
  /** 每次仿真完成后递增，用于驱动 AssetsPage 历史列表重载 */
  runVersion: number;
}