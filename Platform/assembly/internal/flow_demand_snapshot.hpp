#pragma once
#include <map>
#include <set>
#include <ns3_factory/contracts/planning_context.hpp>
#include "internal/packet_queue_store.hpp"

namespace ns3_factory::assembly::internal {

// Forecast is metadata only: no PacketId allocation, no payload generation and
// no credit for relays that may or may not arrive later in the current cycle.
inline auto SnapshotFlowDemands(const runtime::internal::PacketQueueStore& queues,
    const contracts::PlanningInput& physical,
    const std::vector<contracts::FlowInputForecast>& forecast)
    -> contracts::Result<std::vector<contracts::FlowDemand>> {
  using namespace contracts;
  std::map<FlowDemandKey, FlowDemand> demands;
  std::map<FlowId, std::pair<NodeId, NodeId>> identities;
  const auto add = [&](NodeId owner, FlowId flow, NodeId source, NodeId destination,
                       std::uint64_t count, bool ready) -> Status {
    auto airtime = std::find_if(physical.demands().begin(),physical.demands().end(),
        [&](const auto& item) { return item.node_id == owner; });
    const auto nodes = queues.node_ids();
    if(airtime == physical.demands().end() || owner == destination || count == 0 ||
       !std::binary_search(nodes.begin(),nodes.end(),source) ||
       !std::binary_search(nodes.begin(),nodes.end(),destination)) return std::unexpected(Error{
        ErrorCode::kInvalidArgument,"Flow forecast/queue identity is invalid"});
    const auto [identity, inserted] = identities.emplace(flow,std::pair{source,destination});
    if(!inserted && identity->second != std::pair{source,destination}) return std::unexpected(Error{
        ErrorCode::kInvalidArgument,"FlowId belongs to conflicting endpoints"});
    auto [entry, created] = demands.try_emplace(FlowDemandKey{owner,flow},FlowDemand{
        {owner,flow},source,destination,std::nullopt,0,0,airtime->maximum_packet_airtime});
    auto& total = ready ? entry->second.ready_packets : entry->second.queued_packets;
    if(count > UINT64_MAX-total) return std::unexpected(Error{ErrorCode::kOverflow,"Flow demand count overflows"});
    total += count;
    return {};
  };
  for(const auto owner : queues.node_ids()) {
    const auto packets = queues.Packets(owner);
    if(!packets) return std::unexpected(packets.error());
    for(const auto& packet : *packets) {
      const auto* destination = std::get_if<UnicastDestination>(&packet.destination);
      if(!packet.flow_id || !destination) return std::unexpected(Error{
          ErrorCode::kFailedPrecondition,"Flow planning requires identified unicast queue packets"});
      auto status = add(owner,*packet.flow_id,packet.source_node_id,destination->node_id,1,false);
      if(!status) return std::unexpected(status.error());
    }
  }
  std::set<FlowId> unique_forecasts;
  for(const auto& flow : forecast) {
    if(!unique_forecasts.insert(flow.flow_id).second) return std::unexpected(Error{
        ErrorCode::kAlreadyExists,"Input forecast repeats a FlowId"});
    if(!flow.packets) continue;
    auto status = add(flow.source_node_id,flow.flow_id,flow.source_node_id,
                      flow.destination_node_id,flow.packets,true);
    if(!status) return std::unexpected(status.error());
  }
  std::vector<FlowDemand> result;
  for(const auto& [key,value] : demands) result.push_back(value);
  return result;
}
}  // namespace ns3_factory::assembly::internal
