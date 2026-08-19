#pragma once

#include <cmath>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

namespace ns3_factory::contracts {

struct TxEncodeRequest final {
  TransmissionId transmission_id;
  NodeId sender_node_id;
  TransmissionTarget target;
  SimTime started_at;

  auto operator==(const TxEncodeRequest&) const -> bool = default;
};

class TxEmission final {
 public:
  [[nodiscard]] static auto Create(
      TransmissionId transmission_id,
      PacketId packet_id,
      NodeId sender_node_id,
      SimTime started_at,
      SimDuration duration,
      double center_frequency_hz,
      double bandwidth_hz,
      double source_level_db_re_1upa_at_1m) -> Result<TxEmission>;

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> TransmissionId {
    return transmission_id_;
  }

  [[nodiscard]] constexpr auto packet_id() const noexcept -> PacketId {
    return packet_id_;
  }

  [[nodiscard]] constexpr auto sender_node_id() const noexcept -> NodeId {
    return sender_node_id_;
  }

  [[nodiscard]] constexpr auto started_at() const noexcept -> SimTime {
    return started_at_;
  }

  [[nodiscard]] constexpr auto duration() const noexcept -> SimDuration {
    return duration_;
  }

  [[nodiscard]] constexpr auto center_frequency_hz() const noexcept
      -> double {
    return center_frequency_hz_;
  }

  [[nodiscard]] constexpr auto bandwidth_hz() const noexcept -> double {
    return bandwidth_hz_;
  }

  [[nodiscard]] constexpr auto source_level_db_re_1upa_at_1m() const noexcept
      -> double {
    return source_level_db_re_1upa_at_1m_;
  }

  auto operator==(const TxEmission&) const -> bool = default;

 private:
  constexpr TxEmission(TransmissionId transmission_id,
                       PacketId packet_id,
                       NodeId sender_node_id,
                       SimTime started_at,
                       SimDuration duration,
                       double center_frequency_hz,
                       double bandwidth_hz,
                       double source_level_db_re_1upa_at_1m) noexcept
      : transmission_id_(transmission_id),
        packet_id_(packet_id),
        sender_node_id_(sender_node_id),
        started_at_(started_at),
        duration_(duration),
        center_frequency_hz_(center_frequency_hz),
        bandwidth_hz_(bandwidth_hz),
        source_level_db_re_1upa_at_1m_(source_level_db_re_1upa_at_1m) {}

  TransmissionId transmission_id_;
  PacketId packet_id_;
  NodeId sender_node_id_;
  SimTime started_at_;
  SimDuration duration_;
  double center_frequency_hz_;
  double bandwidth_hz_;
  double source_level_db_re_1upa_at_1m_;
};

inline auto TxEmission::Create(TransmissionId transmission_id,
                               PacketId packet_id,
                               NodeId sender_node_id,
                               SimTime started_at,
                               SimDuration duration,
                               double center_frequency_hz,
                               double bandwidth_hz,
                               double source_level_db_re_1upa_at_1m)
    -> Result<TxEmission> {
  if(duration.nanoseconds() < 0) {
    return std::unexpected(Error{ErrorCode::kOutOfRange,
                                 "TxEmission duration must be non-negative"});
  }
  if(!std::isfinite(center_frequency_hz) ||
     !std::isfinite(bandwidth_hz) ||
     !std::isfinite(source_level_db_re_1upa_at_1m)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "TxEmission physical values must be finite"});
  }
  if(center_frequency_hz <= 0.0 || bandwidth_hz <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "TxEmission frequency and bandwidth must be positive"});
  }

  return TxEmission{transmission_id,
                    packet_id,
                    sender_node_id,
                    started_at,
                    duration,
                    center_frequency_hz,
                    bandwidth_hz,
                    source_level_db_re_1upa_at_1m};
}

// Tx PHY implementations are provider boundaries. Callers validate that the
// returned emission still belongs to the packet and encode request before the
// value is admitted to runtime state.
[[nodiscard]] inline auto ValidateTxEmissionIdentity(
    const DigitalPacket& packet,
    const TxEncodeRequest& request,
    const TxEmission& emission) -> Status {
  if(emission.transmission_id() != request.transmission_id ||
     emission.packet_id() != packet.packet_id ||
     emission.sender_node_id() != request.sender_node_id ||
     emission.started_at() != request.started_at) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "TxEmission identity does not match packet/encode request"});
  }
  return {};
}

class ITxPhy {
 public:
  virtual ~ITxPhy() = default;

  [[nodiscard]] virtual auto Encode(
      const DigitalPacket& packet,
      const TxEncodeRequest& request) const -> Result<TxEmission> = 0;
};

}  // namespace ns3_factory::contracts
