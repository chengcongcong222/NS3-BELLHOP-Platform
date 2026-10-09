#pragma once

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::planning::internal {

class AdaptiveTdmaPolicy final {
 public:
  [[nodiscard]] static auto Create(contracts::SimDuration frame_duration,
                                   contracts::SimDuration safety_margin)
      -> contracts::Result<AdaptiveTdmaPolicy> {
    if(frame_duration <= contracts::SimDuration::Zero() ||
       safety_margin < contracts::SimDuration::Zero()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Adaptive TDMA requires a positive frame and non-negative safety margin"});
    }
    return AdaptiveTdmaPolicy{frame_duration, safety_margin};
  }
  auto frame_duration() const noexcept -> contracts::SimDuration { return frame_; }
  auto safety_margin() const noexcept -> contracts::SimDuration { return margin_; }

 private:
  AdaptiveTdmaPolicy(contracts::SimDuration frame, contracts::SimDuration margin)
      : frame_(frame), margin_(margin) {}
  contracts::SimDuration frame_;
  contracts::SimDuration margin_;
};

}  // namespace ns3_factory::planning::internal
