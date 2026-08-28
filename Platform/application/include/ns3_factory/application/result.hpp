#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include <ns3_factory/application/domain.hpp>
#include <ns3_factory/application/run_events.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::application {

enum class RunLifecycle : std::uint8_t {
  kCreated = 1,
  kRunning = 2,
  kCompleted = 3,
  kFailed = 4,
};

struct RunFailureSummary final {
  contracts::ErrorCode code;
  std::string message;

  auto operator==(const RunFailureSummary&) const -> bool = default;
};

struct RunRecord final {
  RunId run_id;
  ExperimentReference experiment;
  ScenarioReference scenario;
  EnvironmentReference environment;
  RunLifecycle lifecycle;
  std::optional<contracts::SimTime> simulation_started_at;
  std::optional<contracts::SimTime> simulation_ended_at;
  std::optional<contracts::SnapshotVersion> final_snapshot_version;
  std::optional<RunFailureSummary> failure;
  std::optional<bool> event_stream_complete;

  auto operator==(const RunRecord&) const -> bool = default;
};

struct NodeSummary final {
  contracts::NodeId node_id;
  contracts::Position3d final_position;
  bool is_fusion_center;

  auto operator==(const NodeSummary&) const -> bool = default;
};

struct FusionResultSummary final {
  std::uint64_t fusion_sequence;
  contracts::SimTime started_at;
  contracts::SimTime completed_at;
  contracts::SimDuration fusion_period;
  std::size_t observation_count;
  double estimated_target_x_meters;
  double estimated_target_y_meters;

  auto operator==(const FusionResultSummary&) const -> bool = default;
};

struct RunProjectionSummary final {
  contracts::SimTime simulation_started_at;
  contracts::SimTime simulation_ended_at;
  contracts::SimDuration simulation_duration;
  contracts::SnapshotVersion final_snapshot_version;
  std::size_t cycle_count;
  std::size_t node_count;
  std::size_t transmission_count;
  std::size_t channel_signal_count;
  std::size_t channel_no_arrival_count;
  std::size_t reception_count;
  std::size_t local_delivery_count;

  auto operator==(const RunProjectionSummary&) const -> bool = default;
};

enum class MetricStatus : std::uint8_t {
  kPass = 1,
  kFail = 2,
  kNotEvaluated = 3,
};

enum class OverallStatus : std::uint8_t {
  kPass = 1,
  kFail = 2,
  kNotFullyEvaluated = 3,
};

struct AcceptanceReportSummary final {
  MetricStatus network_node_count;
  MetricStatus communication_rate;
  MetricStatus bit_error_rate;
  MetricStatus feature_level_fusion;
  MetricStatus bearing_point_count;
  MetricStatus fusion_period;
  OverallStatus overall;
  std::size_t evaluated_target_receptions;
  std::size_t missing_ber_evidence_count;
  std::optional<double> maximum_ber;
  std::optional<double> mean_ber;
  double required_maximum_ber;
  std::optional<std::size_t> minimum_bearing_points;
  std::size_t required_minimum_bearing_points;
  std::optional<contracts::SimDuration> maximum_fusion_period;
  contracts::SimDuration required_maximum_fusion_period;
  std::string ber_reason;

  auto operator==(const AcceptanceReportSummary&) const -> bool = default;
};

struct RunResult final {
  RunId run_id;
  RunProjectionSummary projection;
  std::optional<AcceptanceReportSummary> acceptance_report;
  std::vector<FusionResultSummary> fusion_results;
  std::vector<NodeSummary> nodes;

  auto operator==(const RunResult&) const -> bool = default;
};

class IRunExecutor {
 public:
  virtual ~IRunExecutor() = default;

  [[nodiscard]] virtual auto Execute(
      const RunId& run_id,
      const ScenarioDefinition& scenario,
      const ExperimentDefinition& experiment,
      contracts::ITraceSink& trace_sink) const
      -> contracts::Result<RunResult> = 0;
};

}  // namespace ns3_factory::application
