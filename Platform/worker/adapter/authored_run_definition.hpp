#pragma once

#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <ns3_factory/trajectory/asset.hpp>

#include "internal/simulation_worker.hpp"

namespace ns3_factory::worker::adapter {

inline auto LoadAuthoredRunDefinition(
    const std::filesystem::path& path,
    const internal::RunExecutionRequest& request)
    -> contracts::Result<internal::RunExecutionDefinition> {
  using nlohmann::json;
  using namespace contracts;
  try {
    std::ifstream input{path};
    if(!input) return std::unexpected(Error{ErrorCode::kNotFound,
        "Authored run definition is unavailable"});
    const auto document = json::parse(input);
    if(document.at("schema") != "ns3-factory-authored-run-v1" ||
       document.at("scenario_id") != request.scenario_id.value() ||
       document.at("experiment_id") != request.experiment_id.value() ||
       document.at("version").get<std::uint64_t>() != request.definition_version ||
       document.at("environment").at("asset_id") != request.environment.asset_id ||
       document.at("environment").at("format_version").get<std::uint32_t>() !=
           request.environment.asset_format_version) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Authored run definition identity does not match command"});
    }
    std::vector<NodeCommittedState> nodes;
    for(const auto& value : document.at("nodes")) {
      nodes.push_back(NodeCommittedState{
          NodeId{value.at("node_id").get<std::uint64_t>()},
          {value.at("can_transmit").get<bool>(),
           value.at("can_receive").get<bool>(),
           value.at("duplex_mode") == "FullDuplex"
               ? DuplexMode::kFullDuplex : DuplexMode::kHalfDuplex},
          {{value.at("x_m").get<double>(), value.at("y_m").get<double>(),
            value.at("z_m").get<double>()}, {0.0, 0.0, 0.0}}});
    }
    auto scenario = application::ScenarioDefinition::Create(
        request.scenario_id, request.definition_version,
        document.at("scenario_name").get<std::string>(), nodes,
        request.environment, application::MobilityModel::kTrajectoryDriven,
        NodeId{document.at("fusion_center_node_id").get<std::uint64_t>()});
    if(!scenario) return std::unexpected(scenario.error());

    const auto& e = document.at("experiment");
    const auto routing_mode = e.at("routing_mode").get<std::string>();
    const auto mac_mode = e.at("mac_mode").get<std::string>();
    const auto workload = e.at("workload").get<std::string>();
    if((routing_mode != "DirectToFusionCenter" &&
        routing_mode != "ShortestPath") ||
       mac_mode != "Tdma" ||
       (workload != "BearingFusion" && workload != "CommunicationOnly")) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument,
          "Authored Experiment selects an unsupported routing, MAC, or workload"});
    }
    auto experiment = application::ExperimentDefinition::Create(
        request.experiment_id, request.definition_version,
        e.at("name").get<std::string>(),
        {scenario->scenario_id(), scenario->version()},
        {routing_mode == "ShortestPath"
             ? application::RoutingMode::kShortestPath
             : application::RoutingMode::kDirectToFusionCenter},
        {application::MacMode::kTdma,
         SimDuration::FromNanoseconds(e.at("slot_duration_ns").get<std::int64_t>()),
         SimDuration::FromNanoseconds(e.at("guard_interval_ns").get<std::int64_t>())},
        {e.at("bit_rate_bps").get<std::uint64_t>(),
         e.at("center_frequency_hz").get<double>(),
         e.at("occupied_bandwidth_hz").get<double>(),
         e.at("source_level_db").get<double>(),
         request.equivalent_noise_power_db_re_1upa2, request.quality_mode},
        {workload == "CommunicationOnly"
             ? application::ApplicationWorkload::kCommunicationOnly
             : application::ApplicationWorkload::kBearingFusion,
         request.profile, e.value("minimum_bearing_points", 5U),
         SimDuration::FromNanoseconds(e.value("maximum_fusion_period_ns", 180000000000LL)),
         e.value("target_x_m", 200.0), e.value("target_y_m", 150.0),
         e.value("maximum_ber", 0.0001)},
        e.at("network_update_interval_cycles").get<std::size_t>(),
        request.simulation_cycle_count, request.deterministic_seed);
    if(!experiment) return std::unexpected(experiment.error());

    trajectory::Repository repository;
    for(const auto& a : document.at("trajectory_assets")) {
      std::vector<trajectory::Track> tracks;
      contracts::SimTime start = SimTime::FromNanoseconds(0);
      contracts::SimTime end = start;
      for(const auto& t : a.at("tracks")) {
        std::vector<trajectory::Point> points;
        for(const auto& p : t.at("points")) {
          const auto time = SimTime::FromNanoseconds(p.at("time_ns").get<std::int64_t>());
          points.push_back({time, {p.at("x_m").get<double>(),
                                   p.at("y_m").get<double>(),
                                   p.at("z_m").get<double>()}});
        }
        if(points.empty()) return std::unexpected(Error{ErrorCode::kInvalidArgument,
            "Authored trajectory track is empty"});
        start = points.front().time; end = points.back().time;
        tracks.push_back({t.at("track_id").get<std::string>(), std::move(points)});
      }
      auto asset = trajectory::Asset::Create(
          a.at("asset_id").get<std::string>(), a.at("version").get<std::uint64_t>(),
          trajectory::CoordinateSystem::kLocalCartesianMetersZUp,
          start, end, a.at("source").get<std::string>(), std::move(tracks));
      if(!asset) return std::unexpected(asset.error());
      if(const auto registered = repository.Register(std::move(*asset)); !registered)
        return std::unexpected(registered.error());
    }
    std::vector<trajectory::NodeBinding> bindings;
    for(const auto& b : document.at("trajectory_bindings")) {
      bindings.push_back({NodeId{b.at("node_id").get<std::uint64_t>()},
          {b.at("asset_id").get<std::string>(), b.at("version").get<std::uint64_t>(),
           b.at("track_id").get<std::string>()}});
    }
    auto bound = trajectory::BoundTrajectories::Create(repository, std::move(bindings));
    if(!bound) return std::unexpected(bound.error());
    return internal::RunExecutionDefinition{
        std::move(*scenario), std::move(*experiment), std::move(*bound)};
  } catch(const std::exception& error) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,
        std::string{"Invalid authored run definition: "} + error.what()});
  }
}

}  // namespace ns3_factory::worker::adapter
