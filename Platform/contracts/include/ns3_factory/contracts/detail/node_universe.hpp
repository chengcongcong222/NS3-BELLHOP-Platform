#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts::detail {

[[nodiscard]] inline auto CanonicalizeNodeUniverse(
    std::vector<NodeId> node_ids) -> Result<std::vector<NodeId>> {
  std::sort(node_ids.begin(), node_ids.end());
  if(std::adjacent_find(node_ids.begin(), node_ids.end()) !=
     node_ids.end()) {
    return std::unexpected(
        Error{ErrorCode::kAlreadyExists,
              "Node universe contains a duplicate NodeId"});
  }
  return node_ids;
}

[[nodiscard]] inline auto ContainsNode(std::span<const NodeId> node_ids,
                                       NodeId node_id) noexcept -> bool {
  return std::binary_search(node_ids.begin(), node_ids.end(), node_id);
}

[[nodiscard]] inline auto SameNodeUniverse(
    std::span<const NodeId> lhs,
    std::span<const NodeId> rhs) noexcept -> bool {
  return lhs.size() == rhs.size() &&
         std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

}  // namespace ns3_factory::contracts::detail
