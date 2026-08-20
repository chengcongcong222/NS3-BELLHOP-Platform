#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/detail/node_universe.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

namespace ns3_factory::contracts {

enum class ProtocolRole : std::uint8_t {
  kMember = 1,
  kSink = 2,
  kController = 3,
  kRelay = 4,
  kAccessNode = 5,
  kAnchor = 6,
};

struct RoleBinding final {
  NodeId node_id;
  ProtocolRole role;

  constexpr auto operator==(const RoleBinding&) const noexcept
      -> bool = default;
};

class RoleTable final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<RoleBinding> bindings,
      std::vector<NodeId> valid_node_ids) -> Result<RoleTable>;

  [[nodiscard]] auto bindings() const noexcept
      -> std::span<const RoleBinding> {
    return std::span<const RoleBinding>{bindings_};
  }

  [[nodiscard]] auto node_ids() const noexcept
      -> std::span<const NodeId> {
    return std::span<const NodeId>{node_ids_};
  }

  [[nodiscard]] auto HasRole(NodeId node_id,
                             ProtocolRole role) const noexcept -> bool;

  [[nodiscard]] auto NodesWithRole(ProtocolRole role) const
      -> std::vector<NodeId>;

  auto operator==(const RoleTable&) const -> bool = default;

 private:
  RoleTable(std::vector<RoleBinding> bindings,
            std::vector<NodeId> node_ids) noexcept
      : bindings_(std::move(bindings)), node_ids_(std::move(node_ids)) {}

  [[nodiscard]] static constexpr auto IsValidRole(
      ProtocolRole role) noexcept -> bool;

  [[nodiscard]] static constexpr auto Less(const RoleBinding& lhs,
                                            const RoleBinding& rhs) noexcept
      -> bool;

  std::vector<RoleBinding> bindings_;
  std::vector<NodeId> node_ids_;
};

inline constexpr auto RoleTable::IsValidRole(ProtocolRole role) noexcept
    -> bool {
  switch(role) {
    case ProtocolRole::kMember:
    case ProtocolRole::kSink:
    case ProtocolRole::kController:
    case ProtocolRole::kRelay:
    case ProtocolRole::kAccessNode:
    case ProtocolRole::kAnchor:
      return true;
  }
  return false;
}

inline constexpr auto RoleTable::Less(const RoleBinding& lhs,
                                      const RoleBinding& rhs) noexcept
    -> bool {
  if(lhs.node_id != rhs.node_id) {
    return lhs.node_id < rhs.node_id;
  }
  return static_cast<std::uint8_t>(lhs.role) <
         static_cast<std::uint8_t>(rhs.role);
}

inline auto RoleTable::Create(std::vector<RoleBinding> bindings,
                              std::vector<NodeId> valid_node_ids)
    -> Result<RoleTable> {
  auto node_ids =
      detail::CanonicalizeNodeUniverse(std::move(valid_node_ids));
  if(!node_ids) {
    return std::unexpected(node_ids.error());
  }

  for(const auto& binding : bindings) {
    if(!IsValidRole(binding.role)) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "RoleTable contains an unknown ProtocolRole"});
    }
    if(!detail::ContainsNode(*node_ids, binding.node_id)) {
      return std::unexpected(
          Error{ErrorCode::kNotFound,
                "RoleTable binding references an unknown NodeId"});
    }
  }

  std::sort(bindings.begin(), bindings.end(), Less);
  const auto duplicate = std::adjacent_find(
      bindings.begin(), bindings.end(), [](const RoleBinding& lhs,
                                           const RoleBinding& rhs) {
        return lhs == rhs;
      });
  if(duplicate != bindings.end()) {
    return std::unexpected(
        Error{ErrorCode::kAlreadyExists,
              "RoleTable contains a duplicate role binding"});
  }

  return RoleTable{std::move(bindings), std::move(*node_ids)};
}

inline auto RoleTable::HasRole(NodeId node_id,
                               ProtocolRole role) const noexcept -> bool {
  const RoleBinding candidate{node_id, role};
  return std::binary_search(
      bindings_.begin(), bindings_.end(), candidate, Less);
}

inline auto RoleTable::NodesWithRole(ProtocolRole role) const
    -> std::vector<NodeId> {
  std::vector<NodeId> nodes;
  for(const auto& binding : bindings_) {
    if(binding.role == role) {
      nodes.push_back(binding.node_id);
    }
  }
  return nodes;
}

}  // namespace ns3_factory::contracts
