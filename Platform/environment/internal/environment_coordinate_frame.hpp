#pragma once

#include <cmath>
#include <cstdint>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/state.hpp>

namespace ns3_factory::environment::internal {

enum class VerticalAxisDirection : std::uint8_t {
  kPositiveUp = 1,
  kPositiveDown = 2,
};

class EnvironmentCoordinateFrame final {
 public:
  [[nodiscard]] static auto Create(double surface_z_meters,
                                   VerticalAxisDirection vertical_direction)
      -> contracts::Result<EnvironmentCoordinateFrame> {
    if(!std::isfinite(surface_z_meters)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Environment surface z must be finite"});
    }
    if(vertical_direction != VerticalAxisDirection::kPositiveUp &&
       vertical_direction != VerticalAxisDirection::kPositiveDown) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Environment vertical-axis direction is invalid"});
    }
    return EnvironmentCoordinateFrame{surface_z_meters,
                                      vertical_direction};
  }

  [[nodiscard]] auto DepthMeters(
      const contracts::Position3d& position) const
      -> contracts::Result<double> {
    if(!std::isfinite(position.z_meters)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Position z must be finite"});
    }
    const auto depth =
        vertical_direction_ == VerticalAxisDirection::kPositiveUp
            ? surface_z_meters_ - position.z_meters
            : position.z_meters - surface_z_meters_;
    if(!std::isfinite(depth)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Coordinate-to-depth conversion overflowed"});
    }
    if(depth < 0.0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Position lies above the configured surface"});
    }
    return depth;
  }

  [[nodiscard]] constexpr auto surface_z_meters() const noexcept -> double {
    return surface_z_meters_;
  }

  [[nodiscard]] constexpr auto vertical_direction() const noexcept
      -> VerticalAxisDirection {
    return vertical_direction_;
  }

  auto operator==(const EnvironmentCoordinateFrame&) const -> bool = default;

 private:
  constexpr EnvironmentCoordinateFrame(
      double surface_z_meters,
      VerticalAxisDirection vertical_direction) noexcept
      : surface_z_meters_(surface_z_meters),
        vertical_direction_(vertical_direction) {}

  double surface_z_meters_;
  VerticalAxisDirection vertical_direction_;
};

[[nodiscard]] inline auto HorizontalRangeMeters(
    const contracts::Position3d& tx,
    const contracts::Position3d& rx) -> contracts::Result<double> {
  if(!std::isfinite(tx.x_meters) || !std::isfinite(tx.y_meters) ||
     !std::isfinite(rx.x_meters) || !std::isfinite(rx.y_meters)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Horizontal positions must be finite"});
  }
  const auto range =
      std::hypot(tx.x_meters - rx.x_meters, tx.y_meters - rx.y_meters);
  if(!std::isfinite(range)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Horizontal range calculation overflowed"});
  }
  return range;
}

}  // namespace ns3_factory::environment::internal
