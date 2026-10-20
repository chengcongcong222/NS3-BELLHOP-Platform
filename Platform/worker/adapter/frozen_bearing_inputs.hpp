#pragma once
#include <nlohmann/json.hpp>
#include "internal/simulation_worker.hpp"

namespace ns3_factory::worker::adapter {
inline auto BuildFrozenBearingInputs(const internal::RunExecutionDefinition& definition)
    -> contracts::Result<std::vector<assembly::internal::GeneratedFeatureReport>> {
  using namespace contracts;
  const auto& e=definition.experiment;
  if(!e.planning() || !definition.trajectories ||
     e.fusion().workload==application::ApplicationWorkload::kCommunicationOnly ||
     !e.planning()->fusion_receiver_node_id || definition.recorded)
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Bearing freeze requires geometric cycle-start observations"});
  const auto period=e.planning()->cycle_duration.nanoseconds();
  const auto cycles=e.simulation_cycle_count();
  if(!cycles || cycles>65535 || period<=0 ||
     cycles>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()/period))
    return std::unexpected(Error{ErrorCode::kOutOfRange,"Bearing ledger time/sequence limit exceeded"});
  std::vector<assembly::internal::GeneratedFeatureReport> result;
  for(std::uint64_t cycle=0;cycle<cycles;++cycle) {
    const auto at=SimTime::FromNanoseconds(static_cast<std::int64_t>(cycle)*period);
    for(const auto& n:definition.scenario.nodes()) {
      if(!n.capability.can_transmit || n.node_id==*e.planning()->fusion_receiver_node_id)continue;
      if(result.size()>=100000)
        return std::unexpected(Error{ErrorCode::kOutOfRange,"Bearing ledger exceeds 100000-input deployment budget"});
      const auto motion=definition.trajectories->Query(n.node_id,at);
      if(!motion)return std::unexpected(motion.error());
      const auto report=assembly::internal::MakeDetectionFeatureReportV1(
          static_cast<std::uint16_t>(cycle+1),SimTime::Zero(),at,motion->position.x_meters,
          motion->position.y_meters,e.fusion().target_x_meters,e.fusion().target_y_meters);
      if(!report)return std::unexpected(report.error());
      result.push_back({PlanningCycleId{cycle},n.node_id,PacketId{1000000+result.size()},at,*report});
    }
  }
  if(result.empty())return std::unexpected(Error{ErrorCode::kInvalidArgument,"Bearing ledger has no sources"});
  return result;
}

inline auto BearingInputJson(std::span<const assembly::internal::GeneratedFeatureReport> inputs,
    contracts::NodeId receiver) -> nlohmann::json {
  auto packets=nlohmann::json::array();
  constexpr char hex[]="0123456789abcdef";
  for(const auto& input:inputs) {
    const auto bytes=assembly::internal::EncodeDetectionFeatureReportV1(input.report);
    if(!bytes)throw std::invalid_argument(bytes.error().message);
    std::string payload;
    for(const auto byte:*bytes) {
      const auto b=std::to_integer<unsigned>(byte);
      payload.push_back(hex[b>>4]);payload.push_back(hex[b&15]);
    }
    packets.push_back({{"cycle",std::to_string(input.cycle_id.value())},
      {"packet_id",std::to_string(input.packet_id.value())},
      {"source_node_id",std::to_string(input.sender_node_id.value())},
      {"destination_node_id",std::to_string(receiver.value())},
      {"sample_time_ns",std::to_string(input.sample_time.nanoseconds())},
      {"payload_hex",payload}});
  }
  return {{"generator","DetectionFeatureReportV1/cycle-start-v1"},
          {"packets",packets},{"count",std::to_string(inputs.size())}};
}
} // namespace ns3_factory::worker::adapter
