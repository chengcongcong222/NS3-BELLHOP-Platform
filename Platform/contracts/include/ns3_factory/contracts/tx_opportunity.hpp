#pragma once
#include <optional>

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

struct FlowSelectionConstraint final {
  GrantId grant_id;
  FlowId flow_id;
  NodeId destination_node_id;
  NodeId next_hop_node_id;
  PlanningCycleId cycle_id;
  SnapshotVersion snapshot_version;
  std::uint64_t context_fingerprint;
  SimTime reservation_end;
  constexpr auto operator==(const FlowSelectionConstraint&) const -> bool = default;
};

struct TxOpportunity final {
  NodeId sender_node_id;
  SimTime eligible_at;
  std::optional<FlowSelectionConstraint> flow_selection{};

  constexpr auto operator==(const TxOpportunity&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
