#include <cstdlib>
#include <limits>
#include <type_traits>
#include <variant>

#include <ns3_factory/contracts/trace.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

namespace {

using namespace ns3_factory::contracts;

constexpr auto Seconds(std::int64_t value) -> SimTime {
  return SimTime::FromNanoseconds(value * 1'000'000'000);
}

constexpr auto DurationSeconds(std::int64_t value) -> SimDuration {
  return SimDuration::FromNanoseconds(value * 1'000'000'000);
}

static_assert(!std::is_default_constructible_v<TraceEvent>);
static_assert(!std::is_same_v<TraceTransmissionTarget,
                              TransmissionTarget>);
static_assert(std::is_same_v<
              decltype(std::declval<ITraceSink&>().Emit(
                  std::declval<const TraceEvent&>())),
              Status>);
static_assert(noexcept(std::declval<ITraceSink&>().Emit(
    std::declval<const TraceEvent&>())));
static_assert(std::is_same_v<
              decltype(TraceSignalChannelOutcome::path_count),
              std::uint64_t>);

auto TestTypedPayloadsAndKinds() -> bool {
  auto cycle = TraceEvent::Create(
      Seconds(10),
      CycleCommitTrace{PlanningCycleId{0},
                       SnapshotVersion{0},
                       SnapshotVersion{1},
                       Seconds(10)});
  auto transmission = TraceEvent::Create(
      Seconds(1),
      TransmissionTrace{TransmissionId{2},
                        PacketId{3},
                        NodeId{0},
                        TraceUnicastTransmissionTarget{NodeId{0}},
                        Seconds(1),
                        Seconds(2)});
  auto signal = TraceEvent::Create(
      Seconds(1),
      ChannelOutcomeTrace{
          TransmissionId{2},
          NodeId{0},
          TraceSignalChannelOutcome{DurationSeconds(2), 71.5, 4}});
  auto no_arrival = TraceEvent::Create(
      Seconds(1),
      ChannelOutcomeTrace{TransmissionId{2},
                          NodeId{0},
                          TraceNoArrivalChannelOutcome{}});
  auto reception = TraceEvent::Create(
      Seconds(4),
      ReceptionTrace{ReceptionId{5},
                     TransmissionId{2},
                     PacketId{3},
                     NodeId{0},
                     TraceReceptionDisposition::kRelayEnqueue,
                     TraceRxQualitySummary{
                         10.0,
                         20.0,
                         1.0e-5,
                         TraceRxQualityEvidenceSource::kModeled}});
  auto arrival = TraceEvent::Create(
      Seconds(3),
      SignalArrivalTrace{TransmissionId{2}, NodeId{0}});
  auto position = TraceEvent::Create(
      Seconds(3), NodePositionTrace{NodeId{0}, 1.0, 2.0, -3.0});
  auto fusion = TraceEvent::Create(
      Seconds(5),
      FusionTrace{7, NodeId{0}, Seconds(1), Seconds(5), 6, 9.0, 8.0});

  return cycle && transmission && signal && no_arrival && reception &&
         arrival && position && fusion &&
         cycle->kind() == TraceKind::kCycleCommit &&
         transmission->kind() == TraceKind::kTransmission &&
         signal->kind() == TraceKind::kChannelOutcome &&
         no_arrival->kind() == TraceKind::kChannelOutcome &&
         reception->kind() == TraceKind::kReception &&
         arrival->kind() == TraceKind::kSignalArrival &&
         position->kind() == TraceKind::kNodePosition &&
         fusion->kind() == TraceKind::kFusion &&
         std::get<ReceptionTrace>(reception->payload()).quality &&
         std::get<ReceptionTrace>(reception->payload())
                 .quality->bit_error_rate == 1.0e-5 &&
         std::holds_alternative<TraceUnicastTransmissionTarget>(
             std::get<TransmissionTrace>(transmission->payload()).target) &&
         std::holds_alternative<TraceNoArrivalChannelOutcome>(
             std::get<ChannelOutcomeTrace>(no_arrival->payload()).outcome);
}

auto TestInvariantsAndNullSink() -> bool {
  const auto bad_cycle = TraceEvent::Create(
      Seconds(9),
      CycleCommitTrace{PlanningCycleId{0},
                       SnapshotVersion{0},
                       SnapshotVersion{1},
                       Seconds(10)});
  const auto bad_transmission = TraceEvent::Create(
      Seconds(1),
      TransmissionTrace{TransmissionId{2},
                        PacketId{3},
                        NodeId{4},
                        TraceBroadcastTransmissionTarget{},
                        Seconds(1),
                        Seconds(1)});
  const auto bad_reception = TraceEvent::Create(
      Seconds(2),
      ReceptionTrace{ReceptionId{5},
                     TransmissionId{2},
                     PacketId{3},
                     NodeId{4},
                     static_cast<TraceReceptionDisposition>(0)});
  const auto bad_quality = TraceEvent::Create(
      Seconds(2),
      ReceptionTrace{
          ReceptionId{5},
          TransmissionId{2},
          PacketId{3},
          NodeId{4},
          TraceReceptionDisposition::kLocalDelivery,
          TraceRxQualitySummary{
              0.0,
              0.0,
              std::numeric_limits<double>::quiet_NaN(),
              TraceRxQualityEvidenceSource::kModeled}});
  const auto bad_quality_source = TraceEvent::Create(
      Seconds(2),
      ReceptionTrace{
          ReceptionId{5},
          TransmissionId{2},
          PacketId{3},
          NodeId{4},
          TraceReceptionDisposition::kLocalDelivery,
          TraceRxQualitySummary{
              0.0,
              0.0,
              0.0,
              static_cast<TraceRxQualityEvidenceSource>(0)}});
  const auto bad_position = TraceEvent::Create(
      Seconds(2),
      NodePositionTrace{NodeId{4},
                        std::numeric_limits<double>::infinity(),
                        0.0,
                        0.0});
  const auto bad_fusion_time = TraceEvent::Create(
      Seconds(2),
      FusionTrace{1, NodeId{4}, Seconds(1), Seconds(3), 1, 0.0, 0.0});
  const auto empty_fusion = TraceEvent::Create(
      Seconds(3),
      FusionTrace{1, NodeId{4}, Seconds(1), Seconds(3), 0, 0.0, 0.0});
  auto valid = TraceEvent::Create(
      Seconds(1),
      ChannelOutcomeTrace{TransmissionId{2},
                          NodeId{0},
                          TraceNoArrivalChannelOutcome{}});
  NullTraceSink sink;
  return !bad_cycle && !bad_transmission && !bad_reception && !bad_quality &&
         !bad_quality_source && !bad_position && !bad_fusion_time &&
         !empty_fusion && valid &&
         sink.Emit(*valid);
}

}  // namespace

auto main() -> int {
  return TestTypedPayloadsAndKinds() && TestInvariantsAndNullSink()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
