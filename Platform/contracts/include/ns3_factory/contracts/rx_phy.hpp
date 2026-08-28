#pragma once

#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>
#include <ns3_factory/contracts/rx_quality.hpp>

namespace ns3_factory::contracts {

[[nodiscard]] inline auto CreateNoiseQueryForDesiredSignal(
    const ReceiverWindow& receiver_window,
    Position3d receiver_position) -> Result<NoiseQuery> {
  const auto& desired = receiver_window.desired_signal();
  return NoiseQuery::Create(receiver_window.receiver_node_id(),
                            receiver_position,
                            desired.first_arrival_at(),
                            desired.last_effect_end_at(),
                            desired.lower_frequency_hz(),
                            desired.upper_frequency_hz());
}

class RxDecodeRequest final {
 public:
  [[nodiscard]] static auto Create(
      ReceiverWindow receiver_window,
      NoiseObservation noise_observation) -> Result<RxDecodeRequest>;

  [[nodiscard]] constexpr auto receiver_window() const noexcept
      -> const ReceiverWindow& {
    return receiver_window_;
  }

  [[nodiscard]] constexpr auto noise_observation() const noexcept
      -> const NoiseObservation& {
    return noise_observation_;
  }

  auto operator==(const RxDecodeRequest&) const -> bool = default;

 private:
  RxDecodeRequest(ReceiverWindow receiver_window,
                  NoiseObservation noise_observation)
      : receiver_window_(std::move(receiver_window)),
        noise_observation_(std::move(noise_observation)) {}

  ReceiverWindow receiver_window_;
  NoiseObservation noise_observation_;
};

inline auto RxDecodeRequest::Create(
    ReceiverWindow receiver_window,
    NoiseObservation noise_observation) -> Result<RxDecodeRequest> {
  const auto& desired = receiver_window.desired_signal();
  if(desired.last_effect_end_at() <= desired.first_arrival_at()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "RxDecodeRequest desired signal interval has zero width"});
  }
  if(noise_observation.receiver_node_id() !=
         receiver_window.receiver_node_id() ||
     noise_observation.observed_from() != desired.first_arrival_at() ||
     noise_observation.observed_until() != desired.last_effect_end_at() ||
     noise_observation.lower_frequency_hz() !=
         desired.lower_frequency_hz() ||
     noise_observation.upper_frequency_hz() !=
         desired.upper_frequency_hz()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "RxDecodeRequest noise provenance does not match desired "
              "signal"});
  }

  return RxDecodeRequest{std::move(receiver_window),
                         std::move(noise_observation)};
}

// P0 packet-level scalar processing uses the aggregate channel summary.
// Absolute multipath pressure gains and phases are not additionally applied.
[[nodiscard]] inline auto ComputeP0ScalarReceivedLevelDbRe1upa(
    const ReceivedSignal& signal) -> Result<double> {
  const auto received_level =
      signal.emission().source_level_db_re_1upa_at_1m() -
      signal.channel_response().aggregate_transmission_loss_db();
  if(!std::isfinite(received_level)) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "P0 scalar received signal level is not finite"});
  }
  return received_level;
}

enum class DecodeOutcome : std::uint8_t {
  kDecoded = 1,
  kNotDecoded = 2,
};

class RxDecodeResult final {
 public:
  [[nodiscard]] static auto Create(TransmissionId transmission_id,
                                   PacketId packet_id,
                                   NodeId receiver_node_id,
                                   DecodeOutcome outcome)
      -> Result<RxDecodeResult>;

  [[nodiscard]] static auto Create(TransmissionId transmission_id,
                                   PacketId packet_id,
                                   NodeId receiver_node_id,
                                   DecodeOutcome outcome,
                                   RxQualityEvidence quality_evidence)
      -> Result<RxDecodeResult>;

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> TransmissionId {
    return transmission_id_;
  }

  [[nodiscard]] constexpr auto packet_id() const noexcept -> PacketId {
    return packet_id_;
  }

  [[nodiscard]] constexpr auto receiver_node_id() const noexcept -> NodeId {
    return receiver_node_id_;
  }

  [[nodiscard]] constexpr auto outcome() const noexcept -> DecodeOutcome {
    return outcome_;
  }

  [[nodiscard]] constexpr auto quality_evidence() const noexcept
      -> const std::optional<RxQualityEvidence>& {
    return quality_evidence_;
  }

  auto operator==(const RxDecodeResult&) const -> bool = default;

 private:
  constexpr RxDecodeResult(TransmissionId transmission_id,
                           PacketId packet_id,
                           NodeId receiver_node_id,
                           DecodeOutcome outcome,
                           std::optional<RxQualityEvidence> quality_evidence)
      noexcept
      : transmission_id_(transmission_id),
        packet_id_(packet_id),
        receiver_node_id_(receiver_node_id),
        outcome_(outcome),
        quality_evidence_(std::move(quality_evidence)) {}

  TransmissionId transmission_id_;
  PacketId packet_id_;
  NodeId receiver_node_id_;
  DecodeOutcome outcome_;
  std::optional<RxQualityEvidence> quality_evidence_;
};

inline auto RxDecodeResult::Create(TransmissionId transmission_id,
                                   PacketId packet_id,
                                   NodeId receiver_node_id,
                                   DecodeOutcome outcome)
    -> Result<RxDecodeResult> {
  if(outcome != DecodeOutcome::kDecoded &&
     outcome != DecodeOutcome::kNotDecoded) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "RxDecodeResult contains an invalid DecodeOutcome"});
  }
  return RxDecodeResult{transmission_id,
                        packet_id,
                        receiver_node_id,
                        outcome,
                        std::nullopt};
}

inline auto RxDecodeResult::Create(
    TransmissionId transmission_id,
    PacketId packet_id,
    NodeId receiver_node_id,
    DecodeOutcome outcome,
    RxQualityEvidence quality_evidence) -> Result<RxDecodeResult> {
  if(outcome != DecodeOutcome::kDecoded &&
     outcome != DecodeOutcome::kNotDecoded) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "RxDecodeResult contains an invalid DecodeOutcome"});
  }
  return RxDecodeResult{transmission_id,
                        packet_id,
                        receiver_node_id,
                        outcome,
                        std::move(quality_evidence)};
}

[[nodiscard]] inline auto ValidateRxDecodeResultIdentity(
    const RxDecodeRequest& request,
    const RxDecodeResult& result) -> Status {
  const auto& desired = request.receiver_window().desired_signal();
  if(result.transmission_id() != desired.transmission_id() ||
     result.packet_id() != desired.emission().packet_id() ||
     result.receiver_node_id() !=
         request.receiver_window().receiver_node_id()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "RxDecodeResult identity does not match desired signal"});
  }
  return {};
}

class IRxPhy {
 public:
  virtual ~IRxPhy() = default;

  // Implementations must return the same result for equal requests. Random
  // PER sampling requires a future explicit deterministic RNG mechanism.
  [[nodiscard]] virtual auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> = 0;
};

}  // namespace ns3_factory::contracts
