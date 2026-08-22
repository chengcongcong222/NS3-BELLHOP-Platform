#pragma once

#include <cmath>
#include <cstdint>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::environment::internal::import {

// Lossless P0 representation of one record in the confirmed Bellhop 2D
// ASCII arrival dialect. These values have not been normalized into channel
// pressure gain, phase radians, propagation paths, or aggregate loss.
class BellhopRawArrival final {
 public:
  [[nodiscard]] static auto Create(double raw_magnitude,
                                   double raw_phase_degrees,
                                   double raw_delay_real_seconds,
                                   double raw_delay_imag_seconds,
                                   double raw_source_angle_degrees,
                                   double raw_receiver_angle_degrees,
                                   std::uint32_t raw_top_bounce_count,
                                   std::uint32_t raw_bottom_bounce_count)
      -> contracts::Result<BellhopRawArrival> {
    if(!std::isfinite(raw_magnitude) ||
       !std::isfinite(raw_phase_degrees) ||
       !std::isfinite(raw_delay_real_seconds) ||
       !std::isfinite(raw_delay_imag_seconds) ||
       !std::isfinite(raw_source_angle_degrees) ||
       !std::isfinite(raw_receiver_angle_degrees)) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Bellhop raw arrival numeric fields must be "
                           "finite"});
    }
    if(raw_magnitude < 0.0 || raw_delay_real_seconds < 0.0) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Bellhop raw magnitude and real delay must be "
                           "non-negative"});
    }
    return BellhopRawArrival{raw_magnitude,
                             raw_phase_degrees,
                             raw_delay_real_seconds,
                             raw_delay_imag_seconds,
                             raw_source_angle_degrees,
                             raw_receiver_angle_degrees,
                             raw_top_bounce_count,
                             raw_bottom_bounce_count};
  }

  [[nodiscard]] constexpr auto raw_magnitude() const noexcept -> double {
    return raw_magnitude_;
  }

  [[nodiscard]] constexpr auto raw_phase_degrees() const noexcept -> double {
    return raw_phase_degrees_;
  }

  [[nodiscard]] constexpr auto raw_delay_real_seconds() const noexcept
      -> double {
    return raw_delay_real_seconds_;
  }

  [[nodiscard]] constexpr auto raw_delay_imag_seconds() const noexcept
      -> double {
    return raw_delay_imag_seconds_;
  }

  [[nodiscard]] constexpr auto raw_source_angle_degrees() const noexcept
      -> double {
    return raw_source_angle_degrees_;
  }

  [[nodiscard]] constexpr auto raw_receiver_angle_degrees() const noexcept
      -> double {
    return raw_receiver_angle_degrees_;
  }

  [[nodiscard]] constexpr auto raw_top_bounce_count() const noexcept
      -> std::uint32_t {
    return raw_top_bounce_count_;
  }

  [[nodiscard]] constexpr auto raw_bottom_bounce_count() const noexcept
      -> std::uint32_t {
    return raw_bottom_bounce_count_;
  }

  auto operator==(const BellhopRawArrival&) const -> bool = default;

 private:
  constexpr BellhopRawArrival(double raw_magnitude,
                              double raw_phase_degrees,
                              double raw_delay_real_seconds,
                              double raw_delay_imag_seconds,
                              double raw_source_angle_degrees,
                              double raw_receiver_angle_degrees,
                              std::uint32_t raw_top_bounce_count,
                              std::uint32_t raw_bottom_bounce_count) noexcept
      : raw_magnitude_(raw_magnitude),
        raw_phase_degrees_(raw_phase_degrees),
        raw_delay_real_seconds_(raw_delay_real_seconds),
        raw_delay_imag_seconds_(raw_delay_imag_seconds),
        raw_source_angle_degrees_(raw_source_angle_degrees),
        raw_receiver_angle_degrees_(raw_receiver_angle_degrees),
        raw_top_bounce_count_(raw_top_bounce_count),
        raw_bottom_bounce_count_(raw_bottom_bounce_count) {}

  double raw_magnitude_;
  double raw_phase_degrees_;
  double raw_delay_real_seconds_;
  double raw_delay_imag_seconds_;
  double raw_source_angle_degrees_;
  double raw_receiver_angle_degrees_;
  std::uint32_t raw_top_bounce_count_;
  std::uint32_t raw_bottom_bounce_count_;
};

}  // namespace ns3_factory::environment::internal::import
