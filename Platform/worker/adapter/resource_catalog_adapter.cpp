#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <span>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <ns3_factory/application/domain.hpp>
#include <ns3_factory/application/ids.hpp>

#include "internal/acceptance_preset.hpp"
#include "internal/environment_asset_repository.hpp"

namespace {

using Json = nlohmann::json;
using namespace ns3_factory;

template <typename Integer>
auto Decimal(Integer value) -> std::string {
  return std::to_string(value);
}

auto Checksum(std::uint64_t value) -> std::string {
  std::ostringstream stream;
  stream << std::hex << std::nouppercase << std::setfill('0') << std::setw(16)
         << value;
  return stream.str();
}

auto ProducerName(environment::internal::EnvironmentAssetProducerType type)
    -> std::string_view {
  using Type = environment::internal::EnvironmentAssetProducerType;
  switch(type) {
    case Type::kBellhopRawImport:
      return "BellhopRawImport";
    case Type::kManual:
      return "Manual";
    case Type::kMeasured:
      return "Measured";
    case Type::kFutureModel:
      return "FutureModel";
  }
  return "Unknown";
}

auto Axis(std::string_view unit, std::span<const double> values) -> Json {
  return Json{{"unit", unit},
              {"count", Decimal(values.size())},
              {"minimum", values.front()},
              {"maximum", values.back()}};
}

auto EnvironmentResource(
    const environment::internal::EnvironmentAssetRecord& record) -> Json {
  const auto& metadata = record.metadata;
  const auto& asset = *record.asset;
  const auto vertical =
      metadata.coordinate_frame.vertical_direction() ==
              environment::internal::VerticalAxisDirection::kPositiveUp
          ? "PositiveUp"
          : "PositiveDown";
  return Json{
      {"environment_asset_id", record.asset_id.value()},
      {"format", environment::internal::kAcousticFieldPackageFormat},
      {"package_format_version", Decimal(metadata.package_format_version)},
      {"asset_format_version", Decimal(metadata.asset_format_version)},
      {"provenance",
       {{"producer", ProducerName(metadata.provenance.producer_type())},
        {"created_by_build_version",
         metadata.provenance.created_by_build_version()},
        {"source_description", metadata.provenance.source_description()},
        {"raw_source_logical_name",
         metadata.provenance.raw_source_logical_name()},
        {"normalization_policy_version",
         metadata.provenance.normalization_policy_version()}}},
      {"coordinate_frame",
       {{"surface_z_meters", metadata.coordinate_frame.surface_z_meters()},
        {"vertical_axis", vertical}}},
      {"axes",
       {{"frequency", Axis("Hz", asset.frequency_hz())},
        {"source_depth", Axis("m", asset.source_depth_m())},
        {"receiver_depth", Axis("m", asset.receiver_depth_m())},
        {"horizontal_range", Axis("m", asset.horizontal_range_m())}}},
      {"cell_count", Decimal(metadata.cell_count)},
      {"signal_cell_count", Decimal(metadata.signal_cell_count)},
      {"no_arrival_cell_count", Decimal(metadata.no_arrival_cell_count)},
      {"payload_bytes", Decimal(metadata.payload_bytes)},
      {"checksum",
       {{"algorithm",
         environment::internal::kAcousticFieldPayloadChecksumAlgorithm},
        {"value", Checksum(metadata.payload_checksum)}}},
      {"validation_state", "Valid"}};
}

auto ScenarioResource(const application::ScenarioDefinition& scenario) -> Json {
  Json nodes = Json::array();
  for(const auto& node : scenario.nodes()) {
    nodes.push_back(
        {{"node_id", Decimal(node.node_id.value())},
         {"can_transmit", node.capability.can_transmit},
         {"can_receive", node.capability.can_receive},
         {"duplex_mode",
          node.capability.duplex_mode == contracts::DuplexMode::kHalfDuplex
              ? "HalfDuplex"
              : "FullDuplex"},
         {"initial_position",
          {{"x_meters", node.motion.position.x_meters},
           {"y_meters", node.motion.position.y_meters},
           {"z_meters", node.motion.position.z_meters}}},
         {"initial_velocity",
          {{"x_meters_per_second",
            node.motion.velocity.x_meters_per_second},
           {"y_meters_per_second",
            node.motion.velocity.y_meters_per_second},
           {"z_meters_per_second",
            node.motion.velocity.z_meters_per_second}}}});
  }
  return Json{
      {"scenario_id", scenario.scenario_id().value()},
      {"version", Decimal(scenario.version())},
      {"name", scenario.name()},
      {"nodes", std::move(nodes)},
      {"environment",
       {{"environment_asset_id", scenario.environment().asset_id},
        {"asset_format_version",
         Decimal(scenario.environment().asset_format_version)}}},
      {"mobility", Json{{"model", "ConstantVelocity"}}},
      {"fusion_center_node_id",
       Decimal(scenario.fusion_center_node_id().value())}};
}

auto ExperimentResource(const application::ExperimentDefinition& experiment)
    -> Json {
  const auto& mac = experiment.mac();
  const auto& phy = experiment.phy();
  const auto& fusion = experiment.fusion();
  return Json{
      {"experiment_id", experiment.experiment_id().value()},
      {"version", Decimal(experiment.version())},
      {"name", experiment.name()},
      {"scenario",
       {{"scenario_id", experiment.scenario().scenario_id.value()},
        {"version", Decimal(experiment.scenario().scenario_version)}}},
      {"routing", Json{{"mode", "DirectToFusionCenter"}}},
      {"mac",
       {{"mode", "Tdma"},
        {"slot_duration_ns", Decimal(mac.slot_duration.nanoseconds())},
        {"guard_interval_ns", Decimal(mac.guard_interval.nanoseconds())}}},
      {"phy",
       {{"bit_rate_bits_per_second",
         Decimal(phy.bit_rate_bits_per_second)},
        {"center_frequency_hz", phy.center_frequency_hz},
        {"occupied_bandwidth_hz", phy.occupied_bandwidth_hz},
        {"source_level_db_re_1upa_at_1m",
         phy.source_level_db_re_1upa_at_1m},
        {"equivalent_noise_power_db_re_1upa2",
         phy.equivalent_noise_power_db_re_1upa2},
        {"rx_quality_mode",
         phy.quality_mode == application::RxQualityMode::kNone
             ? "None"
             : "ModeledBpskAwgn"}}},
      {"fusion",
       {{"workload", "AcceptanceBearingFusion"},
        {"acceptance_profile",
         fusion.acceptance_profile ==
                 application::AcceptanceProfile::kAcceptance4Node
             ? "Acceptance4Node"
             : "Extended6Node"},
        {"minimum_bearing_points",
         Decimal(fusion.minimum_bearing_points)},
        {"maximum_fusion_period_ns",
         Decimal(fusion.maximum_fusion_period.nanoseconds())},
        {"maximum_ber", fusion.maximum_ber}}},
      {"network_update_interval_cycles",
       Decimal(experiment.network_update_interval_cycles())},
      {"simulation_cycle_count",
       Decimal(experiment.simulation_cycle_count())},
      {"deterministic_seed", Decimal(experiment.deterministic_seed())}};
}

auto Fail(std::string_view message) -> int {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

auto main(int argc, char** argv) -> int {
  using namespace ns3_factory;
  using namespace application;
  using namespace environment::internal;

  if(argc != 3) {
    return Fail("usage: resource_catalog_adapter REPOSITORY_ROOT ASSET_ID");
  }
  auto repository = EnvironmentAssetRepository::Open(std::filesystem::path{argv[1]});
  auto selected_id = EnvironmentAssetId::Create(argv[2]);
  if(!repository || !selected_id) {
    return Fail("resource catalog repository configuration is invalid");
  }
  auto selected = repository->Find(*selected_id);
  if(!selected) {
    return Fail("configured acceptance environment asset is unavailable");
  }

  Json environments = Json::array();
  auto environment_ids = repository->List();
  if(!environment_ids) return Fail("environment repository cannot be listed");
  for(const auto& environment_id : *environment_ids) {
    auto record = repository->Find(environment_id);
    if(!record) return Fail("environment repository contains an invalid package");
    environments.push_back(EnvironmentResource(*record));
  }

  const EnvironmentReference environment_ref{
      selected->asset_id.value(), selected->metadata.asset_format_version};
  auto acceptance_scenario_id = ScenarioId::Create("acceptance4-scenario");
  auto extended_scenario_id = ScenarioId::Create("extended6-scenario");
  auto acceptance_experiment_id = ExperimentId::Create("acceptance4-experiment");
  auto extended_experiment_id = ExperimentId::Create("extended6-experiment");
  if(!acceptance_scenario_id || !extended_scenario_id ||
     !acceptance_experiment_id || !extended_experiment_id) {
    return Fail("built-in resource identity is invalid");
  }

  auto acceptance_scenario = internal::MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kAcceptance4Node,
      *acceptance_scenario_id,
      1U,
      "Acceptance 4-Node Scenario",
      environment_ref);
  auto extended_scenario = internal::MakeAcceptanceScenarioPreset(
      AcceptanceProfile::kExtended6Node,
      *extended_scenario_id,
      1U,
      "Extended 6-Node Scenario",
      environment_ref);
  if(!acceptance_scenario || !extended_scenario) {
    return Fail("built-in scenario resource cannot be constructed");
  }

  auto acceptance_experiment = internal::MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kAcceptance4Node,
      *acceptance_experiment_id,
      1U,
      "Acceptance 4-Node Experiment",
      ScenarioReference{acceptance_scenario->scenario_id(),
                        acceptance_scenario->version()},
      2U,
      RxQualityMode::kModeledBpskAwgn,
      45.0,
      19U);
  auto extended_experiment = internal::MakeAcceptanceExperimentPreset(
      AcceptanceProfile::kExtended6Node,
      *extended_experiment_id,
      1U,
      "Extended 6-Node Experiment",
      ScenarioReference{extended_scenario->scenario_id(),
                        extended_scenario->version()},
      2U,
      RxQualityMode::kModeledBpskAwgn,
      45.0,
      23U);
  if(!acceptance_experiment || !extended_experiment) {
    return Fail("built-in experiment resource cannot be constructed");
  }

  Json document{
      {"schema_version", 1U},
      {"environments", std::move(environments)},
      {"scenarios",
       Json::array({ScenarioResource(*acceptance_scenario),
                    ScenarioResource(*extended_scenario)})},
      {"experiments",
       Json::array({ExperimentResource(*acceptance_experiment),
                    ExperimentResource(*extended_experiment)})}};
  std::cout << document.dump() << '\n';
  return std::cout ? EXIT_SUCCESS : EXIT_FAILURE;
}
