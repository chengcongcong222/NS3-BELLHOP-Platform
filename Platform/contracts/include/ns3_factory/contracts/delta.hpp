#pragma once

#include <vector>

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

struct NodeStateReplacement final {
  NodeCommittedState state;
};

struct DeltaSet final {
  PlanningCycleId cycle_id;
  SnapshotVersion base_version;
  SimTime effective_at;
  std::vector<NodeStateReplacement> node_replacements;
};

}  // namespace ns3_factory::contracts
