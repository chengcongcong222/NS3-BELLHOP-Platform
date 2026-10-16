#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/trace.hpp>

namespace ns3_factory::application::internal {

// The same target-only record is populated by sampled scalar and waveform
// receivers. CRC failures are recorded even when payload bits happen to match.
struct TargetPacketObservation final {
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;
  std::size_t payload_bit_count;
  std::size_t bit_error_count;
  bool crc_ok;
};

struct TargetPacketAttempt final {
  contracts::TransmissionId transmission_id;
  contracts::PacketId packet_id;
  contracts::NodeId target_node_id;
  std::size_t payload_bit_count;

  auto operator==(const TargetPacketAttempt&) const -> bool = default;
};

struct TargetPacketMetrics final {
  std::vector<TargetPacketAttempt> attempts;
  std::size_t target_attempt_count;
  std::size_t no_arrival_count;
  std::size_t evaluated_target_receptions;
  std::size_t packet_error_count;
  std::size_t evaluated_payload_bit_count;
  std::size_t payload_bit_error_count;
  std::optional<double> bit_error_rate;
  std::optional<double> packet_error_rate;
};

[[nodiscard]] inline auto BuildTargetPacketMetrics(
    std::span<const contracts::TraceEvent> events,
    std::span<const TargetPacketObservation> observations,
    std::size_t expected_payload_bit_count)
    -> contracts::Result<TargetPacketMetrics> {
  using Key = std::pair<std::uint64_t, std::uint64_t>;
  using contracts::Error;
  using contracts::ErrorCode;
  if(expected_payload_bit_count == 0U) {
    return std::unexpected(Error{ErrorCode::kInvalidArgument,
                                 "Comparison payload bit count must be positive"});
  }
  std::map<std::uint64_t, TargetPacketAttempt> target_attempts;
  std::map<Key, bool> channel_signals;
  std::map<Key, std::pair<contracts::PacketId,
                          contracts::TraceReceptionDisposition>> receptions;
  std::map<Key, TargetPacketObservation> observed;
  for(const auto& event : events) {
    if(const auto* tx =
           std::get_if<contracts::TransmissionTrace>(&event.payload())) {
      if(const auto* target =
             std::get_if<contracts::TraceUnicastTransmissionTarget>(
                 &tx->target)) {
        const auto inserted = target_attempts.emplace(
            tx->transmission_id.value(),
            TargetPacketAttempt{tx->transmission_id, tx->packet_id,
                                target->node_id, expected_payload_bit_count});
        if(!inserted.second) {
          return std::unexpected(Error{ErrorCode::kAlreadyExists,
                                       "Duplicate target Transmission trace"});
        }
      }
    } else if(const auto* channel =
                  std::get_if<contracts::ChannelOutcomeTrace>(
                      &event.payload())) {
      const Key key{channel->transmission_id.value(),
                    channel->receiver_node_id.value()};
      const auto inserted = channel_signals.emplace(
          key, contracts::HasChannelSignal(channel->outcome));
      if(!inserted.second) {
        return std::unexpected(Error{ErrorCode::kAlreadyExists,
                                     "Duplicate ChannelOutcome trace"});
      }
    } else if(const auto* rx =
                  std::get_if<contracts::ReceptionTrace>(
                      &event.payload())) {
      const Key key{rx->transmission_id.value(),
                    rx->receiver_node_id.value()};
      if(!receptions.emplace(key, std::pair{rx->packet_id, rx->disposition}).second) {
        return std::unexpected(Error{ErrorCode::kAlreadyExists,
                                     "Duplicate Reception trace"});
      }
    }
  }
  for(const auto& row : observations) {
    if(row.payload_bit_count == 0U ||
       row.bit_error_count > row.payload_bit_count) {
      return std::unexpected(Error{ErrorCode::kInvalidArgument,
                                   "Invalid observed payload bit count"});
    }
    const Key key{row.transmission_id.value(),
                  row.receiver_node_id.value()};
    if(!observed.emplace(key, row).second) {
      return std::unexpected(Error{ErrorCode::kAlreadyExists,
                                   "Duplicate target receiver observation"});
    }
  }

  TargetPacketMetrics metrics{{}, target_attempts.size(), 0U, 0U, 0U,
                              0U, 0U, std::nullopt, std::nullopt};
  metrics.attempts.reserve(target_attempts.size());
  for(const auto& [id, attempt] : target_attempts) {
    const Key key{id, attempt.target_node_id.value()};
    metrics.attempts.push_back(attempt);
    const auto channel = channel_signals.find(key);
    if(channel == channel_signals.end()) {
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,
                                   "Target channel outcome is missing"});
    }
    const auto rx = receptions.find(key);
    const auto row = observed.find(key);
    if(!channel->second) {
      if(rx != receptions.end() || row != observed.end()) {
        return std::unexpected(Error{
            ErrorCode::kFailedPrecondition,
            "No-arrival target cannot have a reception or bit observation"});
      }
      ++metrics.no_arrival_count;
      ++metrics.packet_error_count;
      continue;
    }
    if(rx == receptions.end() || row == observed.end()) {
      return std::unexpected(Error{
          ErrorCode::kFailedPrecondition,
          "Arrived target lacks reception or payload-bit observation"});
    }
    if(row->second.payload_bit_count != expected_payload_bit_count ||
       rx->second.first != attempt.packet_id ||
       rx->second.second == contracts::TraceReceptionDisposition::kOverheard ||
       (rx->second.second == contracts::TraceReceptionDisposition::kNotDecoded) !=
           !row->second.crc_ok) {
      return std::unexpected(Error{
          ErrorCode::kFailedPrecondition,
          "Target reception and bit observation disagree"});
    }
    if(metrics.evaluated_payload_bit_count >
           std::numeric_limits<std::size_t>::max() -
               row->second.payload_bit_count ||
       metrics.payload_bit_error_count >
           std::numeric_limits<std::size_t>::max() -
               row->second.bit_error_count) {
      return std::unexpected(Error{ErrorCode::kOverflow,
                                   "Comparison bit counters overflow"});
    }
    ++metrics.evaluated_target_receptions;
    metrics.evaluated_payload_bit_count += row->second.payload_bit_count;
    metrics.payload_bit_error_count += row->second.bit_error_count;
    if(!row->second.crc_ok || row->second.bit_error_count != 0U) {
      ++metrics.packet_error_count;
    }
  }
  if(metrics.evaluated_payload_bit_count != 0U) {
    metrics.bit_error_rate =
        static_cast<double>(metrics.payload_bit_error_count) /
        static_cast<double>(metrics.evaluated_payload_bit_count);
  }
  if(metrics.target_attempt_count != 0U) {
    metrics.packet_error_rate =
        static_cast<double>(metrics.packet_error_count) /
        static_cast<double>(metrics.target_attempt_count);
  }
  return metrics;
}

[[nodiscard]] inline auto ValidateSameTargetAttempts(
    const TargetPacketMetrics& scalar,
    const TargetPacketMetrics& waveform) -> contracts::Status {
  if(scalar.attempts != waveform.attempts) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "Runs do not contain the same ordered target packet attempts"});
  }
  return {};
}

}  // namespace ns3_factory::application::internal
