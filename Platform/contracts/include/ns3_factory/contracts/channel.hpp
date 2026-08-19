#pragma once

#include <algorithm>
#include <cmath>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

class PropagationPath final {
 public:
  [[nodiscard]] static auto Create(SimDuration excess_delay,
                                   double pressure_gain_linear,
                                   double phase_radians)
      -> Result<PropagationPath>;

  // Additional propagation delay relative to the containing response's
  // first_arrival_delay. Absolute propagation delay is their checked sum.
  [[nodiscard]] constexpr auto excess_delay() const noexcept
      -> SimDuration {
    return excess_delay_;
  }

  // Absolute, dimensionless linear acoustic pressure transfer magnitude at
  // the query frequency. This is not normalized and may be greater than 1.
  [[nodiscard]] constexpr auto pressure_gain_linear() const noexcept
      -> double {
    return pressure_gain_linear_;
  }

  [[nodiscard]] constexpr auto phase_radians() const noexcept -> double {
    return phase_radians_;
  }

  auto operator==(const PropagationPath&) const -> bool = default;

 private:
  constexpr PropagationPath(SimDuration excess_delay,
                            double pressure_gain_linear,
                            double phase_radians) noexcept
      : excess_delay_(excess_delay),
        pressure_gain_linear_(pressure_gain_linear),
        phase_radians_(phase_radians) {}

  SimDuration excess_delay_;
  double pressure_gain_linear_;
  double phase_radians_;
};

inline auto PropagationPath::Create(SimDuration excess_delay,
                                    double pressure_gain_linear,
                                    double phase_radians)
    -> Result<PropagationPath> {
  if(excess_delay.nanoseconds() < 0 || pressure_gain_linear < 0.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "PropagationPath excess delay and pressure gain must be "
              "non-negative"});
  }
  if(!std::isfinite(pressure_gain_linear) ||
     !std::isfinite(phase_radians)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "PropagationPath pressure gain and phase must be finite"});
  }

  return PropagationPath{excess_delay,
                         pressure_gain_linear,
                         phase_radians};
}

class ChannelQuery final {
 public:
  [[nodiscard]] static auto Create(TransmissionId transmission_id,
                                   NodeId sender_node_id,
                                   NodeId receiver_node_id,
                                   Position3d tx_position,
                                   Position3d rx_position,
                                   SimTime emitted_at,
                                   double center_frequency_hz,
                                   double bandwidth_hz)
      -> Result<ChannelQuery>;

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> TransmissionId {
    return transmission_id_;
  }

  [[nodiscard]] constexpr auto sender_node_id() const noexcept -> NodeId {
    return sender_node_id_;
  }

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  [[nodiscard]] constexpr auto tx_position() const noexcept
      -> const Position3d& {
    return tx_position_;
  }

  [[nodiscard]] constexpr auto rx_position() const noexcept
      -> const Position3d& {
    return rx_position_;
  }

  [[nodiscard]] constexpr auto emitted_at() const noexcept -> SimTime {
    return emitted_at_;
  }

  [[nodiscard]] constexpr auto center_frequency_hz() const noexcept
      -> double {
    return center_frequency_hz_;
  }

  [[nodiscard]] constexpr auto bandwidth_hz() const noexcept -> double {
    return bandwidth_hz_;
  }

  auto operator==(const ChannelQuery&) const -> bool = default;

 private:
  constexpr ChannelQuery(TransmissionId transmission_id,
                         NodeId sender_node_id,
                         NodeId receiver_node_id,
                         Position3d tx_position,
                         Position3d rx_position,
                         SimTime emitted_at,
                         double center_frequency_hz,
                         double bandwidth_hz) noexcept
      : transmission_id_(transmission_id),
        sender_node_id_(sender_node_id),
        receiver_node_id_(receiver_node_id),
        tx_position_(tx_position),
        rx_position_(rx_position),
        emitted_at_(emitted_at),
        center_frequency_hz_(center_frequency_hz),
        bandwidth_hz_(bandwidth_hz) {}

  TransmissionId transmission_id_;
  NodeId sender_node_id_;
  NodeId receiver_node_id_;
  Position3d tx_position_;
  Position3d rx_position_;
  SimTime emitted_at_;
  double center_frequency_hz_;
  double bandwidth_hz_;
};

inline auto ChannelQuery::Create(TransmissionId transmission_id,
                                 NodeId sender_node_id,
                                 NodeId receiver_node_id,
                                 Position3d tx_position,
                                 Position3d rx_position,
                                 SimTime emitted_at,
                                 double center_frequency_hz,
                                 double bandwidth_hz)
    -> Result<ChannelQuery> {
  const auto position_is_finite = [](const Position3d& position) {
    return std::isfinite(position.x_meters) &&
           std::isfinite(position.y_meters) &&
           std::isfinite(position.z_meters);
  };

  if(!position_is_finite(tx_position) || !position_is_finite(rx_position) ||
     !std::isfinite(center_frequency_hz) ||
     !std::isfinite(bandwidth_hz)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "ChannelQuery positions and physical values must be finite"});
  }
  if(center_frequency_hz <= 0.0 || bandwidth_hz <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "ChannelQuery frequency and bandwidth must be positive"});
  }

  return ChannelQuery{transmission_id,
                      sender_node_id,
                      receiver_node_id,
                      tx_position,
                      rx_position,
                      emitted_at,
                      center_frequency_hz,
                      bandwidth_hz};
}

class ChannelFieldResponse final {
 public:
  [[nodiscard]] static auto Create(TransmissionId transmission_id,
                                   NodeId receiver_node_id,
                                   double aggregate_transmission_loss_db,
                                   SimDuration first_arrival_delay,
                                   std::vector<PropagationPath> paths)
      -> Result<ChannelFieldResponse>;

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> TransmissionId {
    return transmission_id_;
  }

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  // Scalar aggregate/fallback representation of the channel transfer.
  // Scalar processing may compute source level minus this loss. Path-aware
  // processing instead uses each path's absolute pressure gain, phase, and
  // delay; it must not also apply this aggregate loss to the same signal.
  [[nodiscard]] constexpr auto aggregate_transmission_loss_db() const noexcept
      -> double {
    return aggregate_transmission_loss_db_;
  }

  // Propagation time from the transmission's started_at to the receiver's
  // earliest physical path arrival.
  [[nodiscard]] constexpr auto first_arrival_delay() const noexcept
      -> SimDuration {
    return first_arrival_delay_;
  }

  // Empty paths represent a valid scalar-only response. Non-empty paths are
  // canonicalized by excess delay, pressure gain, then phase, all ascending,
  // and always contain at least one zero-excess-delay path.
  [[nodiscard]] auto paths() const noexcept
      -> std::span<const PropagationPath> {
    return std::span<const PropagationPath>{paths_};
  }

  auto operator==(const ChannelFieldResponse&) const -> bool = default;

 private:
  ChannelFieldResponse(TransmissionId transmission_id,
                       NodeId receiver_node_id,
                       double aggregate_transmission_loss_db,
                       SimDuration first_arrival_delay,
                       std::vector<PropagationPath> paths)
      : transmission_id_(transmission_id),
        receiver_node_id_(receiver_node_id),
        aggregate_transmission_loss_db_(aggregate_transmission_loss_db),
        first_arrival_delay_(first_arrival_delay),
        paths_(std::move(paths)) {}

  TransmissionId transmission_id_;
  NodeId receiver_node_id_;
  double aggregate_transmission_loss_db_;
  SimDuration first_arrival_delay_;
  std::vector<PropagationPath> paths_;
};

inline auto ChannelFieldResponse::Create(
    TransmissionId transmission_id,
    NodeId receiver_node_id,
    double aggregate_transmission_loss_db,
    SimDuration first_arrival_delay,
    std::vector<PropagationPath> paths) -> Result<ChannelFieldResponse> {
  if(!std::isfinite(aggregate_transmission_loss_db)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "ChannelFieldResponse aggregate transmission loss must be "
              "finite"});
  }
  if(first_arrival_delay.nanoseconds() < 0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "ChannelFieldResponse first arrival delay must be "
              "non-negative"});
  }

  std::sort(paths.begin(),
            paths.end(),
            [](const PropagationPath& lhs, const PropagationPath& rhs) {
              if(lhs.excess_delay() != rhs.excess_delay()) {
                return lhs.excess_delay() < rhs.excess_delay();
              }
              if(lhs.pressure_gain_linear() !=
                 rhs.pressure_gain_linear()) {
                return lhs.pressure_gain_linear() <
                       rhs.pressure_gain_linear();
              }
              return lhs.phase_radians() < rhs.phase_radians();
            });
  if(!paths.empty() && paths.front().excess_delay() != SimDuration::Zero()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "ChannelFieldResponse multipath must contain a "
              "zero-excess-delay path"});
  }

  return ChannelFieldResponse{transmission_id,
                              receiver_node_id,
                              aggregate_transmission_loss_db,
                              first_arrival_delay,
                              std::move(paths)};
}

// Providers must return the same transmission/receiver identity as the query.
// Callers validate at the provider boundary before admitting a response to a
// later receive pipeline.
[[nodiscard]] inline auto ValidateChannelFieldResponseIdentity(
    const ChannelQuery& query,
    const ChannelFieldResponse& response) -> Status {
  if(query.transmission_id() != response.transmission_id() ||
     query.receiver_node_id() != response.receiver_node_id()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "ChannelFieldResponse identity does not match ChannelQuery"});
  }
  return {};
}

class IChannelFieldProvider {
 public:
  virtual ~IChannelFieldProvider() = default;

  [[nodiscard]] virtual auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> = 0;
};

}  // namespace ns3_factory::contracts
