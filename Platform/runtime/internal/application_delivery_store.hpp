#pragma once

#include <algorithm>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

#include "internal/reception_disposition.hpp"

namespace ns3_factory::runtime::internal {

class ApplicationDeliveryStore final {
 public:
  [[nodiscard]] static auto Create(
      std::vector<contracts::NodeId> node_ids)
      -> contracts::Result<ApplicationDeliveryStore>;

  ApplicationDeliveryStore(const ApplicationDeliveryStore&) = delete;
  auto operator=(const ApplicationDeliveryStore&)
      -> ApplicationDeliveryStore& = delete;
  ApplicationDeliveryStore(ApplicationDeliveryStore&&) noexcept = default;
  auto operator=(ApplicationDeliveryStore&&) noexcept
      -> ApplicationDeliveryStore& = default;

  [[nodiscard]] auto Append(LocalDeliveryReception delivery)
      -> contracts::Status;

  [[nodiscard]] auto deliveries() const noexcept
      -> std::span<const LocalDeliveryReception> {
    return std::span<const LocalDeliveryReception>{deliveries_};
  }

  [[nodiscard]] auto node_ids() const noexcept
      -> std::span<const contracts::NodeId> {
    return std::span<const contracts::NodeId>{node_ids_};
  }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return deliveries_.size();
  }

 private:
  explicit ApplicationDeliveryStore(
      std::vector<contracts::NodeId> node_ids) noexcept
      : node_ids_(std::move(node_ids)) {}

  [[nodiscard]] static auto CanonicalLess(
      const LocalDeliveryReception& lhs,
      const LocalDeliveryReception& rhs) noexcept -> bool {
    if(lhs.receiver_node_id != rhs.receiver_node_id) {
      return lhs.receiver_node_id < rhs.receiver_node_id;
    }
    if(lhs.reception_id != rhs.reception_id) {
      return lhs.reception_id < rhs.reception_id;
    }
    return lhs.transmission_id < rhs.transmission_id;
  }

  std::vector<contracts::NodeId> node_ids_;
  std::vector<LocalDeliveryReception> deliveries_;
};

inline auto ApplicationDeliveryStore::Create(
    std::vector<contracts::NodeId> node_ids)
    -> contracts::Result<ApplicationDeliveryStore> {
  std::sort(node_ids.begin(), node_ids.end());
  if(std::adjacent_find(node_ids.begin(), node_ids.end()) !=
     node_ids.end()) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kAlreadyExists,
            "ApplicationDeliveryStore node universe contains a duplicate "
            "NodeId"});
  }
  return ApplicationDeliveryStore{std::move(node_ids)};
}

inline auto ApplicationDeliveryStore::Append(
    LocalDeliveryReception delivery) -> contracts::Status {
  if(!std::binary_search(node_ids_.begin(),
                         node_ids_.end(),
                         delivery.receiver_node_id)) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kNotFound,
            "Application delivery receiver is outside the node universe"});
  }
  if(std::any_of(deliveries_.begin(),
                 deliveries_.end(),
                 [&](const LocalDeliveryReception& existing) {
                   return existing.reception_id == delivery.reception_id;
                 })) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kAlreadyExists,
            "ReceptionId is already present in application deliveries"});
  }

  deliveries_.push_back(std::move(delivery));
  std::sort(deliveries_.begin(), deliveries_.end(), CanonicalLess);
  return {};
}

}  // namespace ns3_factory::runtime::internal
