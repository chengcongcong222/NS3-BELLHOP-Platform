#pragma once

#include <cmath>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

// Environmental noise query over half-open simulation-time and frequency
// intervals at one explicitly positioned receiver.
class NoiseQuery final {
 public:
  [[nodiscard]] static auto Create(NodeId receiver_node_id,
                                   Position3d receiver_position,
                                   SimTime observed_from,
                                   SimTime observed_until,
                                   double lower_frequency_hz,
                                   double upper_frequency_hz)
      -> Result<NoiseQuery>;

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  [[nodiscard]] constexpr auto receiver_position() const noexcept
      -> const Position3d& {
    return receiver_position_;
  }

  [[nodiscard]] constexpr auto observed_from() const noexcept -> SimTime {
    return observed_from_;
  }

  [[nodiscard]] constexpr auto observed_until() const noexcept -> SimTime {
    return observed_until_;
  }

  [[nodiscard]] constexpr auto lower_frequency_hz() const noexcept
      -> double {
    return lower_frequency_hz_;
  }

  [[nodiscard]] constexpr auto upper_frequency_hz() const noexcept
      -> double {
    return upper_frequency_hz_;
  }

  auto operator==(const NoiseQuery&) const -> bool = default;

 private:
  constexpr NoiseQuery(NodeId receiver_node_id,
                       Position3d receiver_position,
                       SimTime observed_from,
                       SimTime observed_until,
                       double lower_frequency_hz,
                       double upper_frequency_hz) noexcept
      : receiver_node_id_(receiver_node_id),
        receiver_position_(receiver_position),
        observed_from_(observed_from),
        observed_until_(observed_until),
        lower_frequency_hz_(lower_frequency_hz),
        upper_frequency_hz_(upper_frequency_hz) {}

  NodeId receiver_node_id_;
  Position3d receiver_position_;
  SimTime observed_from_;
  SimTime observed_until_;
  double lower_frequency_hz_;
  double upper_frequency_hz_;
};

inline auto NoiseQuery::Create(NodeId receiver_node_id,
                               Position3d receiver_position,
                               SimTime observed_from,
                               SimTime observed_until,
                               double lower_frequency_hz,
                               double upper_frequency_hz)
    -> Result<NoiseQuery> {
  if(!std::isfinite(receiver_position.x_meters) ||
     !std::isfinite(receiver_position.y_meters) ||
     !std::isfinite(receiver_position.z_meters) ||
     !std::isfinite(lower_frequency_hz) ||
     !std::isfinite(upper_frequency_hz)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "NoiseQuery position and frequency boundaries must be finite"});
  }
  if(observed_until <= observed_from) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "NoiseQuery observation interval must have positive width"});
  }
  if(lower_frequency_hz < 0.0 ||
     upper_frequency_hz <= lower_frequency_hz) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "NoiseQuery frequency interval is invalid"});
  }

  return NoiseQuery{receiver_node_id,
                    receiver_position,
                    observed_from,
                    observed_until,
                    lower_frequency_hz,
                    upper_frequency_hz};
}

// Equivalent in-band acoustic pressure-squared noise power level over the
// exact observation interval and band, referenced to 1 uPa^2. This is not a
// PSD, waveform sample, or amplitude gain.
class NoiseObservation final {
 public:
  [[nodiscard]] static auto Create(
      NodeId receiver_node_id,
      SimTime observed_from,
      SimTime observed_until,
      double lower_frequency_hz,
      double upper_frequency_hz,
      double equivalent_noise_power_db_re_1upa2)
      -> Result<NoiseObservation>;

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  [[nodiscard]] constexpr auto observed_from() const noexcept -> SimTime {
    return observed_from_;
  }

  [[nodiscard]] constexpr auto observed_until() const noexcept -> SimTime {
    return observed_until_;
  }

  [[nodiscard]] constexpr auto lower_frequency_hz() const noexcept
      -> double {
    return lower_frequency_hz_;
  }

  [[nodiscard]] constexpr auto upper_frequency_hz() const noexcept
      -> double {
    return upper_frequency_hz_;
  }

  [[nodiscard]] constexpr auto equivalent_noise_power_db_re_1upa2()
      const noexcept -> double {
    return equivalent_noise_power_db_re_1upa2_;
  }

  auto operator==(const NoiseObservation&) const -> bool = default;

 private:
  constexpr NoiseObservation(
      NodeId receiver_node_id,
      SimTime observed_from,
      SimTime observed_until,
      double lower_frequency_hz,
      double upper_frequency_hz,
      double equivalent_noise_power_db_re_1upa2) noexcept
      : receiver_node_id_(receiver_node_id),
        observed_from_(observed_from),
        observed_until_(observed_until),
        lower_frequency_hz_(lower_frequency_hz),
        upper_frequency_hz_(upper_frequency_hz),
        equivalent_noise_power_db_re_1upa2_(
            equivalent_noise_power_db_re_1upa2) {}

  NodeId receiver_node_id_;
  SimTime observed_from_;
  SimTime observed_until_;
  double lower_frequency_hz_;
  double upper_frequency_hz_;
  double equivalent_noise_power_db_re_1upa2_;
};

inline auto NoiseObservation::Create(
    NodeId receiver_node_id,
    SimTime observed_from,
    SimTime observed_until,
    double lower_frequency_hz,
    double upper_frequency_hz,
    double equivalent_noise_power_db_re_1upa2)
    -> Result<NoiseObservation> {
  if(!std::isfinite(lower_frequency_hz) ||
     !std::isfinite(upper_frequency_hz) ||
     !std::isfinite(equivalent_noise_power_db_re_1upa2)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "NoiseObservation frequency and power must be finite"});
  }
  if(observed_until <= observed_from) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "NoiseObservation interval must have positive width"});
  }
  if(lower_frequency_hz < 0.0 ||
     upper_frequency_hz <= lower_frequency_hz) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "NoiseObservation frequency interval is invalid"});
  }

  return NoiseObservation{receiver_node_id,
                          observed_from,
                          observed_until,
                          lower_frequency_hz,
                          upper_frequency_hz,
                          equivalent_noise_power_db_re_1upa2};
}

[[nodiscard]] inline auto ValidateNoiseObservationIdentity(
    const NoiseQuery& query,
    const NoiseObservation& observation) -> Status {
  if(query.receiver_node_id() != observation.receiver_node_id() ||
     query.observed_from() != observation.observed_from() ||
     query.observed_until() != observation.observed_until() ||
     query.lower_frequency_hz() != observation.lower_frequency_hz() ||
     query.upper_frequency_hz() != observation.upper_frequency_hz()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "NoiseObservation provenance does not match NoiseQuery"});
  }
  return {};
}

class INoiseFieldProvider {
 public:
  virtual ~INoiseFieldProvider() = default;

  [[nodiscard]] virtual auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> = 0;
};

}  // namespace ns3_factory::contracts
