#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
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

using TraceChannelOutcome =
    std::variant<TraceSignalChannelOutcome,
                 TraceNoArrivalChannelOutcome>;

enum class TraceReceptionDisposition : std::uint8_t {
  kNotDecoded = 1,
  kOverheard = 2,
  kLocalDelivery = 3,
  kRelayEnqueue = 4,
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

struct ReceptionTrace final {
  ReceptionId reception_id;
  TransmissionId transmission_id;
  PacketId packet_id;
  NodeId receiver_node_id;
  TraceReceptionDisposition disposition;

  constexpr auto operator==(const ReceptionTrace&) const noexcept
      -> bool = default;
};

using TracePayload =
    std::variant<CycleCommitTrace,
                 TransmissionTrace,
                 ChannelOutcomeTrace,
                 ReceptionTrace>;

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
    if(const auto* signal =
           std::get_if<TraceSignalChannelOutcome>(&channel->outcome);
       signal != nullptr &&
       (signal->first_arrival_delay.nanoseconds() < 0 ||
        !std::isfinite(signal->aggregate_transmission_loss_db))) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "ChannelOutcomeTrace signal summary is invalid"});
    }
  } else {
    kind = TraceKind::kReception;
    const auto disposition =
        std::get<ReceptionTrace>(payload).disposition;
    if(disposition != TraceReceptionDisposition::kNotDecoded &&
       disposition != TraceReceptionDisposition::kOverheard &&
       disposition != TraceReceptionDisposition::kLocalDelivery &&
       disposition != TraceReceptionDisposition::kRelayEnqueue) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "ReceptionTrace disposition is invalid"});
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
