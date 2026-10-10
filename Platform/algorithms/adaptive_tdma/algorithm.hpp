#pragma once
#include <ns3_factory/planning/mac_algorithm.hpp>
#include "internal/adaptive_tdma_mac_planner.hpp"

namespace ns3_factory::algorithms {
inline auto BuildAdaptiveTdma(const contracts::WorldSnapshot& world,
    const contracts::StructureSnapshot& structure, const contracts::PlanningInput& input,
    contracts::SimDuration frame, const planning::Parameters& parameters)
    -> contracts::Result<std::vector<contracts::TxOpportunity>> {
  const auto margin = planning::IntegerParameter(parameters, "safety_margin_ns");
  if(!margin) return std::unexpected(margin.error());
  // Preserve the legacy application's early occupancy rejection, including
  // a user-specified safety margin. Never silently produce an all-idle run.
  auto maximum_airtime = contracts::SimDuration::Zero();
  for(const auto& demand : input.demands()) maximum_airtime = std::max(maximum_airtime, demand.maximum_packet_airtime);
  auto required = contracts::CheckedAdd(maximum_airtime, input.maximum_propagation_tail());
  if(required) required = contracts::CheckedAdd(*required, contracts::SimDuration::FromNanoseconds(*margin));
  if(!required || *required > frame) return std::unexpected(contracts::Error{
      contracts::ErrorCode::kFailedPrecondition,
      "Adaptive TDMA cycle cannot contain one packet, propagation tail and safety margin"});
  auto policy = planning::internal::AdaptiveTdmaPolicy::Create(frame,
      contracts::SimDuration::FromNanoseconds(*margin));
  if(!policy) return std::unexpected(policy.error());
  auto result = planning::internal::AdaptiveTdmaMacPlanner{*policy}.Build(world, structure, input);
  if(!result) return std::unexpected(result.error());
  const auto opportunities = result->mac_plan().tx_opportunities();
  return std::vector<contracts::TxOpportunity>{opportunities.begin(), opportunities.end()};
}
}  // namespace ns3_factory::algorithms
