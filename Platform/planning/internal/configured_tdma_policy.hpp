#pragma once

#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::planning::internal {

class ConfiguredTdmaPolicy final {
 public:
  [[nodiscard]] static auto Create(
      contracts::SimDuration slot_duration,
      std::vector<contracts::NodeId> slot_owners)
      -> contracts::Result<ConfiguredTdmaPolicy> {
    return Create(slot_duration,
                  contracts::SimDuration::Zero(),
                  std::move(slot_owners));
  }

  [[nodiscard]] static auto Create(
      contracts::SimDuration slot_duration,
      contracts::SimDuration guard_interval,
      std::vector<contracts::NodeId> slot_owners)
      -> contracts::Result<ConfiguredTdmaPolicy>;

  [[nodiscard]] constexpr auto slot_duration() const noexcept
      -> contracts::SimDuration {
    return slot_duration_;
  }

  [[nodiscard]] auto slot_owners() const noexcept
      -> std::span<const contracts::NodeId> {
    return std::span<const contracts::NodeId>{slot_owners_};
  }

  [[nodiscard]] constexpr auto guard_interval() const noexcept
      -> contracts::SimDuration {
    return guard_interval_;
  }

  auto operator==(const ConfiguredTdmaPolicy&) const -> bool = default;

 private:
  ConfiguredTdmaPolicy(
      contracts::SimDuration slot_duration,
      contracts::SimDuration guard_interval,
      std::vector<contracts::NodeId> slot_owners) noexcept
      : slot_duration_(slot_duration),
        guard_interval_(guard_interval),
        slot_owners_(std::move(slot_owners)) {}

  contracts::SimDuration slot_duration_;
  contracts::SimDuration guard_interval_;
  std::vector<contracts::NodeId> slot_owners_;
};

inline auto ConfiguredTdmaPolicy::Create(
    contracts::SimDuration slot_duration,
    contracts::SimDuration guard_interval,
    std::vector<contracts::NodeId> slot_owners)
    -> contracts::Result<ConfiguredTdmaPolicy> {
  if(slot_duration <= contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "TDMA slot duration must be positive"});
  }
  if(guard_interval < contracts::SimDuration::Zero() ||
     guard_interval >= slot_duration) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "TDMA guard interval must be non-negative and leave a positive "
            "transmission window"});
  }
  if(slot_owners.empty()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "TDMA slot owner sequence must not be empty"});
  }
  return ConfiguredTdmaPolicy{
      slot_duration, guard_interval, std::move(slot_owners)};
}

}  // namespace ns3_factory::planning::internal
