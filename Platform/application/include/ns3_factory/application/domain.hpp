#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/application/ids.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::application {

using ResourceVersion = std::uint64_t;

struct EnvironmentReference final {
  std::string asset_id;
  std::uint32_t asset_format_version;

  auto operator==(const EnvironmentReference&) const -> bool = default;
};

enum class MobilityModel : std::uint8_t {
  kConstantVelocity = 1,
};

struct ScenarioReference final {
  ScenarioId scenario_id;
  ResourceVersion scenario_version;

  auto operator==(const ScenarioReference&) const -> bool = default;
};

struct ExperimentReference final {
  ExperimentId experiment_id;
  ResourceVersion experiment_version;

  auto operator==(const ExperimentReference&) const -> bool = default;
};

class ScenarioDefinition final {
 public:
  [[nodiscard]] static auto Create(
      ScenarioId scenario_id,
      ResourceVersion version,
      std::string name,
      std::vector<contracts::NodeCommittedState> nodes,
      EnvironmentReference environment,
      MobilityModel mobility_model,
      contracts::NodeId fusion_center_node_id)
      -> contracts::Result<ScenarioDefinition>;

  [[nodiscard]] constexpr auto scenario_id() const noexcept
      -> const ScenarioId& {
    return scenario_id_;
  }
  [[nodiscard]] constexpr auto version() const noexcept -> ResourceVersion {
    return version_;
  }
  [[nodiscard]] constexpr auto name() const noexcept -> const std::string& {
    return name_;
  }
  [[nodiscard]] auto nodes() const noexcept
      -> std::span<const contracts::NodeCommittedState> {
    return nodes_;
  }
  [[nodiscard]] constexpr auto environment() const noexcept
      -> const EnvironmentReference& {
    return environment_;
  }
  [[nodiscard]] constexpr auto mobility_model() const noexcept
      -> MobilityModel {
    return mobility_model_;
  }
  [[nodiscard]] constexpr auto fusion_center_node_id() const noexcept
      -> contracts::NodeId {
    return fusion_center_node_id_;
  }

  auto operator==(const ScenarioDefinition&) const -> bool = default;

 private:
  ScenarioDefinition(ScenarioId scenario_id,
                     ResourceVersion version,
                     std::string name,
                     std::vector<contracts::NodeCommittedState> nodes,
                     EnvironmentReference environment,
                     MobilityModel mobility_model,
                     contracts::NodeId fusion_center_node_id)
      : scenario_id_(std::move(scenario_id)),
        version_(version),
        name_(std::move(name)),
        nodes_(std::move(nodes)),
        environment_(std::move(environment)),
        mobility_model_(mobility_model),
        fusion_center_node_id_(fusion_center_node_id) {}

  ScenarioId scenario_id_;
  ResourceVersion version_;
  std::string name_;
  std::vector<contracts::NodeCommittedState> nodes_;
  EnvironmentReference environment_;
  MobilityModel mobility_model_;
  contracts::NodeId fusion_center_node_id_;
};

enum class RoutingMode : std::uint8_t {
  kDirectToFusionCenter = 1,
};

enum class MacMode : std::uint8_t {
  kTdma = 1,
};

enum class RxQualityMode : std::uint8_t {
  kNone = 1,
  kModeledBpskAwgn = 2,
};

enum class ApplicationWorkload : std::uint8_t {
  kAcceptanceBearingFusion = 1,
};

enum class AcceptanceProfile : std::uint8_t {
  kAcceptance4Node = 1,
  kExtended6Node = 2,
};

struct RoutingConfiguration final {
  RoutingMode mode;

  constexpr auto operator==(const RoutingConfiguration&) const noexcept
      -> bool = default;
};

struct MacConfiguration final {
  MacMode mode;
  contracts::SimDuration slot_duration;
  contracts::SimDuration guard_interval;

  constexpr auto operator==(const MacConfiguration&) const noexcept
      -> bool = default;
};

struct PhyConfiguration final {
  std::uint64_t bit_rate_bits_per_second;
  double center_frequency_hz;
  double occupied_bandwidth_hz;
  double source_level_db_re_1upa_at_1m;
  double equivalent_noise_power_db_re_1upa2;
  RxQualityMode quality_mode;

  auto operator==(const PhyConfiguration&) const -> bool = default;
};

struct FusionConfiguration final {
  ApplicationWorkload workload;
  AcceptanceProfile acceptance_profile;
  std::size_t minimum_bearing_points;
  contracts::SimDuration maximum_fusion_period;
  double target_x_meters;
  double target_y_meters;
  double maximum_ber;

  auto operator==(const FusionConfiguration&) const -> bool = default;
};

class ExperimentDefinition final {
 public:
  [[nodiscard]] static auto Create(
      ExperimentId experiment_id,
      ResourceVersion version,
      std::string name,
      ScenarioReference scenario,
      RoutingConfiguration routing,
      MacConfiguration mac,
      PhyConfiguration phy,
      FusionConfiguration fusion,
      std::size_t network_update_interval_cycles,
      std::size_t simulation_cycle_count,
      std::uint64_t deterministic_seed)
      -> contracts::Result<ExperimentDefinition>;

  [[nodiscard]] constexpr auto experiment_id() const noexcept
      -> const ExperimentId& {
    return experiment_id_;
  }
  [[nodiscard]] constexpr auto version() const noexcept -> ResourceVersion {
    return version_;
  }
  [[nodiscard]] constexpr auto name() const noexcept -> const std::string& {
    return name_;
  }
  [[nodiscard]] constexpr auto scenario() const noexcept
      -> const ScenarioReference& {
    return scenario_;
  }
  [[nodiscard]] constexpr auto routing() const noexcept
      -> const RoutingConfiguration& {
    return routing_;
  }
  [[nodiscard]] constexpr auto mac() const noexcept
      -> const MacConfiguration& {
    return mac_;
  }
  [[nodiscard]] constexpr auto phy() const noexcept
      -> const PhyConfiguration& {
    return phy_;
  }
  [[nodiscard]] constexpr auto fusion() const noexcept
      -> const FusionConfiguration& {
    return fusion_;
  }
  [[nodiscard]] constexpr auto network_update_interval_cycles()
      const noexcept -> std::size_t {
    return network_update_interval_cycles_;
  }
  [[nodiscard]] constexpr auto simulation_cycle_count() const noexcept
      -> std::size_t {
    return simulation_cycle_count_;
  }
  [[nodiscard]] constexpr auto deterministic_seed() const noexcept
      -> std::uint64_t {
    return deterministic_seed_;
  }

  auto operator==(const ExperimentDefinition&) const -> bool = default;

 private:
  ExperimentDefinition(
      ExperimentId experiment_id,
      ResourceVersion version,
      std::string name,
      ScenarioReference scenario,
      RoutingConfiguration routing,
      MacConfiguration mac,
      PhyConfiguration phy,
      FusionConfiguration fusion,
      std::size_t network_update_interval_cycles,
      std::size_t simulation_cycle_count,
      std::uint64_t deterministic_seed)
      : experiment_id_(std::move(experiment_id)),
        version_(version),
        name_(std::move(name)),
        scenario_(std::move(scenario)),
        routing_(routing),
        mac_(mac),
        phy_(phy),
        fusion_(fusion),
        network_update_interval_cycles_(network_update_interval_cycles),
        simulation_cycle_count_(simulation_cycle_count),
        deterministic_seed_(deterministic_seed) {}

  ExperimentId experiment_id_;
  ResourceVersion version_;
  std::string name_;
  ScenarioReference scenario_;
  RoutingConfiguration routing_;
  MacConfiguration mac_;
  PhyConfiguration phy_;
  FusionConfiguration fusion_;
  std::size_t network_update_interval_cycles_;
  std::size_t simulation_cycle_count_;
  std::uint64_t deterministic_seed_;
};

inline auto ScenarioDefinition::Create(
    ScenarioId scenario_id,
    ResourceVersion version,
    std::string name,
    std::vector<contracts::NodeCommittedState> nodes,
    EnvironmentReference environment,
    MobilityModel mobility_model,
    contracts::NodeId fusion_center_node_id)
    -> contracts::Result<ScenarioDefinition> {
  if(version == 0U || name.empty() || nodes.empty() ||
     environment.asset_id.empty() || environment.asset_format_version == 0U ||
     mobility_model != MobilityModel::kConstantVelocity) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Scenario definition metadata is invalid"});
  }
  const auto asset_id_valid =
      detail::ValidateStableIdText(environment.asset_id);
  if(!asset_id_valid) return std::unexpected(asset_id_valid.error());
  for(const auto& node : nodes) {
    const auto& position = node.motion.position;
    const auto& velocity = node.motion.velocity;
    if(!std::isfinite(position.x_meters) ||
       !std::isfinite(position.y_meters) ||
       !std::isfinite(position.z_meters) ||
       !std::isfinite(velocity.x_meters_per_second) ||
       !std::isfinite(velocity.y_meters_per_second) ||
       !std::isfinite(velocity.z_meters_per_second) ||
       (node.capability.duplex_mode != contracts::DuplexMode::kHalfDuplex &&
        node.capability.duplex_mode != contracts::DuplexMode::kFullDuplex)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Scenario node state or capability is invalid"});
    }
  }
  std::sort(nodes.begin(), nodes.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.node_id < rhs.node_id;
  });
  if(std::adjacent_find(nodes.begin(), nodes.end(), [](const auto& lhs,
                                                       const auto& rhs) {
       return lhs.node_id == rhs.node_id;
     }) != nodes.end()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "Scenario contains duplicate NodeId"});
  }
  const auto fusion_center = std::lower_bound(
      nodes.begin(),
      nodes.end(),
      fusion_center_node_id,
      [](const auto& node, contracts::NodeId id) {
        return node.node_id < id;
      });
  if(fusion_center == nodes.end() ||
     fusion_center->node_id != fusion_center_node_id ||
     !fusion_center->capability.can_receive) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Scenario fusion center is outside node universe"});
  }
  return ScenarioDefinition{std::move(scenario_id),
                            version,
                            std::move(name),
                            std::move(nodes),
                            std::move(environment),
                            mobility_model,
                            fusion_center_node_id};
}

inline auto ExperimentDefinition::Create(
    ExperimentId experiment_id,
    ResourceVersion version,
    std::string name,
    ScenarioReference scenario,
    RoutingConfiguration routing,
    MacConfiguration mac,
    PhyConfiguration phy,
    FusionConfiguration fusion,
    std::size_t network_update_interval_cycles,
    std::size_t simulation_cycle_count,
    std::uint64_t deterministic_seed)
    -> contracts::Result<ExperimentDefinition> {
  if(version == 0U || scenario.scenario_version == 0U || name.empty() ||
     routing.mode != RoutingMode::kDirectToFusionCenter ||
     mac.mode != MacMode::kTdma ||
     mac.slot_duration <= contracts::SimDuration::Zero() ||
     mac.guard_interval < contracts::SimDuration::Zero() ||
     phy.bit_rate_bits_per_second == 0U ||
     !std::isfinite(phy.center_frequency_hz) ||
     !std::isfinite(phy.occupied_bandwidth_hz) ||
     !std::isfinite(phy.source_level_db_re_1upa_at_1m) ||
     !std::isfinite(phy.equivalent_noise_power_db_re_1upa2) ||
     phy.center_frequency_hz <= 0.0 || phy.occupied_bandwidth_hz <= 0.0 ||
     (phy.quality_mode != RxQualityMode::kNone &&
      phy.quality_mode != RxQualityMode::kModeledBpskAwgn) ||
     fusion.workload != ApplicationWorkload::kAcceptanceBearingFusion ||
     (fusion.acceptance_profile != AcceptanceProfile::kAcceptance4Node &&
      fusion.acceptance_profile != AcceptanceProfile::kExtended6Node) ||
     fusion.minimum_bearing_points < 5U ||
     fusion.maximum_fusion_period <= contracts::SimDuration::Zero() ||
     !std::isfinite(fusion.target_x_meters) ||
     !std::isfinite(fusion.target_y_meters) ||
     !std::isfinite(fusion.maximum_ber) || fusion.maximum_ber <= 0.0 ||
     fusion.maximum_ber >= 1.0 || network_update_interval_cycles == 0U ||
     simulation_cycle_count == 0U) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Experiment definition is invalid"});
  }
  return ExperimentDefinition{std::move(experiment_id),
                              version,
                              std::move(name),
                              std::move(scenario),
                              routing,
                              mac,
                              phy,
                              fusion,
                              network_update_interval_cycles,
                              simulation_cycle_count,
                              deterministic_seed};
}

}  // namespace ns3_factory::application
