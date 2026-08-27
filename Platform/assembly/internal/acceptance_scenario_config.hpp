#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/node_capability.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/environment_asset_repository.hpp"
#include "internal/rate_based_tx_phy.hpp"

namespace ns3_factory::assembly::internal {

enum class AcceptanceScenarioProfile : std::uint8_t {
  kAcceptance4Node = 1,
  kExtended6Node = 2,
};

struct AcceptanceNodeConfig final {
  contracts::NodeCommittedState initial_state;
  contracts::ProtocolRole role;

  constexpr auto operator==(const AcceptanceNodeConfig&) const
      -> bool = default;
};

struct AcceptanceScenarioParameters final {
  AcceptanceScenarioProfile profile;
  std::vector<AcceptanceNodeConfig> nodes;
  std::uint64_t communication_rate_bits_per_second;
  std::size_t maximum_planned_payload_bytes;
  contracts::SimDuration tdma_slot_duration;
  contracts::SimDuration tdma_guard_interval;
  std::size_t network_update_interval_cycles;
  double ber_requirement;
  std::size_t minimum_bearing_points;
  contracts::SimDuration maximum_fusion_period;
  double target_x_meters;
  double target_y_meters;
  double center_frequency_hz;
  double occupied_bandwidth_hz;
  double source_level_db;
  double environment_depth_meters;
  environment::internal::EnvironmentAssetId environment_asset_id;
};

class AcceptanceScenarioConfig final {
 public:
  [[nodiscard]] static auto Create(AcceptanceScenarioParameters parameters)
      -> contracts::Result<AcceptanceScenarioConfig>;

  [[nodiscard]] constexpr auto profile() const noexcept
      -> AcceptanceScenarioProfile {
    return parameters_.profile;
  }

  [[nodiscard]] auto nodes() const noexcept
      -> std::span<const AcceptanceNodeConfig> {
    return std::span<const AcceptanceNodeConfig>{parameters_.nodes};
  }

  [[nodiscard]] auto sensor_node_ids() const noexcept
      -> std::span<const contracts::NodeId> {
    return std::span<const contracts::NodeId>{sensor_node_ids_};
  }

  [[nodiscard]] constexpr auto fusion_center_node_id() const noexcept
      -> contracts::NodeId {
    return fusion_center_node_id_;
  }

  [[nodiscard]] constexpr auto communication_rate_bits_per_second()
      const noexcept -> std::uint64_t {
    return parameters_.communication_rate_bits_per_second;
  }

  [[nodiscard]] constexpr auto maximum_planned_payload_bytes() const noexcept
      -> std::size_t {
    return parameters_.maximum_planned_payload_bytes;
  }

  [[nodiscard]] constexpr auto maximum_packet_airtime() const noexcept
      -> contracts::SimDuration {
    return maximum_packet_airtime_;
  }

  [[nodiscard]] constexpr auto tdma_slot_duration() const noexcept
      -> contracts::SimDuration {
    return parameters_.tdma_slot_duration;
  }

  [[nodiscard]] constexpr auto tdma_guard_interval() const noexcept
      -> contracts::SimDuration {
    return parameters_.tdma_guard_interval;
  }

  [[nodiscard]] constexpr auto communication_cycle_duration() const noexcept
      -> contracts::SimDuration {
    return communication_cycle_duration_;
  }

  [[nodiscard]] constexpr auto network_update_interval_cycles() const noexcept
      -> std::size_t {
    return parameters_.network_update_interval_cycles;
  }

  [[nodiscard]] constexpr auto ber_requirement() const noexcept -> double {
    return parameters_.ber_requirement;
  }

  [[nodiscard]] constexpr auto minimum_bearing_points() const noexcept
      -> std::size_t {
    return parameters_.minimum_bearing_points;
  }

  [[nodiscard]] constexpr auto maximum_fusion_period() const noexcept
      -> contracts::SimDuration {
    return parameters_.maximum_fusion_period;
  }

  [[nodiscard]] constexpr auto target_x_meters() const noexcept -> double {
    return parameters_.target_x_meters;
  }

  [[nodiscard]] constexpr auto target_y_meters() const noexcept -> double {
    return parameters_.target_y_meters;
  }

  [[nodiscard]] constexpr auto center_frequency_hz() const noexcept
      -> double {
    return parameters_.center_frequency_hz;
  }

  [[nodiscard]] constexpr auto occupied_bandwidth_hz() const noexcept
      -> double {
    return parameters_.occupied_bandwidth_hz;
  }

  [[nodiscard]] constexpr auto source_level_db() const noexcept -> double {
    return parameters_.source_level_db;
  }

  [[nodiscard]] constexpr auto environment_depth_meters() const noexcept
      -> double {
    return parameters_.environment_depth_meters;
  }

  [[nodiscard]] constexpr auto environment_asset_id() const noexcept
      -> const environment::internal::EnvironmentAssetId& {
    return parameters_.environment_asset_id;
  }

  [[nodiscard]] constexpr auto average_initial_horizontal_range_meters()
      const noexcept -> double {
    return average_initial_horizontal_range_meters_;
  }

  [[nodiscard]] auto InitialWorldSnapshot() const
      -> contracts::Result<contracts::WorldSnapshot>;

  [[nodiscard]] auto RoleBindings() const
      -> std::vector<contracts::RoleBinding>;

  [[nodiscard]] auto TxPhyConfig() const noexcept
      -> phy::internal::RateBasedTxPhyConfig {
    return phy::internal::RateBasedTxPhyConfig{
        parameters_.communication_rate_bits_per_second,
        parameters_.center_frequency_hz,
        parameters_.occupied_bandwidth_hz,
        parameters_.source_level_db};
  }

 private:
  AcceptanceScenarioConfig(
      AcceptanceScenarioParameters parameters,
      std::vector<contracts::NodeId> sensor_node_ids,
      contracts::NodeId fusion_center_node_id,
      contracts::SimDuration maximum_packet_airtime,
      contracts::SimDuration communication_cycle_duration,
      double average_initial_horizontal_range_meters) noexcept
      : parameters_(std::move(parameters)),
        sensor_node_ids_(std::move(sensor_node_ids)),
        fusion_center_node_id_(fusion_center_node_id),
        maximum_packet_airtime_(maximum_packet_airtime),
        communication_cycle_duration_(communication_cycle_duration),
        average_initial_horizontal_range_meters_(
            average_initial_horizontal_range_meters) {}

  AcceptanceScenarioParameters parameters_;
  std::vector<contracts::NodeId> sensor_node_ids_;
  contracts::NodeId fusion_center_node_id_;
  contracts::SimDuration maximum_packet_airtime_;
  contracts::SimDuration communication_cycle_duration_;
  double average_initial_horizontal_range_meters_;
};

[[nodiscard]] auto Acceptance4NodeParameters()
    -> contracts::Result<AcceptanceScenarioParameters>;

[[nodiscard]] auto Extended6NodeParameters()
    -> contracts::Result<AcceptanceScenarioParameters>;

[[nodiscard]] inline auto MakeAcceptance4NodeConfig()
    -> contracts::Result<AcceptanceScenarioConfig> {
  auto parameters = Acceptance4NodeParameters();
  if(!parameters) return std::unexpected(parameters.error());
  return AcceptanceScenarioConfig::Create(std::move(*parameters));
}

[[nodiscard]] inline auto MakeExtended6NodeConfig()
    -> contracts::Result<AcceptanceScenarioConfig> {
  auto parameters = Extended6NodeParameters();
  if(!parameters) return std::unexpected(parameters.error());
  return AcceptanceScenarioConfig::Create(std::move(*parameters));
}

inline auto AcceptanceScenarioConfig::Create(
    AcceptanceScenarioParameters parameters)
    -> contracts::Result<AcceptanceScenarioConfig> {
  if(parameters.nodes.size() < 3 || parameters.nodes.size() > 6) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kOutOfRange,
            "Acceptance fixture supports between three and six nodes"});
  }
  if(parameters.communication_rate_bits_per_second == 0 ||
     parameters.maximum_planned_payload_bytes == 0) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Acceptance communication rate and maximum payload must be "
            "positive"});
  }
  if(parameters.tdma_guard_interval < contracts::SimDuration::Zero() ||
     parameters.tdma_slot_duration <= contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Acceptance TDMA durations are invalid"});
  }
  if(parameters.network_update_interval_cycles == 0) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Network update interval must contain at least one cycle"});
  }
  if(!std::isfinite(parameters.ber_requirement) ||
     parameters.ber_requirement <= 0.0 ||
     parameters.ber_requirement >= 1.0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "BER requirement must be finite and within (0, 1)"});
  }
  if(parameters.minimum_bearing_points < 5 ||
     parameters.maximum_fusion_period <= contracts::SimDuration::Zero() ||
     !std::isfinite(parameters.target_x_meters) ||
     !std::isfinite(parameters.target_y_meters)) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Acceptance fusion requirements and target must be valid"});
  }
  const auto tx_phy = phy::internal::RateBasedTxPhy::Create(
      phy::internal::RateBasedTxPhyConfig{
          parameters.communication_rate_bits_per_second,
          parameters.center_frequency_hz,
          parameters.occupied_bandwidth_hz,
          parameters.source_level_db});
  if(!tx_phy || !std::isfinite(parameters.environment_depth_meters) ||
     parameters.environment_depth_meters <= 0.0) {
    return std::unexpected(
        tx_phy ? contracts::Error{
                     contracts::ErrorCode::kOutOfRange,
                     "Acceptance environment depth must be positive and "
                     "finite"}
               : tx_phy.error());
  }

  auto airtime = phy::internal::ComputePayloadAirtime(
      parameters.maximum_planned_payload_bytes,
      parameters.communication_rate_bits_per_second);
  if(!airtime) return std::unexpected(airtime.error());
  const auto required_slot =
      contracts::CheckedAdd(*airtime, parameters.tdma_guard_interval);
  if(!required_slot) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Acceptance TDMA slot requirement overflowed"});
  }
  if(parameters.tdma_slot_duration < *required_slot) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "TDMA slot cannot contain maximum packet airtime and guard"});
  }

  std::vector<contracts::NodeCommittedState> states;
  std::vector<contracts::NodeId> sensor_node_ids;
  states.reserve(parameters.nodes.size());
  sensor_node_ids.reserve(parameters.nodes.size() - 1);
  std::optional<contracts::NodeId> fusion_center;
  contracts::Position3d fusion_position{};
  for(const auto& node : parameters.nodes) {
    const auto& state = node.initial_state;
    const auto finite_motion =
        std::isfinite(state.motion.position.x_meters) &&
        std::isfinite(state.motion.position.y_meters) &&
        std::isfinite(state.motion.position.z_meters) &&
        std::isfinite(state.motion.velocity.x_meters_per_second) &&
        std::isfinite(state.motion.velocity.y_meters_per_second) &&
        std::isfinite(state.motion.velocity.z_meters_per_second);
    if(!finite_motion) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Acceptance node motion must be finite"});
    }
    states.push_back(state);
    if(node.role == contracts::ProtocolRole::kSink) {
      if(fusion_center) {
        return std::unexpected(
            contracts::Error{
                contracts::ErrorCode::kFailedPrecondition,
                "Acceptance scenario requires exactly one fusion sink"});
      }
      if(!state.capability.can_receive ||
         state.motion.velocity != contracts::Velocity3d{0.0, 0.0, 0.0}) {
        return std::unexpected(
            contracts::Error{
                contracts::ErrorCode::kFailedPrecondition,
                "Fusion sink must receive and remain fixed"});
      }
      fusion_center = state.node_id;
      fusion_position = state.motion.position;
    } else {
      if(!state.capability.can_transmit) {
        return std::unexpected(
            contracts::Error{
                contracts::ErrorCode::kFailedPrecondition,
                "Acceptance sensor must be transmit-capable"});
      }
      sensor_node_ids.push_back(state.node_id);
    }
  }
  if(!fusion_center || sensor_node_ids.size() + 1 != states.size()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Acceptance scenario requires one sink and all other nodes as "
            "sensors"});
  }
  const auto world = contracts::WorldSnapshot::Create(
      contracts::SnapshotVersion{0},
      contracts::SimTime::Zero(),
      std::move(states));
  if(!world) return std::unexpected(world.error());
  std::sort(sensor_node_ids.begin(), sensor_node_ids.end());

  constexpr auto kMaximumNanoseconds =
      std::numeric_limits<contracts::NanosecondCount>::max();
  const auto slots = static_cast<contracts::NanosecondCount>(
      sensor_node_ids.size());
  if(parameters.tdma_slot_duration.nanoseconds() >
     kMaximumNanoseconds / slots) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Acceptance communication cycle duration overflowed"});
  }
  const auto cycle_duration = contracts::SimDuration::FromNanoseconds(
      parameters.tdma_slot_duration.nanoseconds() * slots);

  double range_sum = 0.0;
  for(const auto& node : parameters.nodes) {
    if(node.initial_state.node_id == *fusion_center) continue;
    const auto dx = node.initial_state.motion.position.x_meters -
                    fusion_position.x_meters;
    const auto dy = node.initial_state.motion.position.y_meters -
                    fusion_position.y_meters;
    range_sum += std::hypot(dx, dy);
  }
  const auto average_range =
      range_sum / static_cast<double>(sensor_node_ids.size());
  return AcceptanceScenarioConfig{std::move(parameters),
                                  std::move(sensor_node_ids),
                                  *fusion_center,
                                  *airtime,
                                  cycle_duration,
                                  average_range};
}

inline auto AcceptanceScenarioConfig::InitialWorldSnapshot() const
    -> contracts::Result<contracts::WorldSnapshot> {
  std::vector<contracts::NodeCommittedState> states;
  states.reserve(parameters_.nodes.size());
  for(const auto& node : parameters_.nodes) {
    states.push_back(node.initial_state);
  }
  return contracts::WorldSnapshot::Create(
      contracts::SnapshotVersion{0},
      contracts::SimTime::Zero(),
      std::move(states));
}

inline auto AcceptanceScenarioConfig::RoleBindings() const
    -> std::vector<contracts::RoleBinding> {
  std::vector<contracts::RoleBinding> bindings;
  bindings.reserve(parameters_.nodes.size());
  for(const auto& node : parameters_.nodes) {
    bindings.push_back(
        contracts::RoleBinding{node.initial_state.node_id, node.role});
  }
  return bindings;
}

namespace acceptance_config_detail {

inline constexpr auto kSensorSpeedMetersPerSecond = 5'000.0 / 3'600.0;

[[nodiscard]] inline auto Node(
    std::uint64_t id,
    contracts::Position3d position,
    contracts::Velocity3d velocity,
    contracts::ProtocolRole role) -> AcceptanceNodeConfig {
  const auto sink = role == contracts::ProtocolRole::kSink;
  return AcceptanceNodeConfig{
      contracts::NodeCommittedState{
          contracts::NodeId{id},
          contracts::NodeCapabilityProfile{
              !sink, true, contracts::DuplexMode::kHalfDuplex},
          contracts::MotionState{position, velocity}},
      role};
}

[[nodiscard]] inline auto BaseParameters(
    AcceptanceScenarioProfile profile,
    std::vector<AcceptanceNodeConfig> nodes)
    -> contracts::Result<AcceptanceScenarioParameters> {
  auto asset_id = environment::internal::EnvironmentAssetId::Create(
      profile == AcceptanceScenarioProfile::kAcceptance4Node
          ? "acceptance4-synthetic-field-v1"
          : "extended6-synthetic-field-v1");
  if(!asset_id) return std::unexpected(asset_id.error());
  return AcceptanceScenarioParameters{
      profile,
      std::move(nodes),
      60,
      15,
      contracts::SimDuration::FromNanoseconds(4'000'000'000),
      contracts::SimDuration::FromNanoseconds(2'000'000'000),
      10,
      1.0e-4,
      5,
      contracts::SimDuration::FromNanoseconds(180'000'000'000),
      200.0,
      150.0,
      25'000.0,
      4'000.0,
      110.0,
      75.0,
      std::move(*asset_id)};
}

}  // namespace acceptance_config_detail

inline auto Acceptance4NodeParameters()
    -> contracts::Result<AcceptanceScenarioParameters> {
  using acceptance_config_detail::kSensorSpeedMetersPerSecond;
  using acceptance_config_detail::Node;
  return acceptance_config_detail::BaseParameters(
      AcceptanceScenarioProfile::kAcceptance4Node,
      {Node(10,
            {1'000.0, 0.0, -60.0},
            {kSensorSpeedMetersPerSecond, 0.0, 0.0},
            contracts::ProtocolRole::kMember),
       Node(20,
            {-950.0, 250.0, -65.0},
            {0.0, kSensorSpeedMetersPerSecond, 0.0},
            contracts::ProtocolRole::kMember),
       Node(30,
            {0.0, -1'050.0, -55.0},
            {-kSensorSpeedMetersPerSecond, 0.0, 0.0},
            contracts::ProtocolRole::kMember),
       Node(99,
            {0.0, 0.0, -8.0},
            {0.0, 0.0, 0.0},
            contracts::ProtocolRole::kSink)});
}

inline auto Extended6NodeParameters()
    -> contracts::Result<AcceptanceScenarioParameters> {
  using acceptance_config_detail::kSensorSpeedMetersPerSecond;
  using acceptance_config_detail::Node;
  return acceptance_config_detail::BaseParameters(
      AcceptanceScenarioProfile::kExtended6Node,
      {Node(10,
            {1'000.0, 0.0, -60.0},
            {kSensorSpeedMetersPerSecond, 0.0, 0.0},
            contracts::ProtocolRole::kMember),
       Node(20,
            {-950.0, 250.0, -65.0},
            {0.0, kSensorSpeedMetersPerSecond, 0.0},
            contracts::ProtocolRole::kMember),
       Node(30,
            {0.0, -1'050.0, -55.0},
            {-kSensorSpeedMetersPerSecond, 0.0, 0.0},
            contracts::ProtocolRole::kMember),
       Node(40,
            {750.0, 700.0, -70.0},
            {0.0, -kSensorSpeedMetersPerSecond, 0.0},
            contracts::ProtocolRole::kMember),
       Node(50,
            {-700.0, -700.0, -58.0},
            {kSensorSpeedMetersPerSecond, 0.0, 0.0},
            contracts::ProtocolRole::kMember),
       Node(99,
            {0.0, 0.0, -8.0},
            {0.0, 0.0, 0.0},
            contracts::ProtocolRole::kSink)});
}

}  // namespace ns3_factory::assembly::internal
