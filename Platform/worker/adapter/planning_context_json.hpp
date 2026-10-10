#pragma once
#include <charconv>
#include <nlohmann/json.hpp>
#include <ns3_factory/planning/sdk.hpp>

namespace ns3_factory::worker::adapter {
namespace planning_json {
using Json=nlohmann::json;
using namespace contracts;
inline auto Id(std::uint64_t value) -> Json { return std::to_string(value); }
inline auto Duration(SimDuration value) -> Json { return std::to_string(value.nanoseconds()); }
inline auto Time(SimTime value) -> Json { return std::to_string(value.nanoseconds()); }
inline auto Position(Position3d value) -> Json { return Json::array({value.x_meters,value.y_meters,value.z_meters}); }
inline auto Unsigned(const Json& value) -> std::uint64_t {
  if(!value.is_string()) throw std::invalid_argument("IDs, counts and nanoseconds must be decimal strings");
  const auto text=value.get<std::string>();
  std::uint64_t result{};
  const auto parsed=std::from_chars(text.data(),text.data()+text.size(),result);
  if(parsed.ec!=std::errc{} || parsed.ptr!=text.data()+text.size() || std::to_string(result)!=text)
    throw std::invalid_argument("Noncanonical or overflowing unsigned integer");
  return result;
}
inline auto ReadDuration(const Json& value) -> SimDuration {
  const auto count=Unsigned(value);
  if(count>static_cast<std::uint64_t>(INT64_MAX)) throw std::invalid_argument("Nanoseconds overflow");
  return SimDuration::FromNanoseconds(static_cast<std::int64_t>(count));
}
inline auto ReadTime(const Json& value) -> SimTime { return SimTime::FromNanoseconds(ReadDuration(value).nanoseconds()); }
inline auto Real(const Json& value) -> double {
  if(!value.is_number() || !std::isfinite(value.get<double>())) throw std::invalid_argument("Expected finite numeric value");
  return value.get<double>();
}
inline auto ReadPosition(const Json& value) -> Position3d {
  if(!value.is_array() || value.size()!=3) throw std::invalid_argument("Expected three position coordinates");
  return {Real(value[0]),Real(value[1]),Real(value[2])};
}
template<class T> inline auto Must(Result<T> result) -> T {
  if(!result) throw std::invalid_argument(result.error().message);
  return std::move(*result);
}
}  // namespace planning_json

inline auto PlanningContextJson(const contracts::PlanningContext& context) -> nlohmann::json {
  using namespace planning_json;
  const auto& d=context.data();
  Json result{{"schema","platform.planning-context.v2"},{"run_id",d.run_id},
      {"algorithm",{{"id",d.algorithm.id},{"version",d.algorithm.version},{"instance",d.algorithm.instance}}},
      {"fingerprint",Id(planning::ContextFingerprint(context))},{"cycle_id",Id(d.structure.cycle_id().value())},
      {"snapshot_version",Id(d.world.version().value())},{"observed_at_ns",Time(d.world.committed_at())},
      {"duration_ns",Duration(d.duration)},{"global_tail_ns",Duration(d.physical.maximum_propagation_tail())},
      {"deterministic_seed",Id(d.deterministic_seed)},{"effective_parameters",d.effective_parameters},
      {"structure_origin",{{"cycle_id",Id(d.structure_origin.cycle_id.value())},
       {"snapshot_version",Id(d.structure_origin.snapshot_version.value())},{"generated_at_ns",Time(d.structure_origin.generated_at)}}},
      {"budgets",{{"links",Id(d.budgets.maximum_links)},{"demands",Id(d.budgets.maximum_demands)},{"grants",Id(d.budgets.maximum_grants)}}}};
  for(const auto key:{"nodes","connectivity","topology","roles","routes","physical_demands","flow_demands","links"}) result[key]=Json::array();
  for(const auto& n:d.world.nodes()) result["nodes"].push_back({{"node_id",Id(n.node_id.value())},
      {"can_transmit",n.capability.can_transmit},{"can_receive",n.capability.can_receive},
      {"duplex",n.capability.duplex_mode==DuplexMode::kHalfDuplex ? "HalfDuplex":"FullDuplex"},
      {"position",Position(n.motion.position)},
      {"velocity",Json::array({n.motion.velocity.x_meters_per_second,n.motion.velocity.y_meters_per_second,n.motion.velocity.z_meters_per_second})}});
  for(const auto& e:d.structure.connectivity_graph().edges()) result["connectivity"].push_back(Json::array({Id(e.source_node_id.value()),Id(e.target_node_id.value())}));
  for(const auto& e:d.structure.logical_topology().links()) result["topology"].push_back(Json::array({Id(e.source_node_id.value()),Id(e.target_node_id.value())}));
  for(const auto& r:d.structure.role_table().bindings()) result["roles"].push_back(Json::array({Id(r.node_id.value()),Id(static_cast<std::uint64_t>(r.role))}));
  for(const auto& r:d.routing.entries()) result["routes"].push_back(Json::array({Id(r.forwarding_node_id.value()),Id(r.destination_node_id.value()),Id(r.next_hop_node_id.value())}));
  for(const auto& p:d.physical.demands()) result["physical_demands"].push_back({{"node_id",Id(p.node_id.value())},
      {"queued",Id(p.queued_packet_count)},{"ready",Id(p.ready_packet_count)},{"maximum_airtime_ns",Duration(p.maximum_packet_airtime)}});
  for(const auto& f:d.demands) result["flow_demands"].push_back({{"owner",Id(f.key.queue_owner_node_id.value())},
      {"flow_id",Id(f.key.flow_id.value())},{"source",Id(f.source_node_id.value())},{"destination",Id(f.destination_node_id.value())},
      {"next_hop",f.next_hop ? Id(f.next_hop->value()):Json{}},{"queued",Id(f.queued_packets)},
      {"ready",Id(f.ready_packets)},{"maximum_airtime_ns",Duration(f.maximum_airtime)}});
  for(const auto& l:d.links) result["links"].push_back({{"sender",Id(l.sender_node_id.value())},{"receiver",Id(l.receiver_node_id.value())},
      {"tx_position",Position(l.tx_position)},{"rx_position",Position(l.rx_position)},{"observed_at_ns",Time(l.observed_at)},
      {"frequency_hz",l.center_frequency_hz},{"bandwidth_hz",l.bandwidth_hz},{"environment_id",l.environment_id},
      {"environment_version",l.environment_version},{"environment_checksum",l.environment_checksum},{"method",l.method_version},
      {"status",l.status==LinkPredictionStatus::kAvailable ? "Available":l.status==LinkPredictionStatus::kNoArrival ? "NoArrival":"Unavailable"},
      {"loss_db",l.loss_db ? Json(*l.loss_db):Json{}},{"first_delay_ns",l.first_delay ? Duration(*l.first_delay):Json{}},
      {"maximum_excess_delay_ns",l.maximum_excess_delay ? Duration(*l.maximum_excess_delay):Json{}},
      {"path_count",l.path_count ? Id(*l.path_count):Json{}}});
  result["previous"]=nullptr;
  if(d.previous.cycle_id) {
    Json rows=Json::array();
    for(const auto& r:d.previous.links) rows.push_back({{"sender",Id(r.sender_node_id.value())},{"receiver",Id(r.receiver_node_id.value())},
        {"intended_target",r.intended_target},{"attempts",Id(r.attempts)},{"arrivals",Id(r.arrivals)},
        {"decoded",Id(r.decoded)},{"not_decoded",Id(r.not_decoded)},{"deliveries",Id(r.deliveries)},
        {"relays",Id(r.relays)},{"crc_passed",Id(r.crc_passed)},{"crc_failed",Id(r.crc_failed)},
        {"compared_bits",Id(r.compared_bits)},{"error_bits",Id(r.error_bits)}});
    result["previous"]={{"cycle_id",Id(d.previous.cycle_id->value())},{"committed_version",Id(d.previous.committed_version->value())},
        {"started_at_ns",Time(*d.previous.started_at)},{"ended_at_ns",Time(*d.previous.ended_at)},{"links",std::move(rows)}};
  }
  return result;
}

inline auto ReadPlanningContextJson(const nlohmann::json& input) -> contracts::Result<contracts::PlanningContext> {
  using namespace planning_json;
  try {
    if(input.at("schema")!="platform.planning-context.v2" || input.dump().size()>8U*1024U*1024U)
      throw std::invalid_argument("Unsupported or oversized context export");
    const auto cycle=PlanningCycleId{Unsigned(input.at("cycle_id"))};
    const auto version=SnapshotVersion{Unsigned(input.at("snapshot_version"))};
    const auto at=ReadTime(input.at("observed_at_ns"));
    std::vector<NodeId> ids;std::vector<NodeCommittedState> nodes;
    for(const auto& n:input.at("nodes")) {
      auto id=NodeId{Unsigned(n.at("node_id"))}; ids.push_back(id);
      const auto duplex=n.at("duplex").get<std::string>();
      if(duplex!="HalfDuplex" && duplex!="FullDuplex") throw std::invalid_argument("Unknown duplex mode");
      auto v=ReadPosition(n.at("velocity"));
      nodes.push_back({id,{n.at("can_transmit").get<bool>(),n.at("can_receive").get<bool>(),
          duplex=="HalfDuplex" ? DuplexMode::kHalfDuplex:DuplexMode::kFullDuplex},
          {ReadPosition(n.at("position")),{v.x_meters,v.y_meters,v.z_meters}}});
    }
    auto world=Must(WorldSnapshot::Create(version,at,nodes));
    std::vector<DirectedLink> edges;std::vector<LogicalLink> links;std::vector<RoleBinding> bindings;std::vector<RouteEntry> routes;
    for(const auto& e:input.at("connectivity")) edges.push_back({NodeId{Unsigned(e.at(0))},NodeId{Unsigned(e.at(1))}});
    for(const auto& e:input.at("topology")) links.push_back({NodeId{Unsigned(e.at(0))},NodeId{Unsigned(e.at(1))}});
    for(const auto& b:input.at("roles")) {
      const auto role=Unsigned(b.at(1));if(role<1 || role>6) throw std::invalid_argument("Unknown protocol role");
      bindings.push_back({NodeId{Unsigned(b.at(0))},static_cast<ProtocolRole>(role)});
    }
    auto graph=Must(ConnectivityGraph::Create(edges,ids));
    auto topology=Must(LogicalTopology::Create(links,ids,graph));
    auto structure=Must(StructureSnapshot::Create(cycle,version,Must(RoleTable::Create(bindings,ids)),graph,topology));
    for(const auto& r:input.at("routes")) routes.push_back({NodeId{Unsigned(r.at(0))},NodeId{Unsigned(r.at(1))},NodeId{Unsigned(r.at(2))}});
    auto routing=Must(RoutingPlan::Create(routes,structure));
    std::vector<NodeTrafficDemand> physical;
    for(const auto& p:input.at("physical_demands")) physical.push_back({NodeId{Unsigned(p.at("node_id"))},
        Unsigned(p.at("queued")),Unsigned(p.at("ready")),ReadDuration(p.at("maximum_airtime_ns"))});
    auto pinput=Must(PlanningInput::Create(cycle,version,at,physical,ReadDuration(input.at("global_tail_ns"))));
    std::vector<FlowDemand> demands;
    for(const auto& f:input.at("flow_demands")) demands.push_back({{NodeId{Unsigned(f.at("owner"))},FlowId{Unsigned(f.at("flow_id"))}},
        NodeId{Unsigned(f.at("source"))},NodeId{Unsigned(f.at("destination"))},
        f.at("next_hop").is_null() ? std::nullopt:std::optional{NodeId{Unsigned(f.at("next_hop"))}},
        Unsigned(f.at("queued")),Unsigned(f.at("ready")),ReadDuration(f.at("maximum_airtime_ns"))});
    std::vector<LinkPrediction> predictions;
    for(const auto& l:input.at("links")) {
      const auto status=l.at("status").get<std::string>();
      if(status!="Available" && status!="NoArrival" && status!="Unavailable") throw std::invalid_argument("Unknown prediction status");
      predictions.push_back({NodeId{Unsigned(l.at("sender"))},NodeId{Unsigned(l.at("receiver"))},
          ReadPosition(l.at("tx_position")),ReadPosition(l.at("rx_position")),ReadTime(l.at("observed_at_ns")),
          Real(l.at("frequency_hz")),Real(l.at("bandwidth_hz")),l.at("environment_id"),l.at("environment_version"),
          l.at("environment_checksum"),l.at("method"),status=="Available" ? LinkPredictionStatus::kAvailable:
          status=="NoArrival" ? LinkPredictionStatus::kNoArrival:LinkPredictionStatus::kUnavailable,
          l.at("loss_db").is_null() ? std::nullopt:std::optional{Real(l.at("loss_db"))},
          l.at("first_delay_ns").is_null() ? std::nullopt:std::optional{ReadDuration(l.at("first_delay_ns"))},
          l.at("maximum_excess_delay_ns").is_null() ? std::nullopt:std::optional{ReadDuration(l.at("maximum_excess_delay_ns"))},
          l.at("path_count").is_null() ? std::nullopt:std::optional{Unsigned(l.at("path_count"))}});
    }
    PreviousCycleFeedback previous;
    const auto& p=input.at("previous");
    if(!p.is_null()) {
      previous={PlanningCycleId{Unsigned(p.at("cycle_id"))},SnapshotVersion{Unsigned(p.at("committed_version"))},
          ReadTime(p.at("started_at_ns")),ReadTime(p.at("ended_at_ns")),{}};
      for(const auto& r:p.at("links")) previous.links.push_back({NodeId{Unsigned(r.at("sender"))},NodeId{Unsigned(r.at("receiver"))},
          r.at("intended_target").get<bool>(),Unsigned(r.at("attempts")),Unsigned(r.at("arrivals")),Unsigned(r.at("decoded")),
          Unsigned(r.at("not_decoded")),Unsigned(r.at("deliveries")),Unsigned(r.at("relays")),Unsigned(r.at("crc_passed")),
          Unsigned(r.at("crc_failed")),Unsigned(r.at("compared_bits")),Unsigned(r.at("error_bits"))});
    }
    const auto& origin=input.at("structure_origin");const auto& budgets=input.at("budgets");const auto& algorithm=input.at("algorithm");
    auto context=Must(PlanningContext::Create({input.at("run_id"),world,structure,routing,pinput,
        {PlanningCycleId{Unsigned(origin.at("cycle_id"))},SnapshotVersion{Unsigned(origin.at("snapshot_version"))},ReadTime(origin.at("generated_at_ns"))},
        ReadDuration(input.at("duration_ns")),demands,predictions,previous,
        input.at("effective_parameters").get<std::map<std::string,std::string>>(),Unsigned(input.at("deterministic_seed")),
        {Unsigned(budgets.at("links")),Unsigned(budgets.at("demands")),Unsigned(budgets.at("grants"))},
        {algorithm.at("id"),algorithm.at("version"),algorithm.at("instance")}}));
    // Also rejects missing/unknown keys, reordered/noncanonical arrays and any
    // tampering that did not update the exact input fingerprint.
    if(PlanningContextJson(context)!=input) throw std::invalid_argument("Context is noncanonical or fingerprint does not match");
    return context;
  } catch(const std::exception& error) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,std::string{"Invalid planning context: "}+error.what()});
  }
}

inline auto FlowPlanJson(const contracts::ProtocolCyclePlan& plan) -> nlohmann::json {
  using namespace planning_json;
  auto result=Json::array();
  for(const auto& opportunity:plan.mac_plan().tx_opportunities()) {
    const auto& f=*opportunity.flow_selection;
    result.push_back({{"grant_id",Id(f.grant_id.value())},{"flow_id",Id(f.flow_id.value())},
        {"owner",Id(opportunity.sender_node_id.value())},{"destination",Id(f.destination_node_id.value())},
        {"next_hop",Id(f.next_hop_node_id.value())},{"start_ns",Time(opportunity.eligible_at)},
        {"reservation_end_ns",Time(f.reservation_end)},{"cycle_id",Id(f.cycle_id.value())},
        {"snapshot_version",Id(f.snapshot_version.value())},{"context_fingerprint",Id(f.context_fingerprint)}});
  }
  return {{"kind","serial-flow-schedule-v1"},{"grants",result}};
}
inline auto FlowPlanJson(const planning::FlowTransmissionPlan& plan) -> nlohmann::json {
  return FlowPlanJson(plan.protocol_plan());
}
}  // namespace ns3_factory::worker::adapter
