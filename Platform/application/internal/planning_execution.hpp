#pragma once

#include <ns3_factory/application/domain.hpp>
#include <ns3_factory/contracts/planning_input.hpp>
#include <ns3_factory/planning/registered_algorithms.hpp>
#include "internal/adaptive_tdma_mac_planner.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/direct_routing_planner.hpp"
#include "internal/shortest_path_routing_planner.hpp"
#include "internal/logical_topology_policy.hpp"
#include "internal/acoustic_field_asset.hpp"
#include "planning_context_assembler.hpp"

namespace ns3_factory::application::internal {

// Explicit global-simulation connectivity baseline. Physical reachability is
// still decided by the actual channel query, never by this planning graph.
class CapabilityLinkEstimator final : public contracts::ILinkFeasibilityEstimator {
 public:
  auto Estimate(const contracts::LinkFeasibilityQuery& q) const
      -> contracts::Result<contracts::LinkFeasibilityEstimate> override {
    return contracts::LinkFeasibilityEstimate::Create(q.source_node_id(),
        q.target_node_id(), q.observed_at(), 1.0);
  }
};

class DirectedConnectivityTopology final : public structure::internal::ILogicalTopologyPolicy {
 public:
  auto Build(const contracts::WorldSnapshot& world, const contracts::RoleTable&,
             const contracts::ConnectivityGraph& graph) const
      -> contracts::Result<contracts::LogicalTopology> override {
    std::vector<contracts::NodeId> nodes;
    std::vector<contracts::LogicalLink> links;
    for(const auto& n : world.nodes()) nodes.push_back(n.node_id);
    for(const auto& e : graph.edges()) links.push_back({e.source_node_id, e.target_node_id});
    return contracts::LogicalTopology::Create(std::move(links), std::move(nodes), graph);
  }
};

// Keep a fixed business horizon even when comparing configured and adaptive
// schedules. Configured slots may not overrun it; the unused suffix is idle.
class FrameConfiguredMac final : public planning::internal::IMacPlanner {
 public:
  explicit FrameConfiguredMac(const ExperimentDefinition& definition) : definition_(definition) {}
  auto Build(const contracts::WorldSnapshot& world, const contracts::StructureSnapshot& structure) const
      -> contracts::Result<planning::internal::MacPlanningResult> override {
    using namespace contracts;
    if(!definition_.get().planning() || definition_.get().mac().mode != MacMode::kTdma ||
       definition_.get().planning()->safety_margin != SimDuration::Zero()) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Frame-configured MAC requires explicit configured TDMA planning inputs"});
    }
    std::vector<NodeId> owners;
    for(const auto& n : world.nodes()) if(n.capability.can_transmit) owners.push_back(n.node_id);
    const auto& d = definition_.get();
    auto policy = planning::internal::ConfiguredTdmaPolicy::Create(
        d.mac().slot_duration, d.mac().guard_interval, std::move(owners));
    if(!policy) return std::unexpected(policy.error());
    auto result = planning::internal::ConfiguredTdmaMacPlanner{std::move(*policy)}.Build(world, structure);
    if(!result) return std::unexpected(result.error());
    const auto end = CheckedAdd(world.committed_at(), d.planning()->cycle_duration);
    if(!end) return std::unexpected(Error{ErrorCode::kOverflow,
        "Configured TDMA frame end overflows simulation time"});
    if(result->timing().closes_at() > *end) return std::unexpected(Error{
        ErrorCode::kInvalidArgument, "Configured TDMA slots exceed the fixed planning cycle"});
    auto timing = CycleTiming::Create(structure.cycle_id(), world.version(), world.committed_at(), *end);
    if(!timing) return std::unexpected(timing.error());
    const auto opportunities = result->mac_plan().tx_opportunities();
    return planning::internal::MacPlanningResult::Create(*timing,
        {opportunities.begin(), opportunities.end()});
  }
  auto Build(const contracts::WorldSnapshot& world,
             const contracts::StructureSnapshot& structure,
             const contracts::PlanningInput& input) const
      -> contracts::Result<planning::internal::MacPlanningResult> override {
    using namespace contracts;
    const auto valid = ValidatePlanningInput(input, world, structure);
    if(!valid) return std::unexpected(valid.error());
    const auto& definition = definition_.get();
    if(!definition.planning() || definition.mac().mode != MacMode::kTdma ||
       definition.planning()->safety_margin != SimDuration::Zero()) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Frame-configured MAC requires explicit configured TDMA planning inputs"});
    }
    for(const auto& demand : input.demands()) {
      if(!world.FindNode(demand.node_id)->get().capability.can_transmit) continue;
      const auto required = CheckedAdd(demand.maximum_packet_airtime,
                                       definition.mac().guard_interval);
      if(!required) return std::unexpected(Error{ErrorCode::kOverflow,
          "Configured TDMA airtime plus guard overflows"});
      if(*required > definition.mac().slot_duration) {
        return std::unexpected(Error{ErrorCode::kFailedPrecondition,
            "Configured TDMA slot cannot contain actual packet airtime and guard"});
      }
    }
    return Build(world, structure);
  }
 private:
  std::reference_wrapper<const ExperimentDefinition> definition_;
};

class RegisteredMacPlanner final : public planning::internal::IMacPlanner {
 public:
  explicit RegisteredMacPlanner(const ExperimentDefinition& definition) : definition_(definition) {}
  auto Build(const contracts::WorldSnapshot&, const contracts::StructureSnapshot&) const
      -> contracts::Result<planning::internal::MacPlanningResult> override {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
        "Registered MAC requires explicit planning input"});
  }
  auto Build(const contracts::WorldSnapshot& world, const contracts::StructureSnapshot& structure,
             const contracts::PlanningInput& input) const
      -> contracts::Result<planning::internal::MacPlanningResult> override {
    using namespace contracts;
    const auto valid = ValidatePlanningInput(input, world, structure);
    if(!valid) return std::unexpected(valid.error());
    const auto& config = *definition_.get().planning()->mac_algorithm;
    const auto* algorithm = planning::FindAlgorithm(config);
    if(!algorithm) return std::unexpected(Error{ErrorCode::kFailedPrecondition,
        "Exact MAC algorithm package is not installed"});
    const auto parameters = planning::ValidateParameters(*algorithm, config.parameters);
    if(!parameters) return std::unexpected(parameters.error());
    const auto duration = definition_.get().planning()->cycle_duration;
    const auto end = CheckedAdd(world.committed_at(), duration);
    if(!end) return std::unexpected(Error{ErrorCode::kOverflow, "Algorithm cycle end overflows"});
    const auto timing = CycleTiming::Create(structure.cycle_id(), world.version(), world.committed_at(), *end);
    if(!timing) return std::unexpected(timing.error());
    auto result = algorithm->build(world, structure, input, duration, config.parameters);
    if(!result) return std::unexpected(result.error());
    return planning::internal::MacPlanningResult::Create(*timing, std::move(*result));
  }
 private:
  std::reference_wrapper<const ExperimentDefinition> definition_;
};

class PlanningExecutionPlanner final : public planning::internal::IProtocolCyclePlanner {
 public:
  explicit PlanningExecutionPlanner(const ExperimentDefinition& definition) : definition_(definition) {}
  PlanningExecutionPlanner(const ExperimentDefinition& definition,std::string run_id,PlanningEnvironment environment)
      : definition_(definition),run_id_(std::move(run_id)),environment_(std::move(environment)) {}
  auto uses_flow_planning() const noexcept -> bool override {
    const auto& d=definition_.get();
    return d.planning() && d.planning()->mac_algorithm && d.planning()->mac_algorithm->interface=="planning-mac-v2";
  }
  auto BuildFlow(const contracts::WorldSnapshot& world,const contracts::StructureSnapshot& structure,
      const contracts::PlanningInput& input,const contracts::FlowPlanningState& state) const
      -> contracts::Result<contracts::ProtocolCyclePlan> override {
    using namespace contracts;
    const auto& d=definition_.get();
    if(!uses_flow_planning() || !environment_ ||
       (d.routing().mode!=RoutingMode::kDirect && d.routing().mode!=RoutingMode::kShortestPath))
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,"Flow planning requires explicit installed inputs and routing"});
    const auto& config=*d.planning()->mac_algorithm;
    const auto* algorithm=planning::FindFlowAlgorithm(config);
    if(!algorithm) return std::unexpected(Error{ErrorCode::kNotFound,"Exact v2 MAC package is not installed"});
    const auto valid=planning::ValidateParameters(*algorithm,config.parameters);
    if(!valid) return std::unexpected(valid.error());
    auto routing=d.routing().mode==RoutingMode::kDirect
        ? planning::internal::DirectRoutingPlanner{}.Build(structure)
        : planning::internal::ShortestPathRoutingPlanner{}.Build(structure);
    if(!routing) return std::unexpected(routing.error());
    auto context=AssemblePlanningContext(run_id_,world,structure,*routing,input,state,*environment_,
        d.phy().center_frequency_hz,d.phy().occupied_bandwidth_hz,d.planning()->cycle_duration,
        d.deterministic_seed(),config.parameters,{config.id,config.version,"mac"});
    if(!context) return std::unexpected(context.error());
    auto plan=algorithm->build(*context,config.parameters);
    if(!plan) return std::unexpected(plan.error());
    const auto admitted=plan->Validate(*context);
    if(!admitted) return std::unexpected(admitted.error());
    return plan->protocol_plan();
  }
  auto Build(const contracts::WorldSnapshot&, const contracts::StructureSnapshot&) const
      -> contracts::Result<contracts::ProtocolCyclePlan> override {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
        "Planning execution requires explicit planning inputs"});
  }
  auto Build(const contracts::WorldSnapshot& world, const contracts::StructureSnapshot& structure,
             const contracts::PlanningInput& input) const
      -> contracts::Result<contracts::ProtocolCyclePlan> override {
    using namespace planning::internal;
    const auto& d = definition_.get();
    if(!d.planning() ||
       (d.routing().mode != RoutingMode::kDirect && d.routing().mode != RoutingMode::kShortestPath) ||
       (d.mac().mode != MacMode::kTdma && d.mac().mode != MacMode::kAdaptiveTdma && d.mac().mode != MacMode::kRegistered)) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Planning execution requires an explicit supported routing and MAC mode"});
    }
    DirectRoutingPlanner direct;
    ShortestPathRoutingPlanner shortest;
    const IRoutingPlanner& routing = d.routing().mode == RoutingMode::kDirect
        ? static_cast<const IRoutingPlanner&>(direct) : static_cast<const IRoutingPlanner&>(shortest);
    if(d.mac().mode == MacMode::kAdaptiveTdma) {
      auto policy = AdaptiveTdmaPolicy::Create(d.planning()->cycle_duration, d.planning()->safety_margin);
      if(!policy) return std::unexpected(policy.error());
      AdaptiveTdmaMacPlanner mac{*policy};
      return CompositeProtocolCyclePlanner{routing, mac}.Build(world, structure, input);
    }
    if(d.mac().mode == MacMode::kRegistered) {
      RegisteredMacPlanner mac{d};
      return CompositeProtocolCyclePlanner{routing, mac}.Build(world, structure, input);
    }
    FrameConfiguredMac mac{d};
    return CompositeProtocolCyclePlanner{routing, mac}.Build(world, structure, input);
  }
 private:
  std::reference_wrapper<const ExperimentDefinition> definition_;
  std::string run_id_;
  std::optional<PlanningEnvironment> environment_;
};

inline auto MaximumAssetPropagationTail(const environment::internal::AcousticFieldAsset& asset)
    -> contracts::Result<contracts::SimDuration> {
  auto first_arrival_bound = contracts::SimDuration::Zero();
  auto excess_bound = contracts::SimDuration::Zero();
  for(const auto& cell : asset.cells()) {
    if(const auto* signal = std::get_if<environment::internal::AcousticFieldSignalCell>(&cell)) {
      first_arrival_bound = std::max(first_arrival_bound, signal->first_arrival_delay);
      for(const auto& path : signal->paths) excess_bound = std::max(excess_bound, path.excess_delay());
    }
  }
  // Delay is interpolated while paths come from the nearest cell. Separate
  // maxima also cover mixed-cell queries along a moving trajectory.
  const auto total = contracts::CheckedAdd(first_arrival_bound, excess_bound);
  if(!total) return std::unexpected(contracts::Error{contracts::ErrorCode::kOverflow,
      "Acoustic asset propagation tail exceeds simulation duration range"});
  return *total;
}

}  // namespace ns3_factory::application::internal
