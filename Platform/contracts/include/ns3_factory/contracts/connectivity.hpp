#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/detail/node_universe.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct DirectedLink final {
  NodeId source_node_id;
  NodeId target_node_id;

  constexpr auto operator==(const DirectedLink&) const noexcept
      -> bool = default;
};

class ConnectivityGraph final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<DirectedLink> edges,
      std::vector<NodeId> valid_node_ids) -> Result<ConnectivityGraph>;

  [[nodiscard]] auto edges() const noexcept
      -> std::span<const DirectedLink> {
    return std::span<const DirectedLink>{edges_};
  }

  [[nodiscard]] auto node_ids() const noexcept
      -> std::span<const NodeId> {
    return std::span<const NodeId>{node_ids_};
  }

  [[nodiscard]] auto HasEdge(NodeId source_node_id,
                             NodeId target_node_id) const noexcept -> bool;

  [[nodiscard]] auto OutgoingNeighbors(NodeId node_id) const
      -> std::vector<NodeId>;

  [[nodiscard]] auto IncomingNeighbors(NodeId node_id) const
      -> std::vector<NodeId>;

  auto operator==(const ConnectivityGraph&) const -> bool = default;

 private:
  ConnectivityGraph(std::vector<DirectedLink> edges,
                    std::vector<NodeId> node_ids) noexcept
      : edges_(std::move(edges)), node_ids_(std::move(node_ids)) {}

  [[nodiscard]] static constexpr auto Less(const DirectedLink& lhs,
                                            const DirectedLink& rhs) noexcept
      -> bool;

  std::vector<DirectedLink> edges_;
  std::vector<NodeId> node_ids_;
};

inline constexpr auto ConnectivityGraph::Less(
    const DirectedLink& lhs,
    const DirectedLink& rhs) noexcept -> bool {
  if(lhs.source_node_id != rhs.source_node_id) {
    return lhs.source_node_id < rhs.source_node_id;
  }
  return lhs.target_node_id < rhs.target_node_id;
}

inline auto ConnectivityGraph::Create(
    std::vector<DirectedLink> edges,
    std::vector<NodeId> valid_node_ids) -> Result<ConnectivityGraph> {
  auto node_ids =
      detail::CanonicalizeNodeUniverse(std::move(valid_node_ids));
  if(!node_ids) {
    return std::unexpected(node_ids.error());
  }

  for(const auto& edge : edges) {
    if(edge.source_node_id == edge.target_node_id) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "ConnectivityGraph does not allow self-loops"});
    }
    if(!detail::ContainsNode(*node_ids, edge.source_node_id) ||
       !detail::ContainsNode(*node_ids, edge.target_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kNotFound,
                "ConnectivityGraph edge references an unknown NodeId"});
    }
  }

  std::sort(edges.begin(), edges.end(), Less);
  const auto duplicate = std::adjacent_find(
      edges.begin(), edges.end(), [](const DirectedLink& lhs,
                                     const DirectedLink& rhs) {
        return lhs == rhs;
      });
  if(duplicate != edges.end()) {
    return std::unexpected(
        Error{ErrorCode::kAlreadyExists,
              "ConnectivityGraph contains a duplicate directed edge"});
  }

  return ConnectivityGraph{std::move(edges), std::move(*node_ids)};
}

inline auto ConnectivityGraph::HasEdge(NodeId source_node_id,
                                       NodeId target_node_id) const noexcept
    -> bool {
  const DirectedLink candidate{source_node_id, target_node_id};
  return std::binary_search(edges_.begin(), edges_.end(), candidate, Less);
}

inline auto ConnectivityGraph::OutgoingNeighbors(NodeId node_id) const
    -> std::vector<NodeId> {
  std::vector<NodeId> neighbors;
  for(const auto& edge : edges_) {
    if(edge.source_node_id == node_id) {
      neighbors.push_back(edge.target_node_id);
    }
  }
  return neighbors;
}

inline auto ConnectivityGraph::IncomingNeighbors(NodeId node_id) const
    -> std::vector<NodeId> {
  std::vector<NodeId> neighbors;
  for(const auto& edge : edges_) {
    if(edge.target_node_id == node_id) {
      neighbors.push_back(edge.source_node_id);
    }
  }
  return neighbors;
}

}  // namespace ns3_factory::contracts
