#pragma once

#include <compare>
#include <concepts>
#include <limits>
#include <optional>

#include <ns3_factory/contracts/common.hpp>

namespace ns3_factory::contracts {

namespace detail {

[[nodiscard]] constexpr auto CheckedAddCounts(NanosecondCount lhs,
                                               NanosecondCount rhs) noexcept
    -> std::optional<NanosecondCount> {
  constexpr auto kMin = std::numeric_limits<NanosecondCount>::min();
  constexpr auto kMax = std::numeric_limits<NanosecondCount>::max();

  if((rhs > 0 && lhs > kMax - rhs) ||
     (rhs < 0 && lhs < kMin - rhs)) {
    return std::nullopt;
  }
  return lhs + rhs;
}

[[nodiscard]] constexpr auto CheckedSubtractCounts(
    NanosecondCount lhs,
    NanosecondCount rhs) noexcept -> std::optional<NanosecondCount> {
  constexpr auto kMin = std::numeric_limits<NanosecondCount>::min();
  constexpr auto kMax = std::numeric_limits<NanosecondCount>::max();

  if((rhs > 0 && lhs < kMin + rhs) ||
     (rhs < 0 && lhs > kMax + rhs)) {
    return std::nullopt;
  }
  return lhs - rhs;
}

[[nodiscard]] constexpr auto CheckedScaleNanoseconds(
    NanosecondCount value,
    NanosecondCount scale) noexcept -> std::optional<NanosecondCount> {
  constexpr auto kMin = std::numeric_limits<NanosecondCount>::min();
  constexpr auto kMax = std::numeric_limits<NanosecondCount>::max();

  if(value > kMax / scale || value < kMin / scale) {
    return std::nullopt;
  }
  return value * scale;
}

}  // namespace detail

class SimDuration final {
 public:
  using representation_type = NanosecondCount;

  // Negative values are valid offsets and differences.

  [[nodiscard]] static constexpr auto Zero() noexcept -> SimDuration {
    return FromNanoseconds(0);
  }

  [[nodiscard]] static constexpr auto FromNanoseconds(
      representation_type value) noexcept -> SimDuration {
    return SimDuration(value);
  }

  template <std::floating_point FloatingPoint>
  static constexpr auto FromNanoseconds(FloatingPoint) noexcept
      -> SimDuration = delete;

  [[nodiscard]] static constexpr auto TryFromMicroseconds(
      representation_type value) noexcept -> std::optional<SimDuration> {
    return FromScaledNanoseconds(value, 1'000);
  }

  template <std::floating_point FloatingPoint>
  static constexpr auto TryFromMicroseconds(FloatingPoint) noexcept
      -> std::optional<SimDuration> = delete;

  [[nodiscard]] static constexpr auto TryFromMilliseconds(
      representation_type value) noexcept -> std::optional<SimDuration> {
    return FromScaledNanoseconds(value, 1'000'000);
  }

  template <std::floating_point FloatingPoint>
  static constexpr auto TryFromMilliseconds(FloatingPoint) noexcept
      -> std::optional<SimDuration> = delete;

  [[nodiscard]] static constexpr auto TryFromSeconds(
      representation_type value) noexcept -> std::optional<SimDuration> {
    return FromScaledNanoseconds(value, 1'000'000'000);
  }

  template <std::floating_point FloatingPoint>
  static constexpr auto TryFromSeconds(FloatingPoint) noexcept
      -> std::optional<SimDuration> = delete;

  [[nodiscard]] constexpr auto nanoseconds() const noexcept
      -> representation_type {
    return nanoseconds_;
  }

  constexpr auto operator<=>(const SimDuration&) const noexcept = default;

 private:
  constexpr explicit SimDuration(representation_type value) noexcept
      : nanoseconds_(value) {}

  [[nodiscard]] static constexpr auto FromScaledNanoseconds(
      representation_type value,
      representation_type scale) noexcept -> std::optional<SimDuration> {
    const auto result = detail::CheckedScaleNanoseconds(value, scale);
    if(!result) {
      return std::nullopt;
    }
    return FromNanoseconds(*result);
  }

  representation_type nanoseconds_;
};

class SimTime final {
 public:
  using representation_type = NanosecondCount;

  // The signed representation is complete. Whether M1 accepts a negative
  // timestamp for scheduling is a kernel policy, not a value-type rule.

  [[nodiscard]] static constexpr auto Zero() noexcept -> SimTime {
    return FromNanoseconds(0);
  }

  [[nodiscard]] static constexpr auto FromNanoseconds(
      representation_type value) noexcept -> SimTime {
    return SimTime(value);
  }

  template <std::floating_point FloatingPoint>
  static constexpr auto FromNanoseconds(FloatingPoint) noexcept
      -> SimTime = delete;

  [[nodiscard]] constexpr auto nanoseconds() const noexcept
      -> representation_type {
    return nanoseconds_;
  }

  constexpr auto operator<=>(const SimTime&) const noexcept = default;

 private:
  constexpr explicit SimTime(representation_type value) noexcept
      : nanoseconds_(value) {}

  representation_type nanoseconds_;
};

[[nodiscard]] constexpr auto CheckedAdd(SimDuration lhs,
                                        SimDuration rhs) noexcept
    -> std::optional<SimDuration> {
  const auto result =
      detail::CheckedAddCounts(lhs.nanoseconds(), rhs.nanoseconds());
  if(!result) {
    return std::nullopt;
  }
  return SimDuration::FromNanoseconds(*result);
}

[[nodiscard]] constexpr auto CheckedSubtract(SimDuration lhs,
                                             SimDuration rhs) noexcept
    -> std::optional<SimDuration> {
  const auto result =
      detail::CheckedSubtractCounts(lhs.nanoseconds(), rhs.nanoseconds());
  if(!result) {
    return std::nullopt;
  }
  return SimDuration::FromNanoseconds(*result);
}

[[nodiscard]] constexpr auto CheckedAdd(SimTime time,
                                        SimDuration duration) noexcept
    -> std::optional<SimTime> {
  const auto result =
      detail::CheckedAddCounts(time.nanoseconds(), duration.nanoseconds());
  if(!result) {
    return std::nullopt;
  }
  return SimTime::FromNanoseconds(*result);
}

[[nodiscard]] constexpr auto CheckedSubtract(SimTime time,
                                             SimDuration duration) noexcept
    -> std::optional<SimTime> {
  const auto result =
      detail::CheckedSubtractCounts(time.nanoseconds(), duration.nanoseconds());
  if(!result) {
    return std::nullopt;
  }
  return SimTime::FromNanoseconds(*result);
}

[[nodiscard]] constexpr auto CheckedSubtract(SimTime lhs,
                                             SimTime rhs) noexcept
    -> std::optional<SimDuration> {
  const auto result =
      detail::CheckedSubtractCounts(lhs.nanoseconds(), rhs.nanoseconds());
  if(!result) {
    return std::nullopt;
  }
  return SimDuration::FromNanoseconds(*result);
}

}  // namespace ns3_factory::contracts
