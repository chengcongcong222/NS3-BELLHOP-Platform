#pragma once
#include <ns3_factory/contracts/planning_context.hpp>
#include "internal/acoustic_field_channel_provider.hpp"

namespace ns3_factory::application::internal {
struct PlanningEnvironment final {
  std::reference_wrapper<const environment::internal::AcousticFieldChannelProvider> provider;
  std::string asset_id, content_version, checksum;
};

// Application boundary, not plugin code: no live communication query, trace,
// ns-3 access, online Bellhop, or randomly selected noise sample.
inline auto AssemblePlanningContext(std::string run_id,
    const contracts::WorldSnapshot& world,const contracts::StructureSnapshot& structure,
    const contracts::RoutingPlan& routing,const contracts::PlanningInput& physical,
    const contracts::FlowPlanningState& state,const PlanningEnvironment& environment,
    double frequency_hz,double bandwidth_hz,contracts::SimDuration duration,
    std::uint64_t seed,const std::map<std::string,std::string>& parameters,
    contracts::PlanningAlgorithmIdentity algorithm,
    contracts::PlanningBudgets budgets = {}) -> contracts::Result<contracts::PlanningContext> {
  using namespace contracts;
  std::vector<LinkPrediction> links;
  for(const auto& tx : world.nodes()) if(tx.capability.can_transmit)
    for(const auto& rx : world.nodes()) if(rx.capability.can_receive && rx.node_id != tx.node_id) {
      if(links.size() >= budgets.maximum_links) return std::unexpected(Error{
          ErrorCode::kInvalidArgument,"PlanningInputUnavailable: directed link budget exceeded"});
      auto sample=environment.provider.get().Sample(tx.motion.position,rx.motion.position,frequency_hz);
      if(!sample) return std::unexpected(Error{sample.error().code,
          "PlanningInputUnavailable: " + sample.error().message});
      LinkPrediction link{tx.node_id,rx.node_id,tx.motion.position,rx.motion.position,
          world.committed_at(),frequency_hz,bandwidth_hz,environment.asset_id,
          environment.content_version,environment.checksum,"acoustic-field-shared-sample-v1",
          LinkPredictionStatus::kNoArrival,{},{},{},{}};
      if(const auto* signal=std::get_if<environment::internal::AcousticFieldSignalCell>(&*sample)) {
        link.status=LinkPredictionStatus::kAvailable;
        link.loss_db=signal->aggregate_transmission_loss_db;
        link.first_delay=signal->first_arrival_delay;
        if(!signal->paths.empty()) {
          link.path_count=signal->paths.size();
          link.maximum_excess_delay=SimDuration::Zero();
          for(const auto& path:signal->paths) link.maximum_excess_delay=std::max(*link.maximum_excess_delay,path.excess_delay());
        }
      }
      links.push_back(std::move(link));
    }
  auto demands=state.demands;
  for(auto& demand:demands) demand.next_hop=routing.FindNextHop(demand.key.queue_owner_node_id,demand.destination_node_id);
  return PlanningContext::Create({std::move(run_id),world,structure,routing,physical,state.structure_origin,
      duration,std::move(demands),std::move(links),state.previous,parameters,seed,budgets,std::move(algorithm)});
}
}  // namespace ns3_factory::application::internal
