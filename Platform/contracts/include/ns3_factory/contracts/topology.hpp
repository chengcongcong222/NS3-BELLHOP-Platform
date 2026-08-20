#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/detail/node_universe.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

struct LogicalLink final {
  NodeId source_node_id;
  NodeId target_node_id;

  constexpr auto operator==(const LogicalLink&) const noexcept
      -> bool = default;
};

class LogicalTopology final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<LogicalLink> links,
      std::vector<NodeId> valid_node_ids,
      const ConnectivityGraph& connectivity_graph)
      -> Result<LogicalTopology>;

  [[nodiscard]] auto links() const noexcept
      -> std::span<const LogicalLink> {
    return std::span<const LogicalLink>{links_};
  }

  [[nodiscard]] auto node_ids() const noexcept
      -> std::span<const NodeId> {
    return std::span<const NodeId>{node_ids_};
  }

  [[nodiscard]] auto HasLink(NodeId source_node_id,
                             NodeId target_node_id) const noexcept -> bool;

  auto operator==(const LogicalTopology&) const -> bool = default;

 private:
  LogicalTopology(std::vector<LogicalLink> links,
                  std::vector<NodeId> node_ids) noexcept
      : links_(std::move(links)), node_ids_(std::move(node_ids)) {}

  [[nodiscard]] static constexpr auto Less(const LogicalLink& lhs,
                                            const LogicalLink& rhs) noexcept
      -> bool;

  std::vector<LogicalLink> links_;
  std::vector<NodeId> node_ids_;
};

inline constexpr auto LogicalTopology::Less(
    const LogicalLink& lhs,
    const LogicalLink& rhs) noexcept -> bool {
  if(lhs.source_node_id != rhs.source_node_id) {
    return lhs.source_node_id < rhs.source_node_id;
  }
  return lhs.target_node_id < rhs.target_node_id;
}

inline auto LogicalTopology::Create(
    std::vector<LogicalLink> links,
    std::vector<NodeId> valid_node_ids,
    const ConnectivityGraph& connectivity_graph)
    -> Result<LogicalTopology> {
  auto node_ids =
      detail::CanonicalizeNodeUniverse(std::move(valid_node_ids));
  if(!node_ids) {
    return std::unexpected(node_ids.error());
  }
  if(!detail::SameNodeUniverse(*node_ids,
                               connectivity_graph.node_ids())) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "LogicalTopology and ConnectivityGraph node universes differ"});
  }

  for(const auto& link : links) {
    if(link.source_node_id == link.target_node_id) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "LogicalTopology does not allow self-loops"});
    }
    if(!detail::ContainsNode(*node_ids, link.source_node_id) ||
       !detail::ContainsNode(*node_ids, link.target_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kNotFound,
                "LogicalTopology link references an unknown NodeId"});
    }
    if(!connectivity_graph.HasEdge(link.source_node_id,
                                   link.target_node_id)) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "LogicalTopology link is absent from ConnectivityGraph"});
    }
  }

  std::sort(links.begin(), links.end(), Less);
  const auto duplicate = std::adjacent_find(
      links.begin(), links.end(), [](const LogicalLink& lhs,
                                     const LogicalLink& rhs) {
        return lhs == rhs;
      });
  if(duplicate != links.end()) {
    return std::unexpected(
        Error{ErrorCode::kAlreadyExists,
              "LogicalTopology contains a duplicate directed link"});
  }

  return LogicalTopology{std::move(links), std::move(*node_ids)};
}

inline auto LogicalTopology::HasLink(NodeId source_node_id,
                                     NodeId target_node_id) const noexcept
    -> bool {
  const LogicalLink candidate{source_node_id, target_node_id};
  return std::binary_search(links_.begin(), links_.end(), candidate, Less);
}

}  // namespace ns3_factory::contracts
