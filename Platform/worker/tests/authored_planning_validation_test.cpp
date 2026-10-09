#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <unistd.h>

#include "adapter/authored_run_definition.hpp"
#include "internal/planning_execution.hpp"

using namespace ns3_factory;
using namespace contracts;
using namespace application;
using namespace application::internal;
using namespace worker;
using namespace worker::adapter;
using Json = nlohmann::json;

#define CHECK(condition) do { if(!(condition)) { std::cerr << "check failed at " << __LINE__ << '\n'; return false; } } while(false)

namespace {
auto BaseDocument() -> Json {
  Json document{
      {"schema", "ns3-factory-authored-run-v2"}, {"execution_mode", "PlanningV1"},
      {"scenario_id", "planning-scenario"}, {"scenario_name", "Explicit traffic"},
      {"experiment_id", "planning-experiment"}, {"version", 1},
      {"environment", {{"asset_id", "field-v1"}, {"format_version", 1}}},
      {"fusion_center_node_id", nullptr}, {"nodes", Json::array()},
      {"trajectory_assets", Json::array()}, {"trajectory_bindings", Json::array()},
      {"experiment", {
          {"name", "Adaptive traffic"}, {"routing_mode", "Direct"},
          {"mac_mode", "AdaptiveTdma"}, {"workload", "CommunicationOnly"},
          {"planning_cycle_duration_ns", 10'000'000'000LL},
          {"adaptive_tdma", {{"safety_margin_ns", 0}}},
          {"flows", Json::array({{{"source_node_id", 0}, {"destination_node_id", 1}, {"packets_per_cycle", 1}}})},
          {"fusion_receiver_node_id", nullptr}, {"bit_rate_bps", 60},
          {"center_frequency_hz", 25000.0}, {"occupied_bandwidth_hz", 4000.0},
          {"source_level_db", 110.0}, {"network_update_interval_cycles", 1},
          {"minimum_bearing_points", 5}, {"maximum_fusion_period_ns", 180'000'000'000LL},
          {"target_x_m", 200.0}, {"target_y_m", 150.0}, {"maximum_ber", 0.0001}}}};
  Json tracks = Json::array();
  for(int id = 0; id < 2; ++id) {
    const auto track = "track-" + std::to_string(id);
    document["nodes"].push_back({{"node_id", id}, {"can_transmit", true}, {"can_receive", true},
        {"duplex_mode", "HalfDuplex"}, {"x_m", id * 100.0}, {"y_m", 0.0}, {"z_m", -40.0}});
    document["trajectory_bindings"].push_back({{"node_id", id}, {"asset_id", "trajectory-v1"},
        {"version", 1}, {"track_id", track}});
    tracks.push_back({{"track_id", track}, {"points", Json::array({
        {{"time_ns", 0}, {"x_m", id * 100.0}, {"y_m", 0.0}, {"z_m", -40.0}},
        {{"time_ns", 10'000'000'000LL}, {"x_m", id * 100.0}, {"y_m", 0.0}, {"z_m", -40.0}}})}});
  }
  document["trajectory_assets"].push_back({{"asset_id", "trajectory-v1"}, {"version", 1},
      {"source", "validation fixture"}, {"tracks", std::move(tracks)}});
  return document;
}

class TemporaryDefinition final {
 public:
  TemporaryDefinition() {
    char pattern[] = "/tmp/ns3_factory_authored_validation_XXXXXX";
    const auto descriptor = mkstemp(pattern);
    if(descriptor < 0) throw std::runtime_error("Could not create test definition");
    close(descriptor);
    path_ = pattern;
  }
  ~TemporaryDefinition() { std::error_code ignored; std::filesystem::remove(path_, ignored); }
  auto Load(const Json& document) -> Result<worker::internal::RunExecutionDefinition> {
    {
      std::ofstream output{path_, std::ios::trunc};
      output << document.dump();
      if(!output) throw std::runtime_error("Could not write test definition");
    }
    const worker::internal::RunExecutionRequest request{
        *RunId::Create("planning-run"), *ScenarioId::Create("planning-scenario"),
        *ExperimentId::Create("planning-experiment"), 1, {"field-v1", 1},
        AcceptanceProfile::kAcceptance4Node, 1, RxQualityMode::kNone, 45.0, 42,
        PhysicalLayerImplementation::kChannel1Abstract, WaveformModulation::kBpsk,
        WaveformNoiseModel::kNone, 0.0};
    return LoadAuthoredRunDefinition(path_, request);
  }
 private:
  std::filesystem::path path_;
};

auto StrictV2AndCompatibleV1() -> bool {
  TemporaryDefinition fixture;
  const auto valid = BaseDocument();
  CHECK(fixture.Load(valid));
  const std::vector<std::function<void(Json&)>> mutations{
      [](Json& j) { j["experiment"]["planning_cycle_duration_ns"] = 1.5; },
      [](Json& j) { j["experiment"]["planning_cycle_duration_ns"] = std::numeric_limits<std::uint64_t>::max(); },
      [](Json& j) { j["experiment"]["planning_cycle_duration_ns"] = true; },
      [](Json& j) { j["experiment"]["bit_rate_bps"] = -1; },
      [](Json& j) { j["experiment"]["minimum_bearing_points"] = 5.5; },
      [](Json& j) { j["experiment"]["flows"][0]["packets_per_cycle"] = 1.9; },
      [](Json& j) { j["nodes"][0]["node_id"] = -1; },
      [](Json& j) { j["nodes"][0]["node_id"] = 0.5; },
      [](Json& j) { j["nodes"][0]["duplex_mode"] = "unknown"; },
      [](Json& j) { j["experiment"]["slot_duration_ns"] = 1; },
      [](Json& j) { j["experiment"]["adaptive_tdma"]["slots_per_cycle"] = 1; },
      [](Json& j) { j["experiment"]["mac_mode"] = "Tdma"; j["experiment"]["slot_duration_ns"] = 3'000'000'000LL; j["experiment"]["guard_interval_ns"] = 0; },
      [](Json& j) { j["experiment"]["flows"][0]["forwarding_node_id"] = 0; },
      [](Json& j) { j["trajectory_assets"][0]["tracks"][0]["points"][0]["time_ns"] = 0.1; },
      [](Json& j) { j["trajectory_assets"][0]["tracks"][0]["points"][0]["time_ns"] = std::numeric_limits<std::uint64_t>::max(); },
      [](Json& j) { j["trajectory_bindings"][0]["version"] = -1; },
      [](Json& j) { j["environment"]["format_version"] = 4'294'967'297ULL; },
      [](Json& j) { j["experiment"]["unexpected_mac_parameter"] = 0; },
      [](Json& j) { j["experiment"].erase("target_x_m"); }};
  for(std::size_t i = 0; i < mutations.size(); ++i) {
    auto invalid = valid;
    mutations[i](invalid);
    const auto rejected = fixture.Load(invalid);
    if(rejected) { std::cerr << "Accepted invalid v2 mutation " << i << '\n'; return false; }
  }
  auto configured = valid;
  configured["experiment"]["mac_mode"] = "Tdma";
  configured["experiment"]["adaptive_tdma"] = nullptr;
  configured["experiment"]["slot_duration_ns"] = 3'000'000'000LL;
  configured["experiment"]["guard_interval_ns"] = 0;
  CHECK(fixture.Load(configured));
  auto wide_count = valid;
  wide_count["experiment"]["minimum_bearing_points"] = 4'294'967'301ULL;
  const auto preserved = fixture.Load(wide_count);
  CHECK(preserved && preserved->experiment.fusion().minimum_bearing_points == 4'294'967'301ULL);
  auto legacy = configured;
  legacy["schema"] = "ns3-factory-authored-run-v1";
  legacy.erase("execution_mode");
  legacy["fusion_center_node_id"] = 1;
  legacy["nodes"][0]["duplex_mode"] = "legacy unknown value";
  auto& old = legacy["experiment"];
  old["routing_mode"] = "DirectToFusionCenter";
  old["minimum_bearing_points"] = 5.5;
  for(const auto key : {"planning_cycle_duration_ns", "adaptive_tdma", "flows", "fusion_receiver_node_id"}) old.erase(key);
  const auto compatible = fixture.Load(legacy);
  CHECK(compatible && !compatible->experiment.planning());
  CHECK(compatible->scenario.nodes().front().capability.duplex_mode == DuplexMode::kHalfDuplex);
  return true;
}

auto ConfiguredGuardUsesActualAirtime() -> bool {
  const auto duration = [](std::int64_t ns) { return SimDuration::FromNanoseconds(ns); };
  const auto experiment = ExperimentDefinition::Create(*ExperimentId::Create("guard-experiment"), 1,
      "Guard validation", {*ScenarioId::Create("guard-scenario"), 1}, {RoutingMode::kDirect},
      {MacMode::kTdma, duration(3'000'000'000), duration(2'000'000'000)},
      {60, 25000, 4000, 110, 45, RxQualityMode::kNone},
      {ApplicationWorkload::kCommunicationOnly, AcceptanceProfile::kAcceptance4Node,
       5, duration(180'000'000'000), 200, 150, 1e-4}, 1, 1, 42,
      PlanningExecutionConfiguration{duration(6'000'000'000), duration(0), {{NodeId{0}, NodeId{1}, 1}}, std::nullopt});
  const std::vector<NodeId> ids{NodeId{0}, NodeId{1}};
  const auto world = WorldSnapshot::Create(SnapshotVersion{0}, SimTime::Zero(), {
      {NodeId{0}, {true, true, DuplexMode::kHalfDuplex}, {{0, 0, -40}, {0, 0, 0}}},
      {NodeId{1}, {true, true, DuplexMode::kHalfDuplex}, {{100, 0, -40}, {0, 0, 0}}}});
  const auto graph = ConnectivityGraph::Create({{NodeId{0}, NodeId{1}}}, ids);
  const auto roles = RoleTable::Create({}, ids);
  const auto topology = LogicalTopology::Create({{NodeId{0}, NodeId{1}}}, ids, *graph);
  const auto structure = StructureSnapshot::Create(PlanningCycleId{0}, SnapshotVersion{0}, *roles, *graph, *topology);
  CHECK(experiment && world && structure);
  const FrameConfiguredMac planner{*experiment};
  for(const auto airtime : {1'000'000'000LL, 1'000'000'001LL, 2'000'000'000LL}) {
    const auto input = PlanningInput::Create(PlanningCycleId{0}, SnapshotVersion{0}, SimTime::Zero(),
        {{NodeId{0}, 1, 0, duration(airtime)}, {NodeId{1}, 0, 0, duration(airtime)}}, duration(500'000'000));
    CHECK(input);
    const auto result = planner.Build(*world, *structure, *input);
    if(airtime == 1'000'000'000LL) CHECK(result);
    else CHECK(!result && result.error().code == ErrorCode::kFailedPrecondition);
  }
  return true;
}
}  // namespace

int main() {
  return StrictV2AndCompatibleV1() && ConfiguredGuardUsesActualAirtime() ? EXIT_SUCCESS : EXIT_FAILURE;
}
