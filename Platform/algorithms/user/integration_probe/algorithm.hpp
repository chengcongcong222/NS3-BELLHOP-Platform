#pragma once
// This example uses public SDK/contracts only, never Runtime/internal headers.
#include <ns3_factory/planning/mac_algorithm.hpp>

namespace user_integration_probe {
inline auto Build(const ns3_factory::contracts::WorldSnapshot& world,
    const ns3_factory::contracts::StructureSnapshot& structure,
    const ns3_factory::contracts::PlanningInput& input,
    ns3_factory::contracts::SimDuration duration,
    const ns3_factory::planning::Parameters& parameters)
    -> ns3_factory::contracts::Result<std::vector<ns3_factory::contracts::TxOpportunity>> {
  using namespace ns3_factory::contracts;
  const auto valid = ValidatePlanningInput(input, world, structure);
  if(!valid) return std::unexpected(valid.error());
  if(world.nodes().size() < 2) return std::unexpected(Error{
      ErrorCode::kFailedPrecondition, "Ordered MAC example needs at least two nodes"});
  const auto gap = ns3_factory::planning::IntegerParameter(parameters, "gap_ns");
  if(!gap) return std::unexpected(gap.error());
  const auto end = CheckedAdd(world.committed_at(), duration);
  if(!end) return std::unexpected(Error{ErrorCode::kOverflow, "Cycle end overflows"});
  auto demands = input.demands();
  auto cursor = world.committed_at();
  std::vector<TxOpportunity> result;
  for(std::size_t i = 0; i < demands.size(); ++i) {
    const auto& demand = demands[parameters.at("order") == "降序" ? demands.size()-1-i : i];
    auto width = CheckedAdd(demand.maximum_packet_airtime, input.maximum_propagation_tail());
    if(width) width = CheckedAdd(*width, SimDuration::FromNanoseconds(*gap));
    if(!width) return std::unexpected(Error{ErrorCode::kOverflow, "Physical occupancy overflows"});
    const auto count = demand.queued_packet_count + demand.ready_packet_count;
    if(count && *width <= SimDuration::Zero()) return std::unexpected(Error{
        ErrorCode::kInvalidArgument, "Active sender must have positive occupancy"});
    for(std::uint64_t n = 0; n < count; ++n) {
      const auto next = CheckedAdd(cursor, *width);
      if(!next || *next > *end) break;
      result.push_back({demand.node_id, cursor});
      cursor = *next;
    }
  }
  return result;
}
}
