import type { ResultDto, RunDto, ScenarioDto } from "../api/types";
import { result, run, scenario } from "./fixtures";

export const acceptance4Scenario: ScenarioDto = {
  ...scenario,
  nodes: [
    {
      ...scenario.nodes[0],
      node_id: "0",
      can_transmit: true,
      initial_position: { x_meters: -500, y_meters: 0, z_meters: -80 },
      initial_velocity: { x_meters_per_second: 1, y_meters_per_second: 0, z_meters_per_second: 0 },
    },
    {
      ...scenario.nodes[0],
      node_id: "1",
      can_transmit: true,
      initial_position: { x_meters: 0, y_meters: 500, z_meters: -90 },
      initial_velocity: { x_meters_per_second: 0, y_meters_per_second: -1, z_meters_per_second: 0 },
    },
    {
      ...scenario.nodes[0],
      node_id: "2",
      can_transmit: true,
      initial_position: { x_meters: 500, y_meters: 0, z_meters: -100 },
      initial_velocity: { x_meters_per_second: -1, y_meters_per_second: 0, z_meters_per_second: 0 },
    },
    {
      ...scenario.nodes[0],
      node_id: "99",
      initial_position: { x_meters: 0, y_meters: 0, z_meters: -8 },
    },
  ],
};

export const acceptance4Run: RunDto = {
  ...run,
  run_id: "acceptance4-golden-run",
};

export const acceptance4Result: ResultDto = {
  ...result,
  run_id: acceptance4Run.run_id,
  projection: {
    ...result.projection,
    simulation_ended_at_ns: "180000000000",
    simulation_duration_ns: "180000000000",
    node_count: "4",
    transmission_count: "12",
    channel_signal_count: "9",
    channel_no_arrival_count: "3",
    reception_count: "9",
    local_delivery_count: "6",
  },
  acceptance_report: {
    ...result.acceptance_report!,
    network_node_count: "Pass",
    communication_rate: "Pass",
    bit_error_rate: "Pass",
    feature_level_fusion: "Pass",
    bearing_point_count: "Pass",
    fusion_period: "Pass",
    overall: "Pass",
    evaluated_target_receptions: "6",
    maximum_ber: 0.00008,
    mean_ber: 0.00004,
    minimum_bearing_points: "5",
    maximum_fusion_period_ns: "120000000000",
    required_maximum_fusion_period_ns: "180000000000",
  },
  fusion_results: [
    {
      fusion_sequence: "1",
      started_at_ns: "0",
      completed_at_ns: "120000000000",
      fusion_period_ns: "120000000000",
      observation_count: "5",
      estimated_target_x_meters: 120.5,
      estimated_target_y_meters: -45.25,
    },
  ],
  nodes: acceptance4Scenario.nodes.map((node) => ({
    node_id: node.node_id,
    ...node.initial_position,
    is_fusion_center: node.node_id === acceptance4Scenario.fusion_center_node_id,
  })),
};
