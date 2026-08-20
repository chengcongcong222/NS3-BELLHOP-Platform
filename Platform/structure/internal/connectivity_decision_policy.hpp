#pragma once

#include <cmath>
#include <optional>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::structure::internal {

class ConnectivityDecisionPolicy final {
 public:
  [[nodiscard]] static auto Create(
      std::optional<double> max_coarse_range_m,
      double enter_threshold,
      double keep_threshold) -> contracts::Result<ConnectivityDecisionPolicy>;

  [[nodiscard]] constexpr auto max_coarse_range_m() const noexcept
      -> std::optional<double> {
    return max_coarse_range_m_;
  }

  [[nodiscard]] constexpr auto enter_threshold() const noexcept -> double {
    return enter_threshold_;
  }

  [[nodiscard]] constexpr auto keep_threshold() const noexcept -> double {
    return keep_threshold_;
  }

  auto operator==(const ConnectivityDecisionPolicy&) const
      -> bool = default;

 private:
  constexpr ConnectivityDecisionPolicy(
      std::optional<double> max_coarse_range_m,
      double enter_threshold,
      double keep_threshold) noexcept
      : max_coarse_range_m_(max_coarse_range_m),
        enter_threshold_(enter_threshold),
        keep_threshold_(keep_threshold) {}

  std::optional<double> max_coarse_range_m_;
  double enter_threshold_;
  double keep_threshold_;
};

inline auto ConnectivityDecisionPolicy::Create(
    std::optional<double> max_coarse_range_m,
    double enter_threshold,
    double keep_threshold) -> contracts::Result<ConnectivityDecisionPolicy> {
  if(!std::isfinite(enter_threshold) ||
     !std::isfinite(keep_threshold) ||
     (max_coarse_range_m && !std::isfinite(*max_coarse_range_m))) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Connectivity decision values must be finite"});
  }
  if(max_coarse_range_m && *max_coarse_range_m <= 0.0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Coarse range must be positive when present"});
  }
  if(enter_threshold < keep_threshold) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Enter threshold must not be below keep threshold"});
  }
  return ConnectivityDecisionPolicy{
      max_coarse_range_m, enter_threshold, keep_threshold};
}

}  // namespace ns3_factory::structure::internal
