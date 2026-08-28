#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/application/domain.hpp>

#include "internal/acceptance_scenario_config.hpp"

namespace ns3_factory::application::internal {

[[nodiscard]] inline auto MakeAcceptanceScenarioPreset(
    AcceptanceProfile profile,
    ScenarioId scenario_id,
    ResourceVersion version,
    std::string name,
    EnvironmentReference environment)
    -> contracts::Result<ScenarioDefinition> {
  const auto config = profile == AcceptanceProfile::kAcceptance4Node
                          ? assembly::internal::MakeAcceptance4NodeConfig()
                          : assembly::internal::MakeExtended6NodeConfig();
  if(!config) return std::unexpected(config.error());
  std::vector<contracts::NodeCommittedState> nodes;
  nodes.reserve(config->nodes().size());
  for(const auto& node : config->nodes()) {
    nodes.push_back(node.initial_state);
  }
  return ScenarioDefinition::Create(std::move(scenario_id),
                                    version,
                                    std::move(name),
                                    std::move(nodes),
                                    std::move(environment),
                                    MobilityModel::kConstantVelocity,
                                    config->fusion_center_node_id());
}

[[nodiscard]] inline auto MakeAcceptanceExperimentPreset(
    AcceptanceProfile profile,
    ExperimentId experiment_id,
    ResourceVersion version,
    std::string name,
    ScenarioReference scenario,
    std::size_t simulation_cycle_count,
    RxQualityMode quality_mode = RxQualityMode::kModeledBpskAwgn,
    double equivalent_noise_power_db_re_1upa2 = 45.0,
    std::uint64_t deterministic_seed = 0U)
    -> contracts::Result<ExperimentDefinition> {
  const auto config = profile == AcceptanceProfile::kAcceptance4Node
                          ? assembly::internal::MakeAcceptance4NodeConfig()
                          : assembly::internal::MakeExtended6NodeConfig();
  if(!config) return std::unexpected(config.error());
  return ExperimentDefinition::Create(
      std::move(experiment_id),
      version,
      std::move(name),
      std::move(scenario),
      RoutingConfiguration{RoutingMode::kDirectToFusionCenter},
      MacConfiguration{MacMode::kTdma,
                       config->tdma_slot_duration(),
                       config->tdma_guard_interval()},
      PhyConfiguration{config->communication_rate_bits_per_second(),
                       config->center_frequency_hz(),
                       config->occupied_bandwidth_hz(),
                       config->source_level_db(),
                       equivalent_noise_power_db_re_1upa2,
                       quality_mode},
      FusionConfiguration{
          ApplicationWorkload::kAcceptanceBearingFusion,
          profile,
          config->minimum_bearing_points(),
          config->maximum_fusion_period(),
          config->target_x_meters(),
          config->target_y_meters(),
          config->ber_requirement()},
      config->network_update_interval_cycles(),
      simulation_cycle_count,
      deterministic_seed);
}

}  // namespace ns3_factory::application::internal
