#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <span>
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

// All operands must ALREADY share one receiver oscillator and integer sample
// clock. Packet-local baseband outputs cannot be passed here without a validated
// time/phase conversion. This primitive does not imply runtime integration.
struct ReceiverSampleContribution {
  std::uint64_t transmission_id;
  std::int64_t first_sample;
  const WaveformBuffer* signal_only;
};

struct ReceiverSampleMixture {
  WaveformBuffer signal_sum;
  WaveformBuffer observed;
  std::size_t contributing_signals;
};

// One desired receive window; finite support outside each input is exactly zero.
// Ambient is ONE slice from the receiver-owned continuous noise realization.
// There is deliberately no noise argument per transmission.
inline auto MixReceiverSamples(
    std::span<const ReceiverSampleContribution> inputs,
    std::int64_t first_sample, const WaveformBuffer& ambient,
    std::size_t maximum_samples, std::size_t maximum_signals)
    -> Result<ReceiverSampleMixture> {
  if(first_sample < 0 || ambient.sample_count() > maximum_samples ||
     inputs.size() > maximum_signals || inputs.empty())
    return std::unexpected(Error{ErrorCode::kOutOfRange, "Receiver mixture budget or window invalid"});
  const auto room = [](std::int64_t start, std::size_t count) {
    return start >= 0 && count <= static_cast<std::uint64_t>(
        std::numeric_limits<std::int64_t>::max() - start);
  };
  if(!room(first_sample, ambient.sample_count()))
    return std::unexpected(Error{ErrorCode::kOverflow, "Receiver window overflow"});
  std::vector<ReceiverSampleContribution> ordered(inputs.begin(), inputs.end());
  std::sort(ordered.begin(), ordered.end(), [](const auto& a, const auto& b) {
    return a.transmission_id < b.transmission_id;
  });
  for(std::size_t i = 0; i < ordered.size(); ++i) {
    const auto& input = ordered[i];
    if(!input.signal_only ||
       input.signal_only->sample_rate_hz() != ambient.sample_rate_hz() ||
       !room(input.first_sample, input.signal_only->sample_count()) ||
       (i && ordered[i-1].transmission_id == input.transmission_id))
      return std::unexpected(Error{ErrorCode::kInvalidArgument,
          "Receiver mixture requires unique signals on the same sample clock"});
  }
  std::vector<WaveformBuffer::Sample> sum(ambient.sample_count());
  std::size_t contributors = 0;
  const auto end = first_sample + static_cast<std::int64_t>(sum.size());
  for(const auto& input : ordered) {
    const auto begin = std::max(first_sample, input.first_sample);
    const auto until = std::min(end, input.first_sample +
        static_cast<std::int64_t>(input.signal_only->sample_count()));
    if(begin >= until) continue;
    ++contributors;
    for(auto sample = begin; sample < until; ++sample)
      sum[static_cast<std::size_t>(sample - first_sample)] +=
          input.signal_only->samples()[static_cast<std::size_t>(sample - input.first_sample)];
  }
  auto signal = WaveformBuffer::Create(ambient.sample_rate_hz(), std::move(sum));
  if(!signal) return std::unexpected(signal.error());
  auto observed = AddWaveforms(*signal, ambient);
  if(!observed) return std::unexpected(observed.error());
  return ReceiverSampleMixture{std::move(*signal), std::move(*observed), contributors};
}
} // namespace ns3_factory::phy::internal
