#pragma once
#include "frozen_bearing_inputs.hpp"

#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include <ns3_factory/trajectory/asset.hpp>
#include <ns3_factory/planning/registered_algorithms.hpp>

#include "internal/simulation_worker.hpp"
#include "planning_context_json.hpp"
#include "recorded_run_channel.hpp"

namespace ns3_factory::worker::adapter {

// V1 remains a compatibility decoder. V2 rejects lossy numeric conversions and
// unknown algorithm parameters before nlohmann's arithmetic get() casts run.
inline auto ValidatePlanningV2Document(const nlohmann::json& document,bool flow_scoped=false)
    -> contracts::Status {
  using Json = nlohmann::json;
  try {
    const auto require = [](bool condition, std::string_view field) {
      if(!condition) throw std::invalid_argument(
          "Invalid authored v2 field: " + std::string{field});
    };
    const auto integer = [&](const Json& value, std::uint64_t maximum,
                              std::string_view field) {
      if(value.is_number_unsigned()) {
        require(value.get<std::uint64_t>() <= maximum, field);
      } else {
        require(value.is_number_integer(), field);
        const auto number = value.get<std::int64_t>();
        require(number >= 0 && static_cast<std::uint64_t>(number) <= maximum, field);
      }
    };
    const auto real = [&](const Json& value, std::string_view field) {
      require(value.is_number() && std::isfinite(value.get<double>()), field);
    };
    const auto keys = [&](const Json& object,
                           std::initializer_list<std::string_view> allowed,
                           std::string_view field) {
      require(object.is_object(), field);
      for(const auto& item : object.items()) {
        require(std::find(allowed.begin(), allowed.end(), item.key()) != allowed.end(), field);
      }
    };
    constexpr auto u64 = std::numeric_limits<std::uint64_t>::max();
    constexpr auto i64 = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    constexpr auto count_max = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    keys(document, {"schema", "execution_mode", "scenario_id", "scenario_source_revision",
        "scenario_name", "spatial_domain", "experiment_id", "experiment_source_revision",
        "version", "environment", "fusion_center_node_id", "nodes", "trajectory_bindings",
        "trajectory_assets", "experiment", "recorded_channel", "recorded_proxy", "bearing_inputs"}, "document keys");
    integer(document.at("version"), u64, "version");
    for(const auto field : {"scenario_source_revision", "experiment_source_revision"})
      if(document.contains(field)) integer(document.at(field), u64, field);
    keys(document.at("environment"), {"asset_id", "format_version", "checksum"}, "environment keys");
    integer(document.at("environment").at("format_version"),
        std::numeric_limits<std::uint32_t>::max(), "environment format_version");
    if(!document.at("fusion_center_node_id").is_null())
      integer(document.at("fusion_center_node_id"), u64, "fusion_center_node_id");
    require(document.at("nodes").is_array(), "nodes");
    for(const auto& node : document.at("nodes")) {
      keys(node, {"node_id", "can_transmit", "can_receive", "duplex_mode", "x_m", "y_m", "z_m"}, "node keys");
      integer(node.at("node_id"), u64, "node_id");
      require(node.at("can_transmit").is_boolean() && node.at("can_receive").is_boolean(), "capabilities");
      require(node.at("duplex_mode") == "HalfDuplex" || node.at("duplex_mode") == "FullDuplex", "duplex_mode");
      for(const auto field : {"x_m", "y_m", "z_m"}) real(node.at(field), field);
    }
    const auto& experiment = document.at("experiment");
    keys(experiment, {"name", "routing_mode", "mac_mode", "workload", "physical_layer",
        "waveform_modulation", "waveform_noise", "waveform_noise_psd_db_re_1upa2_per_hz",
        "deterministic_seed", "slot_duration_ns", "guard_interval_ns", "bit_rate_bps",
        "center_frequency_hz", "occupied_bandwidth_hz", "source_level_db",
        "network_update_interval_cycles", "minimum_bearing_points", "maximum_fusion_period_ns",
        "target_x_m", "target_y_m", "maximum_ber", "planning_cycle_duration_ns", "adaptive_tdma",
        "flows", "fusion_receiver_node_id", "mac_algorithm", "osdm"}, "experiment keys");
    require(experiment.at("routing_mode") == "Direct" || experiment.at("routing_mode") == "ShortestPath", "routing_mode");
    require(experiment.at("workload") == "CommunicationOnly" || experiment.at("workload") == "BearingFusion", "workload");
    integer(experiment.at("planning_cycle_duration_ns"), i64, "planning_cycle_duration_ns");
    integer(experiment.at("bit_rate_bps"), u64, "bit_rate_bps");
    integer(experiment.at("network_update_interval_cycles"), count_max, "network_update_interval_cycles");
    integer(experiment.at("minimum_bearing_points"), count_max, "minimum_bearing_points");
    integer(experiment.at("maximum_fusion_period_ns"), i64, "maximum_fusion_period_ns");
    for(const auto field : {"center_frequency_hz", "occupied_bandwidth_hz", "source_level_db",
                           "target_x_m", "target_y_m", "maximum_ber"}) real(experiment.at(field), field);
    if(experiment.contains("deterministic_seed")) integer(experiment.at("deterministic_seed"), u64, "deterministic_seed");
    if(experiment.contains("physical_layer")) require(experiment.at("physical_layer") == "Channel1Abstract" ||
        experiment.at("physical_layer") == "Channel2Waveform" ||
        experiment.at("physical_layer") == "Channel3Recorded", "physical_layer");
    if(experiment.contains("waveform_modulation")) require(experiment.at("waveform_modulation") == "Bpsk" ||
        experiment.at("waveform_modulation") == "Bfsk" ||
        experiment.at("waveform_modulation") == "Osdm", "waveform_modulation");
    if(experiment.contains("waveform_noise")) require(experiment.at("waveform_noise") == "None" ||
        experiment.at("waveform_noise") == "Awgn", "waveform_noise");
    if(experiment.contains("waveform_noise_psd_db_re_1upa2_per_hz"))
      real(experiment.at("waveform_noise_psd_db_re_1upa2_per_hz"), "waveform_noise_psd_db_re_1upa2_per_hz");
    if(experiment.at("mac_mode") == "RegisteredMac") {
      require(!experiment.contains("slot_duration_ns") && !experiment.contains("guard_interval_ns") &&
          (!experiment.contains("adaptive_tdma") || experiment.at("adaptive_tdma").is_null()), "registered MAC legacy parameters");
      const auto& a = experiment.at("mac_algorithm");
      keys(a, {"id", "version", "package_sha256", "interface", "parameters", "worker_sha256"}, "mac_algorithm keys");
      require(a.at("interface") == (flow_scoped ? "planning-mac-v2":"planning-mac-v1"), "algorithm interface");
      require(a.at("worker_sha256").is_string() && a.at("worker_sha256").get<std::string>().size() == 64, "worker checksum");
      planning::MacAlgorithmConfiguration config{a.at("id").get<std::string>(), a.at("version").get<std::string>(),
          a.at("package_sha256").get<std::string>(), a.at("parameters").get<planning::Parameters>(),a.at("interface")};
      if(flow_scoped) {
        const auto* algorithm=planning::FindFlowAlgorithm(config);
        require(algorithm!=nullptr,"v2 algorithm package unavailable");
        const auto valid=planning::ValidateParameters(*algorithm,config.parameters);
        if(!valid) return std::unexpected(valid.error());
      } else {
        const auto* algorithm = planning::FindAlgorithm(config);
        require(algorithm != nullptr, "algorithm package unavailable");
        const auto valid = planning::ValidateParameters(*algorithm, config.parameters);
        if(!valid) return std::unexpected(valid.error());
      }
    } else if(experiment.at("mac_mode") == "AdaptiveTdma") {
      require(!experiment.contains("mac_algorithm"), "unexpected registered algorithm");
      require(!experiment.contains("slot_duration_ns") && !experiment.contains("guard_interval_ns"), "adaptive slot parameters");
      keys(experiment.at("adaptive_tdma"), {"safety_margin_ns"}, "adaptive_tdma keys");
      integer(experiment.at("adaptive_tdma").at("safety_margin_ns"), i64, "safety_margin_ns");
    } else {
      require(!experiment.contains("mac_algorithm"), "unexpected registered algorithm");
      require(experiment.at("mac_mode") == "Tdma", "mac_mode");
      require(!experiment.contains("adaptive_tdma") || experiment.at("adaptive_tdma").is_null(), "configured adaptive parameters");
      integer(experiment.at("slot_duration_ns"), i64, "slot_duration_ns");
      integer(experiment.at("guard_interval_ns"), i64, "guard_interval_ns");
    }
    require(experiment.at("flows").is_array(), "flows");
    for(const auto& flow : experiment.at("flows")) {
      if(flow_scoped) {
        keys(flow,{"flow_id","source_node_id","destination_node_id","packets_per_cycle"},"flow keys");
        integer(flow.at("flow_id"),u64,"flow_id");
      } else keys(flow, {"source_node_id", "destination_node_id", "packets_per_cycle"}, "flow keys");
      integer(flow.at("source_node_id"), u64, "flow source_node_id");
      integer(flow.at("destination_node_id"), u64, "flow destination_node_id");
      integer(flow.at("packets_per_cycle"), count_max, "packets_per_cycle");
    }
    if(!experiment.at("fusion_receiver_node_id").is_null())
      integer(experiment.at("fusion_receiver_node_id"), u64, "fusion_receiver_node_id");
    require(document.at("trajectory_assets").is_array(), "trajectory_assets");
    for(const auto& asset : document.at("trajectory_assets")) {
      keys(asset, {"asset_id", "version", "source", "checksum_sha256", "tracks"}, "trajectory asset keys");
      integer(asset.at("version"), u64, "trajectory version");
      require(asset.at("tracks").is_array(), "trajectory tracks");
      for(const auto& track : asset.at("tracks")) {
        keys(track, {"track_id", "points"}, "trajectory track keys");
        require(track.at("points").is_array(), "trajectory points");
        for(const auto& point : track.at("points")) {
          keys(point, {"time_ns", "x_m", "y_m", "z_m"}, "trajectory point keys");
          integer(point.at("time_ns"), i64, "trajectory time_ns");
          for(const auto field : {"x_m", "y_m", "z_m"}) real(point.at(field), field);
        }
      }
    }
    require(document.at("trajectory_bindings").is_array(), "trajectory_bindings");
    for(const auto& binding : document.at("trajectory_bindings")) {
      keys(binding, {"node_id", "asset_id", "version", "track_id"}, "trajectory binding keys");
      integer(binding.at("node_id"), u64, "binding node_id");
      integer(binding.at("version"), u64, "binding version");
    }
    return {};
  } catch(const std::exception& error) {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument, error.what()});
  }
}

// V3 is the first authored definition carrying stable FlowId. Keep old numeric
// JSON decoders unchanged; normalize v3's explicit decimal strings only here.
inline void NormalizePlanningV3Integers(nlohmann::json& value) {
  static const std::set<std::string> fields{"version","scenario_source_revision","experiment_source_revision",
      "format_version","node_id","source_node_id","destination_node_id","flow_id","packets_per_cycle",
      "fusion_center_node_id","fusion_receiver_node_id","time_ns","planning_cycle_duration_ns",
      "bit_rate_bps","network_update_interval_cycles","minimum_bearing_points","maximum_fusion_period_ns",
      "deterministic_seed","slot_duration_ns","guard_interval_ns","safety_margin_ns"};
  if(value.is_array()) for(auto& item:value) NormalizePlanningV3Integers(item);
  if(value.is_object()) for(auto& item:value.items()) {
    if(item.key()=="mac_algorithm" || item.key()=="bearing_inputs") continue; // independently versioned string boundaries
    if(fields.contains(item.key()) && !item.value().is_null()) item.value()=planning_json::Unsigned(item.value());
    else NormalizePlanningV3Integers(item.value());
  }
}

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
    auto document = json::parse(input);
    const bool proxy_mode=document.at("schema")=="ns3-factory-authored-run-v5";
    const bool recorded_mode=proxy_mode || document.at("schema")=="ns3-factory-authored-run-v4";
    if(recorded_mode!=(request.physical_layer==PhysicalLayerImplementation::kChannel3Recorded)||
       (recorded_mode && !proxy_mode)!=document.contains("recorded_channel") ||
       proxy_mode!=document.contains("recorded_proxy"))
      throw std::invalid_argument("Recorded mode requires matching v4/v5 definition and command");
    const bool flow_scoped=document.at("schema")=="ns3-factory-authored-run-v3";
    if(flow_scoped) {
      NormalizePlanningV3Integers(document);
      if(document.at("experiment").at("mac_mode")!="RegisteredMac")
        throw std::invalid_argument("Authored v3 requires explicit flow-scoped MAC");
    }
    const bool planning_v2 = recorded_mode || flow_scoped || document.at("schema") == "ns3-factory-authored-run-v2";
    if(planning_v2) {
      const auto valid = ValidatePlanningV2Document(document,flow_scoped);
      if(!valid) return std::unexpected(valid.error());
    }
    if((document.at("schema") != "ns3-factory-authored-run-v1" && !planning_v2) ||
       (planning_v2 && document.value("execution_mode", "") != "PlanningV1") ||
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
    std::optional<NodeId> legacy_center;
    if(!document.at("fusion_center_node_id").is_null())
      legacy_center = NodeId{document.at("fusion_center_node_id").get<std::uint64_t>()};
    if(!planning_v2 && !legacy_center) return std::unexpected(Error{ErrorCode::kInvalidArgument,
        "Legacy authored definition requires its explicit application receiver"});
    auto scenario = application::ScenarioDefinition::Create(
        request.scenario_id, request.definition_version,
        document.at("scenario_name").get<std::string>(), nodes,
        request.environment, application::MobilityModel::kTrajectoryDriven,
        legacy_center);
    if(!scenario) return std::unexpected(scenario.error());

    const auto& e = document.at("experiment");
    const auto routing_mode = e.at("routing_mode").get<std::string>();
    const auto mac_mode = e.at("mac_mode").get<std::string>();
    const auto workload = e.at("workload").get<std::string>();
    if((routing_mode != "DirectToFusionCenter" &&
        routing_mode != "ShortestPath" && !(planning_v2 && routing_mode == "Direct")) ||
       (mac_mode != "Tdma" && !(planning_v2 && (mac_mode == "AdaptiveTdma" || mac_mode == "RegisteredMac"))) ||
       (workload != "BearingFusion" && workload != "CommunicationOnly")) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument,
          "Authored Experiment selects an unsupported routing, MAC, or workload"});
    }
    std::optional<application::PlanningExecutionConfiguration> planning;
    if(planning_v2) {
      planning.emplace(application::PlanningExecutionConfiguration{
          SimDuration::FromNanoseconds(e.at("planning_cycle_duration_ns").get<std::int64_t>()),
          SimDuration::Zero(), {}, std::nullopt});
      if(mac_mode == "AdaptiveTdma") {
        if(e.contains("slot_duration_ns") || e.contains("guard_interval_ns"))
          return std::unexpected(Error{ErrorCode::kInvalidArgument,
              "Adaptive TDMA does not accept configured slot fields"});
        planning->safety_margin = SimDuration::FromNanoseconds(
            e.at("adaptive_tdma").at("safety_margin_ns").get<std::int64_t>());
      }
      if(mac_mode == "RegisteredMac") {
        const auto& a = e.at("mac_algorithm");
        planning->mac_algorithm.emplace(planning::MacAlgorithmConfiguration{
            a.at("id").get<std::string>(), a.at("version").get<std::string>(),
            a.at("package_sha256").get<std::string>(), a.at("parameters").get<planning::Parameters>(),a.at("interface")});
      }
      if(e.contains("fusion_receiver_node_id") && !e.at("fusion_receiver_node_id").is_null())
        planning->fusion_receiver_node_id = NodeId{e.at("fusion_receiver_node_id").get<std::uint64_t>()};
      for(const auto& flow : e.value("flows", json::array())) planning->flows.push_back({
          NodeId{flow.at("source_node_id").get<std::uint64_t>()},
          NodeId{flow.at("destination_node_id").get<std::uint64_t>()},
          flow.at("packets_per_cycle").get<std::size_t>(),flow_scoped
              ? std::optional{FlowId{flow.at("flow_id").get<std::uint64_t>()}} : std::nullopt});
    }
    auto experiment = application::ExperimentDefinition::Create(
        request.experiment_id, request.definition_version,
        e.at("name").get<std::string>(),
        {scenario->scenario_id(), scenario->version()},
        {routing_mode == "ShortestPath"
             ? application::RoutingMode::kShortestPath
             : routing_mode == "Direct" ? application::RoutingMode::kDirect
                                         : application::RoutingMode::kDirectToFusionCenter},
        {mac_mode == "RegisteredMac" ? application::MacMode::kRegistered :
            mac_mode == "AdaptiveTdma" ? application::MacMode::kAdaptiveTdma : application::MacMode::kTdma,
         SimDuration::FromNanoseconds(mac_mode != "Tdma" ? 0 : e.at("slot_duration_ns").get<std::int64_t>()),
         SimDuration::FromNanoseconds(mac_mode != "Tdma" ? 0 : e.at("guard_interval_ns").get<std::int64_t>())},
        {e.at("bit_rate_bps").get<std::uint64_t>(),
         e.at("center_frequency_hz").get<double>(),
         e.at("occupied_bandwidth_hz").get<double>(),
         e.at("source_level_db").get<double>(),
         request.equivalent_noise_power_db_re_1upa2, request.quality_mode},
        {workload == "CommunicationOnly"
             ? application::ApplicationWorkload::kCommunicationOnly
             : application::ApplicationWorkload::kBearingFusion,
         request.profile, planning_v2 ? e.at("minimum_bearing_points").get<std::size_t>()
                                     : e.value("minimum_bearing_points", 5U),
         SimDuration::FromNanoseconds(e.value("maximum_fusion_period_ns", 180000000000LL)),
         e.value("target_x_m", 200.0), e.value("target_y_m", 150.0),
         e.value("maximum_ber", 0.0001)},
        e.at("network_update_interval_cycles").get<std::size_t>(),
        request.simulation_cycle_count, request.deterministic_seed, std::move(planning));
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
    std::optional<phy::internal::OsdmConfig> osdm;
    if(request.waveform_modulation == WaveformModulation::kOsdm) {
      if(!planning_v2 || request.physical_layer == PhysicalLayerImplementation::kChannel1Abstract ||
         e.value("waveform_modulation", "") != "Osdm")
        throw std::invalid_argument("OSDM command/definition mismatch or unsupported execution mode");
      const auto& o = e.at("osdm");
      if(!o.is_object() || o.size() != 6 || !o.contains("constellation") ||
         (o.at("constellation") != "Bpsk" && o.at("constellation") != "Qpsk"))
        throw std::invalid_argument("Invalid OSDM configuration");
      for(const auto key : {"block_length", "vector_length", "cyclic_prefix", "maximum_sync_search"})
        if(!o.at(key).is_number_unsigned() || o.at(key).get<std::uint64_t>() == 0 ||
           o.at(key).get<std::uint64_t>() > 16384)
          throw std::invalid_argument("OSDM count exceeds explicit compute budget");
      if(!o.at("minimum_sync_correlation").is_number())
        throw std::invalid_argument("OSDM synchronization threshold must be numeric");
      osdm = phy::internal::OsdmConfig{o.at("block_length").get<std::size_t>(),
          o.at("vector_length").get<std::size_t>(), o.at("cyclic_prefix").get<std::size_t>(),
          o.at("constellation") == "Qpsk" ? phy::internal::OsdmConstellation::kQpsk : phy::internal::OsdmConstellation::kBpsk,
          true, o.at("minimum_sync_correlation").get<double>(), o.at("maximum_sync_search").get<std::size_t>()};
      auto valid = phy::internal::ValidateOsdmConfig(*osdm);
      if(!valid) return std::unexpected(valid.error());
    } else if(e.contains("osdm") || e.value("waveform_modulation", "") == "Osdm") {
      throw std::invalid_argument("Inactive or mismatched OSDM parameters");
    }
    std::shared_ptr<const phy::internal::RecordedWaveformChannel> recorded;
    if(recorded_mode) {
      if(e.at("physical_layer")!="Channel3Recorded"||e.at("workload")!="CommunicationOnly"||
         (e.at("mac_mode")!="AdaptiveTdma"&&e.at("mac_mode")!="Tdma")||
         e.at("routing_mode")!="Direct"||e.at("source_level_db")!=0||(!proxy_mode && nodes.size()!=2)||
         request.quality_mode!=application::RxQualityMode::kNone)
        throw std::invalid_argument("Recorded link experiment requires two endpoints, direct communication, relative unit amplitude and non-oracle TDMA");
      if(!proxy_mode) {
      const auto& rc=document.at("recorded_channel");
      if(rc.at("manifest").at("asset_id")!=request.environment.asset_id||request.environment.asset_format_version!=1)
        throw std::invalid_argument("Recorded source identity mismatch");
      const auto sender=planning_json::Unsigned(rc.at("transmitter_node_id"));
      const auto receiver=planning_json::Unsigned(rc.at("receiver_node_id"));
      for(const auto& node:nodes) {
        if((node.node_id.value()!=sender&&node.node_id.value()!=receiver)||
           node.capability.can_transmit!=(node.node_id.value()==sender)||
           node.capability.can_receive!=(node.node_id.value()==receiver))
          throw std::invalid_argument("Recorded directed link does not cover physical node capabilities");
      }
      } else if(!document.at("spatial_domain").is_null()) {
        throw std::invalid_argument("Proxy logical layout must not carry a spatial acoustic domain");
      }
      if(proxy_mode) for(const auto& n:document.at("nodes"))
        if(n.at("x_m")!=0 || n.at("y_m")!=0 || n.at("z_m")!=-1)
          throw std::invalid_argument("Proxy layout must be separate from fixed adapter positions");
      for(const auto& a:document.at("trajectory_assets")) for(const auto& track:a.at("tracks")) {
        const auto& first=track.at("points").at(0);
        for(const auto& point:track.at("points")) for(const auto axis:{"x_m","y_m","z_m"})
          if(point.at(axis)!=first.at(axis))throw std::invalid_argument("Recorded Doppler cannot be combined with synthetic trajectory motion");
      }
      const auto rate=static_cast<double>(experiment->phy().bit_rate_bits_per_second);
      const auto fs=osdm ? rate / *phy::internal::BitsPerOsdmSymbol(*osdm) : rate*100;
      auto loaded=proxy_mode
        ? LoadRecordedProxyChannel(path.parent_path().parent_path(),document.at("recorded_proxy"),fs,nodes)
        : LoadRecordedRunChannel(path.parent_path().parent_path(),document.at("recorded_channel"),fs);
      if(!loaded)return std::unexpected(loaded.error());
      recorded=std::move(*loaded);
    }
    std::map<NodeId,phy::internal::ModulationScheme> modulations;
    if(proxy_mode && document.at("recorded_proxy").contains("transmitter_modulations")) {
      const auto& entries=document.at("recorded_proxy").at("transmitter_modulations");
      if(!entries.is_array())throw std::invalid_argument("Proxy transmitter modulations must be an array");
      for(const auto& entry:entries) {
        if(entry.size()!=2 || !entry.contains("node_id") || !entry.contains("modulation"))
          throw std::invalid_argument("Invalid proxy node modem fields");
        const auto id=NodeId{planning_json::Unsigned(entry.at("node_id"))};
        const auto found=std::find_if(nodes.begin(),nodes.end(),[&](const auto& n){return n.node_id==id && n.capability.can_transmit;});
        if(found==nodes.end() || !osdm)
          throw std::invalid_argument("Mixed transmitter preset requires capable sender and base OSDM clock");
        const auto mode=entry.at("modulation").get<std::string>();
        if(mode!="Bpsk" && mode!="Bfsk" && mode!="Osdm")
          throw std::invalid_argument("Unknown proxy transmitter modulation");
        const auto fs=static_cast<double>(experiment->phy().bit_rate_bits_per_second)/
            *phy::internal::BitsPerOsdmSymbol(*osdm);
        if((mode=="Bpsk"||mode=="Bfsk") && (fs/100!=std::floor(fs/100) || fs<100))
          throw std::invalid_argument("Mixed modem nominal rate is not integral on common clock");
        if(!modulations.emplace(id,mode=="Bpsk" ? phy::internal::ModulationScheme::kBpsk :
            mode=="Bfsk" ? phy::internal::ModulationScheme::kBfsk : phy::internal::ModulationScheme::kOsdm).second)
          throw std::invalid_argument("Duplicate proxy sender modem");
      }
    }
    internal::RunExecutionDefinition result{
        std::move(*scenario), std::move(*experiment), std::move(*bound), std::move(osdm),
        std::move(recorded),std::move(modulations)};
    if(document.contains("bearing_inputs")) {
      auto inputs=BuildFrozenBearingInputs(result);
      if(!inputs)return std::unexpected(inputs.error());
      if(document.at("bearing_inputs")!=BearingInputJson(*inputs,*result.experiment.planning()->fusion_receiver_node_id))
        throw std::invalid_argument("Frozen bearing bytes do not match immutable source/target/trajectory");
      result.frozen_bearing_inputs=std::move(*inputs);
    }
    return result;
  } catch(const std::exception& error) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,
        std::string{"Invalid authored run definition: "} + error.what()});
  }
}

}  // namespace ns3_factory::worker::adapter
