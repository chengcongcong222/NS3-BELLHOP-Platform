#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/application/result.hpp>
#include <ns3_factory/contracts/link_feasibility.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/trace.hpp>

#include "internal/acceptance_feature_application.hpp"
#include "internal/acceptance_run_report.hpp"
#include "internal/acceptance_scenario_config.hpp"
#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/communication_id_allocator.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_role_assignment_policy.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/connectivity_decision_policy.hpp"
#include "internal/direct_to_sink_routing_planner.hpp"
#include "internal/discrete_frequency_selector.hpp"
#include "internal/environment_asset_repository.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/rate_based_tx_phy.hpp"
#include "internal/scalar_ber_rx_phy.hpp"
#include "internal/scenario_runtime.hpp"
#include "internal/single_sink_star_topology_policy.hpp"
#include "internal/structure_builder.hpp"
#include "internal/world_state_store.hpp"

namespace ns3_factory::application::internal {

namespace acceptance_run_executor_detail {

using namespace contracts;

class RecordingTraceSink final : public ITraceSink {
 public:
  explicit RecordingTraceSink(ITraceSink& downstream) noexcept
      : downstream_(downstream) {}

  auto Emit(const TraceEvent& event) noexcept -> Status override {
    events.push_back(event);
    const auto ignored = downstream_.get().Emit(event);
    (void)ignored;
    return {};
  }

  std::vector<TraceEvent> events;

 private:
  std::reference_wrapper<ITraceSink> downstream_;
};

class FusionCenterEstimator final : public ILinkFeasibilityEstimator {
 public:
  explicit FusionCenterEstimator(NodeId fusion_center) noexcept
      : fusion_center_(fusion_center) {}

  auto Estimate(const LinkFeasibilityQuery& query) const
      -> Result<LinkFeasibilityEstimate> override {
    return LinkFeasibilityEstimate::Create(
        query.source_node_id(),
        query.target_node_id(),
        query.observed_at(),
        query.target_node_id() == fusion_center_ ? 1.0 : 0.0);
  }

 private:
  NodeId fusion_center_;
};

class AcceptancePlanner final
    : public planning::internal::IProtocolCyclePlanner {
 public:
  explicit AcceptancePlanner(
      const assembly::internal::AcceptanceScenarioConfig& config) noexcept
      : config_(config) {}

  auto Build(const WorldSnapshot& snapshot,
             const StructureSnapshot& structure) const
      -> Result<ProtocolCyclePlan> override {
    auto policy = planning::internal::ConfiguredTdmaPolicy::Create(
        config_.get().tdma_slot_duration(),
        config_.get().tdma_guard_interval(),
        std::vector<NodeId>{config_.get().sensor_node_ids().begin(),
                            config_.get().sensor_node_ids().end()});
    if(!policy) return std::unexpected(policy.error());
    const planning::internal::DirectToSinkRoutingPlanner routing;
    const planning::internal::ConfiguredTdmaMacPlanner mac{
        std::move(*policy)};
    return planning::internal::CompositeProtocolCyclePlanner{routing, mac}
        .Build(snapshot, structure);
  }

 private:
  std::reference_wrapper<
      const assembly::internal::AcceptanceScenarioConfig>
      config_;
};

class ConstantNoise final : public INoiseFieldProvider {
 public:
  explicit ConstantNoise(double power_db) noexcept : power_db_(power_db) {}

  auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(
        query.receiver_node_id(),
        query.observed_from(),
        query.observed_until(),
        query.lower_frequency_hz(),
        query.upper_frequency_hz(),
        power_db_);
  }

 private:
  double power_db_;
};

class DeterministicRxPhy final : public IRxPhy {
 public:
  auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& signal = request.receiver_window().desired_signal();
    return RxDecodeResult::Create(signal.transmission_id(),
                                  signal.emission().packet_id(),
                                  signal.receiver_node_id(),
                                  DecodeOutcome::kDecoded);
  }
};

[[nodiscard]] inline auto ToMetricStatus(
    assembly::internal::AcceptanceMetricStatus status) -> MetricStatus {
  switch(status) {
    case assembly::internal::AcceptanceMetricStatus::kPass:
      return MetricStatus::kPass;
    case assembly::internal::AcceptanceMetricStatus::kFail:
      return MetricStatus::kFail;
    case assembly::internal::AcceptanceMetricStatus::kNotEvaluated:
      return MetricStatus::kNotEvaluated;
  }
  return MetricStatus::kNotEvaluated;
}

[[nodiscard]] inline auto ToOverallStatus(
    assembly::internal::AcceptanceOverallStatus status) -> OverallStatus {
  switch(status) {
    case assembly::internal::AcceptanceOverallStatus::kPass:
      return OverallStatus::kPass;
    case assembly::internal::AcceptanceOverallStatus::kFail:
      return OverallStatus::kFail;
    case assembly::internal::AcceptanceOverallStatus::kNotFullyEvaluated:
      return OverallStatus::kNotFullyEvaluated;
  }
  return OverallStatus::kNotFullyEvaluated;
}

[[nodiscard]] inline auto ToAcceptanceSummary(
    const assembly::internal::AcceptanceRunReport& report)
    -> AcceptanceReportSummary {
  return AcceptanceReportSummary{
      ToMetricStatus(report.network_node_count.status),
      ToMetricStatus(report.communication_rate.status),
      ToMetricStatus(report.bit_error_rate.status),
      ToMetricStatus(report.feature_level_fusion.status),
      ToMetricStatus(report.bearing_point_count.status),
      ToMetricStatus(report.fusion_period.status),
      ToOverallStatus(report.overall_status),
      report.bit_error_rate.evaluated_receptions,
      report.bit_error_rate.missing_evidence_count,
      report.bit_error_rate.maximum_ber,
      report.bit_error_rate.mean_ber,
      report.bit_error_rate.required_maximum_ber,
      report.bearing_point_count.measured_minimum_per_result,
      report.bearing_point_count.required_minimum_per_result,
      report.fusion_period.measured_maximum_period,
      report.fusion_period.required_maximum_period,
      std::string{report.bit_error_rate.reason}};
}

[[nodiscard]] inline auto BuildAcceptanceConfig(
    const ScenarioDefinition& scenario,
    const ExperimentDefinition& experiment,
    const environment::internal::EnvironmentAssetId& environment_asset_id,
    double environment_depth_meters)
    -> Result<assembly::internal::AcceptanceScenarioConfig> {
  if(experiment.scenario().scenario_id != scenario.scenario_id() ||
     experiment.scenario().scenario_version != scenario.version()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Experiment does not reference the supplied Scenario version"});
  }
  std::vector<assembly::internal::AcceptanceNodeConfig> nodes;
  nodes.reserve(scenario.nodes().size());
  for(const auto& node : scenario.nodes()) {
    nodes.push_back(assembly::internal::AcceptanceNodeConfig{
        node,
        node.node_id == scenario.fusion_center_node_id()
            ? ProtocolRole::kSink
            : ProtocolRole::kMember});
  }
  return assembly::internal::AcceptanceScenarioConfig::Create(
      assembly::internal::AcceptanceScenarioParameters{
          experiment.fusion().acceptance_profile ==
                  AcceptanceProfile::kAcceptance4Node
              ? assembly::internal::AcceptanceScenarioProfile::kAcceptance4Node
              : assembly::internal::AcceptanceScenarioProfile::kExtended6Node,
          std::move(nodes),
          experiment.phy().bit_rate_bits_per_second,
          assembly::internal::kDetectionFeatureV1PayloadBytes,
          experiment.mac().slot_duration,
          experiment.mac().guard_interval,
          experiment.network_update_interval_cycles(),
          experiment.fusion().maximum_ber,
          experiment.fusion().minimum_bearing_points,
          experiment.fusion().maximum_fusion_period,
          experiment.fusion().target_x_meters,
          experiment.fusion().target_y_meters,
          experiment.phy().center_frequency_hz,
          experiment.phy().occupied_bandwidth_hz,
          experiment.phy().source_level_db_re_1upa_at_1m,
          environment_depth_meters,
          environment_asset_id});
}

}  // namespace acceptance_run_executor_detail

class AcceptanceRunExecutor final : public IRunExecutor {
 public:
  explicit AcceptanceRunExecutor(
      const environment::internal::EnvironmentAssetRepository& environments)
      noexcept
      : environments_(environments) {}

  [[nodiscard]] auto Execute(
      const RunId& run_id,
      const ScenarioDefinition& scenario,
      const ExperimentDefinition& experiment,
      contracts::ITraceSink& event_sink) const
      -> contracts::Result<RunResult> override {
    using namespace acceptance_run_executor_detail;
    auto asset_id = environment::internal::EnvironmentAssetId::Create(
        scenario.environment().asset_id);
    if(!asset_id) return std::unexpected(asset_id.error());
    auto asset = environments_.get().Load(*asset_id);
    if(!asset) {
      return std::unexpected(
          contracts::Error{asset.error().code,
                           "Environment asset missing: " +
                               asset.error().message});
    }
    if((*asset)->format_version() !=
       scenario.environment().asset_format_version) {
      return std::unexpected(
          contracts::Error{
              contracts::ErrorCode::kFailedPrecondition,
              "Environment asset format version does not match Scenario"});
    }
    const auto maximum_depth = std::max(
        (*asset)->source_depth_m().back(),
        (*asset)->receiver_depth_m().back());
    auto config = BuildAcceptanceConfig(
        scenario, experiment, *asset_id, maximum_depth);
    if(!config) return std::unexpected(config.error());
    auto frequency_policy =
        environment::internal::DiscreteFrequencySelectionPolicy::Create(0.0);
    if(!frequency_policy) return std::unexpected(frequency_policy.error());
    auto channel = environment::internal::AcousticFieldChannelProvider::Create(
        *asset, *frequency_policy);
    if(!channel) return std::unexpected(channel.error());
    auto initial_snapshot = config->InitialWorldSnapshot();
    if(!initial_snapshot) return std::unexpected(initial_snapshot.error());
    std::vector<contracts::NodeId> node_ids;
    for(const auto& node : initial_snapshot->nodes()) {
      node_ids.push_back(node.node_id);
    }
    auto queues = runtime::internal::PacketQueueStore::Create(node_ids);
    auto deliveries =
        runtime::internal::ApplicationDeliveryStore::Create(node_ids);
    auto connectivity =
        structure::internal::ConnectivityDecisionPolicy::Create(
            std::nullopt, 0.5, 0.5);
    auto tx_phy = phy::internal::RateBasedTxPhy::Create(
        phy::internal::RateBasedTxPhyConfig{
            experiment.phy().bit_rate_bits_per_second,
            experiment.phy().center_frequency_hz,
            experiment.phy().occupied_bandwidth_hz,
            experiment.phy().source_level_db_re_1upa_at_1m});
    if(!queues) return std::unexpected(queues.error());
    if(!deliveries) return std::unexpected(deliveries.error());
    if(!connectivity) return std::unexpected(connectivity.error());
    if(!tx_phy) return std::unexpected(tx_phy.error());
    auto feature_application =
        assembly::internal::AcceptanceFeatureApplication::Create(
            *config, *queues, *deliveries, contracts::SimTime::Zero());
    if(!feature_application) {
      return std::unexpected(feature_application.error());
    }

    kernel::internal::Ns3KernelGateway gateway;
    runtime::internal::WorldStateStore world{std::move(*initial_snapshot)};
    runtime::internal::CommunicationIdAllocator ids{
        contracts::TransmissionId{1'000}, contracts::ReceptionId{2'000}};
    FusionCenterEstimator estimator{config->fusion_center_node_id()};
    structure::internal::ConfiguredRoleAssignmentPolicy roles{
        config->RoleBindings()};
    structure::internal::SingleSinkStarTopologyPolicy topology;
    structure::internal::StructureBuilder structure_builder{
        *connectivity, estimator, roles, topology};
    AcceptancePlanner planner{*config};
    ConstantNoise noise{
        experiment.phy().equivalent_noise_power_db_re_1upa2};
    DeterministicRxPhy deterministic_rx;
    auto modeled_rx = phy::internal::ScalarBerRxPhyDecorator::Create(
        deterministic_rx, experiment.phy().bit_rate_bits_per_second);
    if(!modeled_rx) return std::unexpected(modeled_rx.error());
    const contracts::IRxPhy& rx_phy =
        experiment.phy().quality_mode == RxQualityMode::kModeledBpskAwgn
            ? static_cast<const contracts::IRxPhy&>(*modeled_rx)
            : static_cast<const contracts::IRxPhy&>(deterministic_rx);
    RecordingTraceSink trace{event_sink};
    assembly::internal::ScenarioRuntime runtime{
        gateway,
        world,
        *queues,
        *deliveries,
        ids,
        structure_builder,
        planner,
        *tx_phy,
        *channel,
        noise,
        rx_phy,
        contracts::PlanningCycleId{0},
        &trace,
        experiment.network_update_interval_cycles(),
        &*feature_application};
    const auto executed =
        runtime.RunCycles(experiment.simulation_cycle_count());
    if(!executed) return std::unexpected(executed.error());

    auto projection = assembly::internal::AcceptanceRunProjection::Build(
        *config,
        *tx_phy,
        trace.events,
        feature_application->fusion_result_store(),
        world.current_snapshot());
    if(!projection) return std::unexpected(projection.error());
    const auto report =
        assembly::internal::BuildAcceptanceRunReport(*projection);

    std::vector<FusionResultSummary> fusion_results;
    for(const auto& result : feature_application->fusion_results()) {
      fusion_results.push_back(FusionResultSummary{
          result.fusion_sequence,
          result.started_at,
          result.completed_at,
          result.fusion_period,
          result.observation_count,
          result.estimated_target_x_meters,
          result.estimated_target_y_meters});
    }
    std::vector<NodeSummary> nodes;
    for(const auto& node : world.current_snapshot().nodes()) {
      nodes.push_back(NodeSummary{
          node.node_id,
          node.motion.position,
          node.node_id == scenario.fusion_center_node_id()});
    }
    return RunResult{
        run_id,
        RunProjectionSummary{
            projection->run_started_at(),
            projection->run_ended_at(),
            projection->simulation_duration(),
            projection->final_snapshot_version(),
            projection->cycle_count(),
            projection->node_count(),
            projection->transmission_count(),
            projection->channel_signal_count(),
            projection->channel_no_arrival_count(),
            projection->reception_count(),
            projection->local_delivery_count()},
        report ? std::optional{ToAcceptanceSummary(*report)} : std::nullopt,
        std::move(fusion_results),
        std::move(nodes)};
  }

 private:
  std::reference_wrapper<
      const environment::internal::EnvironmentAssetRepository>
      environments_;
};

}  // namespace ns3_factory::application::internal
