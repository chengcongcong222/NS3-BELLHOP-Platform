#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>

namespace ns3_factory::contracts {

// Explicit simulation-wide knowledge copied at the formal cycle boundary;
// these observations are not knowledge obtained by a distributed protocol.
enum class PlanningKnowledgeScope : std::uint8_t {
  kGlobalSimulationKnowledge = 1,
};

struct NodeAirtime final {
  NodeId node_id;
  SimDuration maximum_packet_airtime;
};

struct NodeTrafficDemand final {
  NodeId node_id;
  std::uint64_t queued_packet_count;
  std::uint64_t ready_packet_count;
  SimDuration maximum_packet_airtime;
};

class PlanningInput final {
 public:
  [[nodiscard]] static auto Create(
      PlanningCycleId cycle_id, SnapshotVersion base_snapshot_version,
      SimTime observed_at, std::vector<NodeTrafficDemand> demands,
      SimDuration maximum_propagation_tail, bool allow_receiver_overlap = false,
      SimDuration emission_quantum = SimDuration::FromNanoseconds(1)) -> Result<PlanningInput> {
    if(maximum_propagation_tail < SimDuration::Zero() || emission_quantum <= SimDuration::Zero()) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument,
          "Planning propagation tail must be non-negative"});
    }
    std::sort(demands.begin(), demands.end(), [](const auto& a, const auto& b) {
      return a.node_id < b.node_id;
    });
    for(std::size_t i = 0; i < demands.size(); ++i) {
      const auto& demand = demands[i];
      if(i && demands[i - 1].node_id == demand.node_id) {
        return std::unexpected(Error{ErrorCode::kAlreadyExists,
            "Planning demand contains a duplicate node"});
      }
      if(demand.maximum_packet_airtime < SimDuration::Zero() ||
         ((demand.queued_packet_count || demand.ready_packet_count) &&
          demand.maximum_packet_airtime == SimDuration::Zero())) {
        return std::unexpected(Error{ErrorCode::kInvalidArgument,
            "Planning demand requires a positive packet airtime"});
      }
      if(demand.ready_packet_count >
         std::numeric_limits<std::uint64_t>::max() - demand.queued_packet_count) {
        return std::unexpected(Error{ErrorCode::kOverflow,
            "Planning packet demand overflows"});
      }
    }
    return PlanningInput{cycle_id, base_snapshot_version, observed_at,
                         std::move(demands), maximum_propagation_tail, allow_receiver_overlap, emission_quantum};
  }

  auto cycle_id() const noexcept -> PlanningCycleId { return cycle_id_; }
  auto base_snapshot_version() const noexcept -> SnapshotVersion { return version_; }
  auto observed_at() const noexcept -> SimTime { return observed_at_; }
  auto knowledge_scope() const noexcept -> PlanningKnowledgeScope {
    return PlanningKnowledgeScope::kGlobalSimulationKnowledge;
  }
  auto demands() const noexcept -> std::span<const NodeTrafficDemand> { return demands_; }
  auto maximum_propagation_tail() const noexcept -> SimDuration { return tail_; }
  auto allow_receiver_overlap() const noexcept -> bool { return allow_receiver_overlap_; }
  auto emission_quantum() const noexcept -> SimDuration { return emission_quantum_; }

 private:
  PlanningInput(PlanningCycleId cycle_id, SnapshotVersion version,
                SimTime observed_at, std::vector<NodeTrafficDemand> demands,
                SimDuration tail, bool allow_receiver_overlap, SimDuration emission_quantum)
      : cycle_id_(cycle_id), version_(version), observed_at_(observed_at),
        demands_(std::move(demands)), tail_(tail), allow_receiver_overlap_(allow_receiver_overlap),
        emission_quantum_(emission_quantum) {}
  PlanningCycleId cycle_id_;
  SnapshotVersion version_;
  SimTime observed_at_;
  std::vector<NodeTrafficDemand> demands_;
  SimDuration tail_;
  bool allow_receiver_overlap_{false};
  SimDuration emission_quantum_;
};

[[nodiscard]] inline auto ValidatePlanningInput(
    const PlanningInput& input, const WorldSnapshot& world,
    const StructureSnapshot& structure) -> Status {
  if(input.cycle_id() != structure.cycle_id() ||
     input.base_snapshot_version() != world.version() ||
     structure.base_snapshot_version() != world.version() ||
     input.observed_at() != world.committed_at()) {
    return std::unexpected(Error{ErrorCode::kFailedPrecondition,
        "Planning input provenance does not match the cycle boundary"});
  }
  const auto nodes = world.nodes();
  const auto ids = structure.connectivity_graph().node_ids();
  const auto demands = input.demands();
  if(nodes.size() != ids.size() || nodes.size() != demands.size()) {
    return std::unexpected(Error{ErrorCode::kFailedPrecondition,
        "Planning input node universe is incomplete"});
  }
  for(std::size_t i = 0; i < nodes.size(); ++i) {
    if(nodes[i].node_id != ids[i] || nodes[i].node_id != demands[i].node_id ||
       (!nodes[i].capability.can_transmit &&
        (demands[i].queued_packet_count || demands[i].ready_packet_count)) ||
       (nodes[i].capability.can_transmit &&
        demands[i].maximum_packet_airtime <= SimDuration::Zero())) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Planning demand node identity or transmit capability is invalid"});
    }
  }
  return {};
}

// Admission checks for explicitly bound planned algorithms. No PHY, channel
// query, mutable owner, scheduler or online provider enters this boundary.
[[nodiscard]] inline auto ValidatePlanningPlan(
    const ProtocolCyclePlan& plan, const PlanningInput& input,
    const WorldSnapshot& world, const StructureSnapshot& structure) -> Status {
  const auto valid = ValidatePlanningInput(input, world, structure);
  if(!valid) return valid;
  const auto& timing = plan.timing();
  if(timing.cycle_id() != input.cycle_id() ||
     timing.base_snapshot_version() != input.base_snapshot_version() ||
     timing.starts_at() != input.observed_at()) {
    return std::unexpected(Error{ErrorCode::kFailedPrecondition,
        "Planned cycle provenance does not match its immutable input"});
  }
  // An external planned algorithm must bind an actual routing table, even
  // when empty. Reconstruct against this structure: provenance equality alone
  // does not prove that a table was built for this node universe/topology.
  const auto& routing = plan.routing_plan();
  if(!routing || routing->cycle_id() != structure.cycle_id() ||
     routing->base_snapshot_version() != structure.base_snapshot_version()) {
    return std::unexpected(Error{ErrorCode::kFailedPrecondition,
        "Planned algorithm routing is absent or has mismatched provenance"});
  }
  const auto routes = routing->entries();
  const auto rebound = RoutingPlan::Create(
      std::vector<RouteEntry>{routes.begin(), routes.end()}, structure);
  if(!rebound) return std::unexpected(rebound.error());
  auto previous_end = timing.starts_at();
  std::vector<std::pair<NodeId,SimTime>> sender_ends;
  for(const auto& opportunity : plan.mac_plan().tx_opportunities()) {
    if(opportunity.eligible_at.nanoseconds()%input.emission_quantum().nanoseconds()!=0)
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,"Planned emission is off declared sample grid"});
    const auto demands = input.demands();
    const auto found = std::lower_bound(demands.begin(), demands.end(),
        opportunity.sender_node_id, [](const auto& demand, NodeId node_id) {
          return demand.node_id < node_id;
        });
    const auto node = world.FindNode(opportunity.sender_node_id);
    if(found == demands.end() || found->node_id != opportunity.sender_node_id ||
       !node || !node->get().capability.can_transmit) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Planned sender has no valid airtime or transmit capability"});
    }
    const auto occupied = CheckedAdd(found->maximum_packet_airtime,
                                    input.maximum_propagation_tail());
    if(!occupied) return std::unexpected(Error{ErrorCode::kOverflow,
        "Planned physical occupancy overflows"});
    const auto end = CheckedAdd(opportunity.eligible_at, *occupied);
    if(!end) return std::unexpected(Error{ErrorCode::kOverflow,
        "Planned physical completion time overflows"});
    if((!input.allow_receiver_overlap() && opportunity.eligible_at < previous_end) || *end > timing.closes_at()) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
          "Planned transmissions overlap or cannot finish before cycle close"});
    }
    if(input.allow_receiver_overlap()) {
      auto tx_end=CheckedAdd(opportunity.eligible_at,found->maximum_packet_airtime);
      if(!tx_end)return std::unexpected(Error{ErrorCode::kOverflow,"Planned sender airtime overflow"});
      auto prior=std::find_if(sender_ends.begin(),sender_ends.end(),[&](const auto& p){
        return p.first==opportunity.sender_node_id;
      });
      if(prior!=sender_ends.end()) {
        if(opportunity.eligible_at<prior->second)
          return std::unexpected(Error{ErrorCode::kFailedPrecondition,"Same sender transmissions overlap"});
        prior->second=*tx_end;
      } else sender_ends.emplace_back(opportunity.sender_node_id,*tx_end);
    }
    previous_end = *end;
  }
  return {};
}

}  // namespace ns3_factory::contracts
