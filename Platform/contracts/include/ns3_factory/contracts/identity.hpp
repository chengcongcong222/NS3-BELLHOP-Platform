#pragma once

#include <compare>

#include <ns3_factory/contracts/common.hpp>

namespace ns3_factory::contracts {

namespace detail {

template <typename Domain>
class StrongId final {
 public:
  using value_type = IdentityValue;

  StrongId() = delete;

  constexpr explicit StrongId(value_type value) noexcept : value_(value) {}

  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return value_;
  }

  constexpr auto operator<=>(const StrongId&) const noexcept = default;

 private:
  value_type value_;
};

struct NodeIdDomain;
struct PacketIdDomain;
struct TransmissionIdDomain;
struct ReceptionIdDomain;
struct PlanningCycleIdDomain;
struct SnapshotVersionDomain;
struct FlowIdDomain;
struct GrantIdDomain;

}  // namespace detail

using NodeId = detail::StrongId<detail::NodeIdDomain>;
using PacketId = detail::StrongId<detail::PacketIdDomain>;
using TransmissionId = detail::StrongId<detail::TransmissionIdDomain>;
using ReceptionId = detail::StrongId<detail::ReceptionIdDomain>;
using PlanningCycleId = detail::StrongId<detail::PlanningCycleIdDomain>;
using SnapshotVersion = detail::StrongId<detail::SnapshotVersionDomain>;
using FlowId = detail::StrongId<detail::FlowIdDomain>;
using GrantId = detail::StrongId<detail::GrantIdDomain>;

}  // namespace ns3_factory::contracts
