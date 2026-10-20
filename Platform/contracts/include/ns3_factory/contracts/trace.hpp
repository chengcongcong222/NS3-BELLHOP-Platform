#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>
#include <utility>
#include <variant>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

enum class TraceKind : std::uint8_t {
  kCycleCommit = 1,
  kTransmission = 2,
  kChannelOutcome = 3,
  kReception = 4,
  kSignalArrival = 5,
  kNodePosition = 6,
  kFusion = 7,
};

struct TraceUnicastTransmissionTarget final {
  NodeId node_id;

  constexpr auto operator==(
      const TraceUnicastTransmissionTarget&) const noexcept -> bool = default;
};

struct TraceBroadcastTransmissionTarget final {
  constexpr auto operator==(
      const TraceBroadcastTransmissionTarget&) const noexcept
      -> bool = default;
};

using TraceTransmissionTarget =
    std::variant<TraceUnicastTransmissionTarget,
                 TraceBroadcastTransmissionTarget>;

struct TraceSignalChannelOutcome final {
  SimDuration first_arrival_delay;
  double aggregate_transmission_loss_db;
  std::uint64_t path_count;

  auto operator==(const TraceSignalChannelOutcome&) const -> bool = default;
};

struct TraceNoArrivalChannelOutcome final {
  constexpr auto operator==(
      const TraceNoArrivalChannelOutcome&) const noexcept -> bool = default;
};

// Mode C: a modeled flight-time reference plus a complete measured-operator
// window. Coefficients are normalized, so absolute TL/path count are absent.
struct TraceRecordedChannelOutcome final {
  SimDuration reference_delay;
  SimDuration receive_duration;
  auto operator==(const TraceRecordedChannelOutcome&) const -> bool = default;
};

using TraceChannelOutcome =
    std::variant<TraceSignalChannelOutcome,
                 TraceNoArrivalChannelOutcome, TraceRecordedChannelOutcome>;

[[nodiscard]] inline bool HasChannelSignal(const TraceChannelOutcome& outcome) {
  return !std::holds_alternative<TraceNoArrivalChannelOutcome>(outcome);
}

enum class TraceReceptionDisposition : std::uint8_t {
  kNotDecoded = 1,
  kOverheard = 2,
  kLocalDelivery = 3,
  kRelayEnqueue = 4,
};

enum class TraceRxQualityEvidenceSource : std::uint8_t {
  kModeled = 1,
  kMeasured = 2,
  kExternal = 3,
};

struct TraceRxQualitySummary final {
  double signal_to_noise_ratio_db;
  double eb_n0_db;
  double bit_error_rate;
  TraceRxQualityEvidenceSource source;

  constexpr auto operator==(const TraceRxQualitySummary&) const noexcept
      -> bool = default;
};

struct CycleCommitTrace final {
  PlanningCycleId cycle_id;
  SnapshotVersion base_snapshot_version;
  SnapshotVersion committed_snapshot_version;
  SimTime committed_at;

  constexpr auto operator==(const CycleCommitTrace&) const noexcept
      -> bool = default;
};

struct TransmissionTrace final {
  TransmissionId transmission_id;
  PacketId packet_id;
  NodeId sender_node_id;
  TraceTransmissionTarget target;
  SimTime started_at;
  SimTime ended_at;

  auto operator==(const TransmissionTrace&) const -> bool = default;
};

struct ChannelOutcomeTrace final {
  TransmissionId transmission_id;
  NodeId receiver_node_id;
  TraceChannelOutcome outcome;

  auto operator==(const ChannelOutcomeTrace&) const -> bool = default;
};

// Observation only; never consulted for decoding or delivery decisions.
struct TraceWaveformSummary final {
  std::uint64_t payload_bit_count;
  std::uint64_t bit_error_count;
  bool crc_ok;
  double signal_power_upa2;
  double noise_power_upa2;
  // Legacy members retain source compatibility; normalized values are encoded
  // under relative-power JSON keys, never *_upa2 keys.
  bool normalized_power = false;
  constexpr auto operator==(const TraceWaveformSummary&) const noexcept -> bool = default;
};

struct TraceRecordedMixture final {
  std::uint64_t signal_count;
  std::uint64_t sample_count;
  double mixture_power_relative;
  double noise_power_relative;
  constexpr auto operator==(const TraceRecordedMixture&) const noexcept -> bool = default;
};

struct ReceptionTrace final {
  ReceptionId reception_id;
  TransmissionId transmission_id;
  PacketId packet_id;
  NodeId receiver_node_id;
  TraceReceptionDisposition disposition;
  std::optional<TraceRxQualitySummary> quality;
  std::optional<TraceWaveformSummary> waveform{};
  std::optional<TraceRecordedMixture> mixture{};

  constexpr auto operator==(const ReceptionTrace&) const noexcept
      -> bool = default;
};

struct SignalArrivalTrace final {
  TransmissionId transmission_id;
  NodeId receiver_node_id;

  constexpr auto operator==(const SignalArrivalTrace&) const noexcept
      -> bool = default;
};

// Trace-owned position summary. It intentionally does not expose MotionState,
// trajectory owners, or any runtime-internal object.
struct NodePositionTrace final {
  NodeId node_id;
  double x_meters;
  double y_meters;
  double z_meters;

  constexpr auto operator==(const NodePositionTrace&) const noexcept
      -> bool = default;
};

struct FusionTrace final {
  std::uint64_t fusion_sequence;
  NodeId fusion_center_node_id;
  SimTime started_at;
  SimTime completed_at;
  std::uint64_t observation_count;
  double estimated_target_x_meters;
  double estimated_target_y_meters;

  constexpr auto operator==(const FusionTrace&) const noexcept
      -> bool = default;
};

using TracePayload =
    std::variant<CycleCommitTrace,
                 TransmissionTrace,
                 ChannelOutcomeTrace,
                 ReceptionTrace,
                 SignalArrivalTrace,
                 NodePositionTrace,
                 FusionTrace>;

// A compact, read-only observation value. TraceKind is derived from the
// payload by Create so the discriminator and payload cannot disagree.
class TraceEvent final {
 public:
  [[nodiscard]] static auto Create(SimTime occurred_at,
                                   TracePayload payload)
      -> Result<TraceEvent>;

  [[nodiscard]] constexpr auto occurred_at() const noexcept -> SimTime {
    return occurred_at_;
  }

  [[nodiscard]] constexpr auto kind() const noexcept -> TraceKind {
    return kind_;
  }

  [[nodiscard]] constexpr auto payload() const noexcept
      -> const TracePayload& {
    return payload_;
  }

  auto operator==(const TraceEvent&) const -> bool = default;

 private:
  TraceEvent(SimTime occurred_at, TraceKind kind, TracePayload payload)
      : occurred_at_(occurred_at),
        kind_(kind),
        payload_(std::move(payload)) {}

  SimTime occurred_at_;
  TraceKind kind_;
  TracePayload payload_;
};

inline auto TraceEvent::Create(SimTime occurred_at,
                               TracePayload payload)
    -> Result<TraceEvent> {
  auto kind = TraceKind::kCycleCommit;
  if(const auto* cycle = std::get_if<CycleCommitTrace>(&payload)) {
    constexpr auto kMaxVersion =
        std::numeric_limits<SnapshotVersion::value_type>::max();
    if(occurred_at != cycle->committed_at ||
       cycle->base_snapshot_version.value() == kMaxVersion ||
       cycle->committed_snapshot_version.value() !=
           cycle->base_snapshot_version.value() + 1) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "CycleCommitTrace does not describe one successful commit"});
    }
  } else if(const auto* transmission =
                std::get_if<TransmissionTrace>(&payload)) {
    kind = TraceKind::kTransmission;
    if(occurred_at != transmission->started_at ||
       transmission->ended_at <= transmission->started_at) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "TransmissionTrace timing is inconsistent"});
    }
  } else if(const auto* channel =
                std::get_if<ChannelOutcomeTrace>(&payload)) {
    kind = TraceKind::kChannelOutcome;
    if(const auto* recorded=std::get_if<TraceRecordedChannelOutcome>(&channel->outcome);
       recorded && (recorded->reference_delay.nanoseconds()<0 ||
                    recorded->receive_duration.nanoseconds()<=0 ||
                    !CheckedAdd(recorded->reference_delay,recorded->receive_duration)))
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid recorded channel trace envelope"});
    if(const auto* signal =
           std::get_if<TraceSignalChannelOutcome>(&channel->outcome);
       signal != nullptr &&
       (signal->first_arrival_delay.nanoseconds() < 0 ||
        !std::isfinite(signal->aggregate_transmission_loss_db))) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "ChannelOutcomeTrace signal summary is invalid"});
    }
  } else if(const auto* reception =
                std::get_if<ReceptionTrace>(&payload)) {
    kind = TraceKind::kReception;
    const auto disposition = reception->disposition;
    if(disposition != TraceReceptionDisposition::kNotDecoded &&
       disposition != TraceReceptionDisposition::kOverheard &&
       disposition != TraceReceptionDisposition::kLocalDelivery &&
       disposition != TraceReceptionDisposition::kRelayEnqueue) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "ReceptionTrace disposition is invalid"});
    }
    if(reception->waveform) {
      const auto& w = *reception->waveform;
      if(w.payload_bit_count == 0 || w.bit_error_count > w.payload_bit_count ||
         !std::isfinite(w.signal_power_upa2) || w.signal_power_upa2 < 0 ||
         !std::isfinite(w.noise_power_upa2) || w.noise_power_upa2 < 0) {
        return std::unexpected(Error{ErrorCode::kInvalidArgument,
                                    "ReceptionTrace waveform summary is invalid"});
      }
    }
    if(reception->mixture) {
      const auto& m=*reception->mixture;
      if(!m.signal_count || !m.sample_count || !std::isfinite(m.mixture_power_relative) ||
         !std::isfinite(m.noise_power_relative) || m.mixture_power_relative<0 || m.noise_power_relative<0)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid recorded mixture trace"});
    }
    if(reception->quality) {
      const auto& quality = *reception->quality;
      if(!std::isfinite(quality.signal_to_noise_ratio_db) ||
         !std::isfinite(quality.eb_n0_db) ||
         !std::isfinite(quality.bit_error_rate) ||
         quality.bit_error_rate < 0.0 || quality.bit_error_rate > 1.0) {
        return std::unexpected(
            Error{ErrorCode::kInvalidArgument,
                  "ReceptionTrace quality summary is invalid"});
      }
      switch(quality.source) {
        case TraceRxQualityEvidenceSource::kModeled:
        case TraceRxQualityEvidenceSource::kMeasured:
        case TraceRxQualityEvidenceSource::kExternal:
          break;
        default:
          return std::unexpected(
              Error{ErrorCode::kInvalidArgument,
                    "ReceptionTrace quality source is invalid"});
      }
    }
  } else if(std::holds_alternative<SignalArrivalTrace>(payload)) {
    kind = TraceKind::kSignalArrival;
  } else if(const auto* position =
                std::get_if<NodePositionTrace>(&payload)) {
    kind = TraceKind::kNodePosition;
    if(!std::isfinite(position->x_meters) ||
       !std::isfinite(position->y_meters) ||
       !std::isfinite(position->z_meters)) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "NodePositionTrace coordinates must be finite"});
    }
  } else {
    kind = TraceKind::kFusion;
    const auto& fusion = std::get<FusionTrace>(payload);
    if(occurred_at != fusion.completed_at ||
       fusion.started_at > fusion.completed_at ||
       fusion.observation_count == 0 ||
       !std::isfinite(fusion.estimated_target_x_meters) ||
       !std::isfinite(fusion.estimated_target_y_meters)) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "FusionTrace summary is invalid"});
    }
  }
  return TraceEvent{occurred_at, kind, std::move(payload)};
}

class ITraceSink {
 public:
  virtual ~ITraceSink() = default;

  // Trace delivery is non-causal. Callers must unconditionally ignore the
  // returned Status and must never alter simulation state because of it.
  [[nodiscard]] virtual auto Emit(const TraceEvent& event) noexcept
      -> Status = 0;
};

class NullTraceSink final : public ITraceSink {
 public:
  [[nodiscard]] auto Emit(const TraceEvent&) noexcept -> Status override {
    return {};
  }
};

}  // namespace ns3_factory::contracts
