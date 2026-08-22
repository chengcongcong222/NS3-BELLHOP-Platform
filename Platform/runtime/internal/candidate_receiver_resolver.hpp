#pragma once

#include <algorithm>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

#include "internal/cycle_working_state.hpp"

namespace ns3_factory::runtime::internal {

class CandidateReceiverResolver final {
 public:
  [[nodiscard]] auto Resolve(
      const CycleWorkingState& working_state,
      contracts::NodeId sender_node_id) const
      -> contracts::Result<std::vector<contracts::NodeId>>;
};

inline auto CandidateReceiverResolver::Resolve(
    const CycleWorkingState& working_state,
    contracts::NodeId sender_node_id) const
    -> contracts::Result<std::vector<contracts::NodeId>> {
  const auto nodes = working_state.base_snapshot().nodes();
  const auto sender = std::lower_bound(
      nodes.begin(),
      nodes.end(),
      sender_node_id,
      [](const contracts::NodeCommittedState& node,
         contracts::NodeId candidate) {
        return node.node_id < candidate;
      });
  if(sender == nodes.end() || sender->node_id != sender_node_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kNotFound,
            "Candidate receiver sender is outside the cycle node universe"});
  }

  std::vector<contracts::NodeId> candidates;
  candidates.reserve(nodes.size());
  for(const auto& node : nodes) {
    if(node.node_id != sender_node_id && node.capability.can_receive) {
      candidates.push_back(node.node_id);
    }
  }
  return candidates;
}

}  // namespace ns3_factory::runtime::internal
