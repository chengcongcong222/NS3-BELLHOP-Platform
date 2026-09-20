#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>
#include <vector>

#include "internal/target_packet_metrics.hpp"

namespace {
using namespace ns3_factory::contracts;
using namespace ns3_factory::application::internal;

auto Check(bool value, std::string_view label) -> bool {
  if(!value) std::cerr << "FAILED: " << label << '\n';
  return value;
}

auto Append(std::vector<TraceEvent>& events, TracePayload payload) -> bool {
  auto event = TraceEvent::Create(SimTime::Zero(), std::move(payload));
  if(!event) return false;
  events.push_back(std::move(*event));
  return true;
}

auto Fixtures() -> std::optional<std::vector<TraceEvent>> {
  std::vector<TraceEvent> events;
  for(unsigned id = 1U; id <= 3U; ++id) {
    if(!Append(events, TransmissionTrace{
            TransmissionId{id}, PacketId{100U + id}, NodeId{1},
            TraceUnicastTransmissionTarget{NodeId{2}}, SimTime::Zero(),
            SimTime::FromNanoseconds(1'000'000)})) {
      return std::nullopt;
    }
  }
  if(!Append(events, TransmissionTrace{
          TransmissionId{4}, PacketId{104}, NodeId{1},
          TraceBroadcastTransmissionTarget{}, SimTime::Zero(),
          SimTime::FromNanoseconds(1'000'000)}) ||
     !Append(events, ChannelOutcomeTrace{
          TransmissionId{1}, NodeId{2},
          TraceSignalChannelOutcome{SimDuration::Zero(), 70.0, 1}}) ||
     !Append(events, ChannelOutcomeTrace{
          TransmissionId{2}, NodeId{2},
          TraceSignalChannelOutcome{SimDuration::Zero(), 70.0, 1}}) ||
     !Append(events, ChannelOutcomeTrace{
          TransmissionId{3}, NodeId{2},
          TraceNoArrivalChannelOutcome{}}) ||
     !Append(events, ChannelOutcomeTrace{
          TransmissionId{1}, NodeId{3},
          TraceSignalChannelOutcome{SimDuration::Zero(), 70.0, 1}}) ||
     !Append(events, ReceptionTrace{
          ReceptionId{10}, TransmissionId{1}, PacketId{101}, NodeId{2},
          TraceReceptionDisposition::kLocalDelivery, std::nullopt}) ||
     !Append(events, ReceptionTrace{
          ReceptionId{11}, TransmissionId{2}, PacketId{102}, NodeId{2},
          TraceReceptionDisposition::kNotDecoded, std::nullopt}) ||
     !Append(events, ReceptionTrace{
          ReceptionId{12}, TransmissionId{1}, PacketId{101}, NodeId{3},
          TraceReceptionDisposition::kOverheard, std::nullopt})) {
    return std::nullopt;
  }
  return events;
}

auto TestTargetOnlyMetrics() -> bool {
  auto events = Fixtures();
  if(!events) return Check(false, "Trace fixtures");
  const std::vector<TargetPacketObservation> rows{
      {TransmissionId{1}, NodeId{2}, 120U, 0U, true},
      {TransmissionId{2}, NodeId{2}, 120U, 3U, false},
      {TransmissionId{1}, NodeId{3}, 120U, 90U, true}};
  const auto metrics = BuildTargetPacketMetrics(*events, rows, 120U);
  if(!metrics) return Check(false, "Target-only metrics");
  auto same = *metrics;
  same.attempts.back().packet_id = PacketId{999};
  return Check(metrics->target_attempt_count == 3U &&
                   metrics->evaluated_target_receptions == 2U &&
                   metrics->no_arrival_count == 1U &&
                   metrics->packet_error_count == 2U &&
                   metrics->evaluated_payload_bit_count == 240U &&
                   metrics->payload_bit_error_count == 3U &&
                   metrics->bit_error_rate &&
                   std::abs(*metrics->bit_error_rate - 3.0 / 240.0) < 1e-15 &&
                   metrics->packet_error_rate &&
                   std::abs(*metrics->packet_error_rate - 2.0 / 3.0) < 1e-15,
               "Target BER excludes no-arrival bits and overheard receiver") &&
         Check(ValidateSameTargetAttempts(*metrics, *metrics) &&
                   !ValidateSameTargetAttempts(*metrics, same),
               "Comparison requires identical target attempts");
}

auto TestMissingAndContradictoryEvidence() -> bool {
  auto events = Fixtures();
  if(!events) return Check(false, "Trace fixtures");
  const std::vector<TargetPacketObservation> missing{
      {TransmissionId{1}, NodeId{2}, 120U, 0U, true}};
  const std::vector<TargetPacketObservation> duplicate{
      {TransmissionId{1}, NodeId{2}, 120U, 0U, true},
      {TransmissionId{1}, NodeId{2}, 120U, 0U, true}};
  const std::vector<TargetPacketObservation> wrong_crc{
      {TransmissionId{1}, NodeId{2}, 120U, 0U, false},
      {TransmissionId{2}, NodeId{2}, 120U, 3U, false}};
  const std::vector<TargetPacketObservation> invented_no_arrival{
      {TransmissionId{1}, NodeId{2}, 120U, 0U, true},
      {TransmissionId{2}, NodeId{2}, 120U, 3U, false},
      {TransmissionId{3}, NodeId{2}, 120U, 120U, false}};
  return Check(!BuildTargetPacketMetrics(*events, missing, 120U),
               "Missing arrived-target bits are not treated as zero") &&
         Check(!BuildTargetPacketMetrics(*events, duplicate, 120U),
               "Duplicate observation rejected") &&
         Check(!BuildTargetPacketMetrics(*events, wrong_crc, 120U),
               "CRC and reception disposition must agree") &&
         Check(!BuildTargetPacketMetrics(
                   *events, invented_no_arrival, 120U),
               "No-arrival has no invented BER sample");
}

}  // namespace

int main() {
  return TestTargetOnlyMetrics() && TestMissingAndContradictoryEvidence()
             ? EXIT_SUCCESS : EXIT_FAILURE;
}
