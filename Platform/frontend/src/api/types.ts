export type DecimalString = string;
export type Lifecycle = "Created" | "Running" | "Completed" | "Failed";
export type MetricStatus = "Pass" | "Fail" | "NotEvaluated";
export type OverallStatus = "Pass" | "Fail" | "NotFullyEvaluated";

export interface FailureDto {
  code: string;
  message: string;
}

export interface AxisDto {
  unit: "Hz" | "m";
  count: DecimalString;
  minimum: number;
  maximum: number;
}

export interface EnvironmentDto {
  environment_asset_id: string;
  format: string;
  package_format_version: DecimalString;
  asset_format_version: DecimalString;
  provenance: {
    producer: string;
    created_by_build_version: string;
    source_description: string;
    raw_source_logical_name: string;
    normalization_policy_version: string;
  };
  coordinate_frame: { surface_z_meters: number; vertical_axis: string };
  axes: {
    frequency: AxisDto;
    source_depth: AxisDto;
    receiver_depth: AxisDto;
    horizontal_range: AxisDto;
  };
  cell_count: DecimalString;
  signal_cell_count: DecimalString;
  no_arrival_cell_count: DecimalString;
  payload_bytes: DecimalString;
  checksum: { algorithm: string; value: string };
  validation_state: "Valid";
}

export interface ScenarioDto {
  scenario_id: string;
  version: DecimalString;
  name: string;
  nodes: Array<{
    node_id: DecimalString;
    can_transmit: boolean;
    can_receive: boolean;
    duplex_mode: string;
    initial_position: {
      x_meters: number;
      y_meters: number;
      z_meters: number;
    };
    initial_velocity: {
      x_meters_per_second: number;
      y_meters_per_second: number;
      z_meters_per_second: number;
    };
  }>;
  environment: {
    environment_asset_id: string;
    asset_format_version: DecimalString;
  };
  mobility: { model: string };
  fusion_center_node_id: DecimalString;
}

export interface ExperimentDto {
  experiment_id: string;
  version: DecimalString;
  name: string;
  scenario: { scenario_id: string; version: DecimalString };
  routing: { mode: string };
  mac: {
    mode: string;
    slot_duration_ns: DecimalString;
    guard_interval_ns: DecimalString;
  };
  phy: {
    bit_rate_bits_per_second: DecimalString;
    center_frequency_hz: number;
    occupied_bandwidth_hz: number;
    source_level_db_re_1upa_at_1m: number;
    equivalent_noise_power_db_re_1upa2: number;
    rx_quality_mode: "None" | "ModeledBpskAwgn";
  };
  fusion: {
    workload: string;
    acceptance_profile: string;
    minimum_bearing_points: DecimalString;
    maximum_fusion_period_ns: DecimalString;
    maximum_ber: number;
  };
  network_update_interval_cycles: DecimalString;
  simulation_cycle_count: DecimalString;
  deterministic_seed: DecimalString;
}

export interface RunDto {
  run_id: string;
  experiment_id: string;
  experiment_version: DecimalString;
  scenario_id: string;
  scenario_version: DecimalString;
  environment_asset_id: string;
  environment_format_version: DecimalString;
  lifecycle: Lifecycle;
  simulation_started_at_ns: DecimalString | null;
  simulation_ended_at_ns: DecimalString | null;
  final_snapshot_version: DecimalString | null;
  event_stream_complete: boolean | null;
  failure: FailureDto | null;
}

export interface RunSummaryDto {
  catalog_sequence: DecimalString;
  run_id: string;
  experiment_id: string;
  experiment_version: DecimalString;
  scenario_id: string;
  scenario_version: DecimalString;
  environment_asset_id: string;
  environment_format_version: DecimalString;
  lifecycle: Lifecycle;
  event_stream_complete: boolean | null;
  result_available: boolean;
  failure: FailureDto | null;
}

export interface AcceptanceReportDto {
  network_node_count: MetricStatus;
  communication_rate: MetricStatus;
  bit_error_rate: MetricStatus;
  feature_level_fusion: MetricStatus;
  bearing_point_count: MetricStatus;
  fusion_period: MetricStatus;
  overall: OverallStatus;
  evaluated_target_receptions: DecimalString;
  missing_ber_evidence_count: DecimalString;
  maximum_ber: number | null;
  mean_ber: number | null;
  required_maximum_ber: number;
  minimum_bearing_points: DecimalString | null;
  required_minimum_bearing_points: DecimalString;
  maximum_fusion_period_ns: DecimalString | null;
  required_maximum_fusion_period_ns: DecimalString;
  ber_reason: string;
}

export interface ResultDto {
  run_id: string;
  experiment_id: string;
  experiment_version: DecimalString;
  scenario_id: string;
  scenario_version: DecimalString;
  environment_asset_id: string;
  environment_format_version: DecimalString;
  projection: {
    simulation_started_at_ns: DecimalString;
    simulation_ended_at_ns: DecimalString;
    simulation_duration_ns: DecimalString;
    final_snapshot_version: DecimalString;
    cycle_count: DecimalString;
    node_count: DecimalString;
    transmission_count: DecimalString;
    channel_signal_count: DecimalString;
    channel_no_arrival_count: DecimalString;
    reception_count: DecimalString;
    local_delivery_count: DecimalString;
  };
  acceptance_report: AcceptanceReportDto | null;
  fusion_results: Array<{
    fusion_sequence: DecimalString;
    started_at_ns: DecimalString;
    completed_at_ns: DecimalString;
    fusion_period_ns: DecimalString;
    observation_count: DecimalString;
    estimated_target_x_meters: number;
    estimated_target_y_meters: number;
  }>;
  nodes: Array<{
    node_id: DecimalString;
    x_meters: number;
    y_meters: number;
    z_meters: number;
    is_fusion_center: boolean;
  }>;
}

export interface ResultSummaryDto {
  catalog_sequence: DecimalString;
  run_id: string;
  experiment_id: string;
  experiment_version: DecimalString;
  scenario_id: string;
  scenario_version: DecimalString;
  environment_asset_id: string;
  environment_format_version: DecimalString;
  acceptance_overall: OverallStatus | null;
  simulation_duration_ns: DecimalString;
  fusion_result_count: DecimalString;
}

export interface SystemInfoDto {
  schema_version: number;
  platform_name: string;
  platform_version: string;
  product_baseline: string;
  build: { source_revision: string; configuration: string; cxx_standard: string };
  simulation: { engine: string; version: string; time_authority: string; scheduler_authority: string; scheduling_gateway: string };
  interfaces: { api_schema_version: string; worker_wire_schema_version: string; acceptance_evidence_schema_version: string; frontend_release: string };
  runtime_mode: string;
}

export interface AcceptanceEvidenceDto {
  schema_version: 1;
  immutable_snapshot: true;
  baseline: {
    baseline_id: "Acceptance4Node";
    baseline_version: string;
    classification: string;
    hard_requirements: {
      network_node_count_minimum: string;
      network_node_count_maximum: string;
      communication_rate_bits_per_second: string;
      maximum_bit_error_rate: number;
      feature_level_fusion_required: boolean;
      minimum_bearing_points: string;
      maximum_fusion_period_ns: string;
    };
    demo_parameters: Record<string, unknown>;
  };
  manifest: { run_id: string; system: SystemInfoDto; environment: EnvironmentDto; scenario: ScenarioDto; experiment: ExperimentDto };
  run: { run_id: string; lifecycle: Lifecycle; event_stream_complete: boolean };
  projection: ResultDto["projection"];
  acceptance_report: AcceptanceReportDto;
  fusion_results: ResultDto["fusion_results"];
  nodes: ResultDto["nodes"];
  semantics: {
    verdict_origin: "BackendAcceptanceReport";
    ber_evidence_source: "Modeled" | "NotEvaluated";
    no_arrival: string;
    not_decoded: string;
    aggregate_policy: string;
  };
}

export interface CycleCommitTraceDto {
  occurred_at_ns: DecimalString;
  kind: "CycleCommit";
  payload: {
    cycle_id: DecimalString;
    base_snapshot_version: DecimalString;
    committed_snapshot_version: DecimalString;
    committed_at_ns: DecimalString;
  };
}

export interface TransmissionTraceDto {
  occurred_at_ns: DecimalString;
  kind: "Transmission";
  payload: {
    transmission_id: DecimalString;
    packet_id: DecimalString;
    sender_node_id: DecimalString;
    target:
      | { type: "Unicast"; node_id: DecimalString }
      | { type: "Broadcast" };
    started_at_ns: DecimalString;
    ended_at_ns: DecimalString;
  };
}

export interface ChannelOutcomeTraceDto {
  occurred_at_ns: DecimalString;
  kind: "ChannelOutcome";
  payload: {
    transmission_id: DecimalString;
    receiver_node_id: DecimalString;
    outcome:
      | {
          type: "Signal";
          first_arrival_delay_ns: DecimalString;
          aggregate_transmission_loss_db: number;
          path_count: DecimalString;
        }
      | { type: "NoArrival" };
  };
}

export interface ReceptionTraceDto {
  occurred_at_ns: DecimalString;
  kind: "Reception";
  payload: {
    reception_id: DecimalString;
    transmission_id: DecimalString;
    packet_id: DecimalString;
    receiver_node_id: DecimalString;
    disposition: "NotDecoded" | "Overheard" | "LocalDelivery" | "RelayEnqueue";
    quality: {
      signal_to_noise_ratio_db: number;
      eb_n0_db: number;
      bit_error_rate: number;
      source: "Modeled" | "Measured" | "External";
    } | null;
  };
}

export type TraceEventDto =
  | CycleCommitTraceDto
  | TransmissionTraceDto
  | ChannelOutcomeTraceDto
  | ReceptionTraceDto;

export interface RunEventDto {
  run_id: string;
  sequence: DecimalString;
  trace: TraceEventDto;
}
