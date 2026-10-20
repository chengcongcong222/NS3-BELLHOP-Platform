#pragma once

#include <algorithm>
#include <numeric>
#include "recorded_waveform_channel.hpp"

namespace ns3_factory::phy::internal {

// Explicit proxy convention: packet-reset Tx oscillator, independently replayed
// CIRs and modeled delays. Add REAL passband samples on the receiver clock, then
// downconvert once in the desired arrival's oscillator frame. CIR phases remain
// intact. This does not claim synchronized oscillators in the original sea trial.
// Legacy v4 continues to use RecordedWaveformChannel directly.
class RecordedProxyWaveformChannel final : public RecordedWaveformChannel {
 public:
  static auto Create(std::vector<RecordedLink> links)
      -> Result<RecordedProxyWaveformChannel> {
    auto channel = RecordedWaveformChannel::Create(std::move(links));
    if(!channel) return std::unexpected(channel.error());
    for(const auto& a : channel->links()) {
      if(!RecordingSampleIndex(0,a.channel->delay_rate_hz))
        return std::unexpected(Error{ErrorCode::kInvalidArgument,
            "Proxy recording clock has no supported exact rational representation"});
      for(const auto& b : channel->links()) {
        if(a.receiver != b.receiver) continue;
        if(a.baseband_rate_hz != b.baseband_rate_hz ||
           a.passband_rate_hz != b.passband_rate_hz ||
           a.channel->carrier_hz != b.channel->carrier_hz ||
           a.noise != b.noise)
          return std::unexpected(Error{ErrorCode::kInvalidArgument,
              "Proxy receiver requires one sample clock, carrier and noise owner"});
      }
      if(!SampleIndex(a.modeled_reference_delay.nanoseconds(), a.passband_rate_hz))
        return std::unexpected(Error{ErrorCode::kInvalidArgument,
            "Proxy delay is not on the passband sample grid"});
    }
    return RecordedProxyWaveformChannel{std::move(*channel)};
  }

  auto Query(const contracts::ChannelQuery& query) const
      -> Result<contracts::ChannelFieldOutcome> override {
    for(const auto& l : links()) {
      if(l.transmitter == query.sender_node_id() && l.receiver == query.receiver_node_id()) {
        if(!SampleIndex(query.emitted_at().nanoseconds(), l.passband_rate_hz) ||
           !RecordingSampleIndex(query.emitted_at().nanoseconds(), l.channel->delay_rate_hz))
          return std::unexpected(Error{ErrorCode::kInvalidArgument,
              "Proxy emission is not on both receiver and recording sample grids"});
      }
    }
    return RecordedWaveformChannel::Query(query);
  }

  auto SupportsMixture() const noexcept -> bool override { return true; }
  auto EmissionQuantumNanoseconds() const -> Result<std::int64_t> override {
    std::uint64_t quantum=1;
    auto merge=[&](std::uint64_t next)->bool {
      const auto factor=next/std::gcd(quantum,next);
      if(quantum>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())/factor)return false;
      quantum*=factor;
      return true;
    };
    for(const auto& l:links()) {
      const auto numerator=static_cast<std::uint64_t>(l.channel->delay_rate_hz*1'000'000);
      if(!merge(1'000'000'000/std::gcd(std::uint64_t{1'000'000'000},std::uint64_t{l.passband_rate_hz})) ||
         !merge(1'000'000'000'000'000ULL/std::gcd(1'000'000'000'000'000ULL,
                                               static_cast<unsigned long long>(numerator))))
        return std::unexpected(Error{ErrorCode::kOverflow,"Combined recording clock exceeds simulation time"});
    }
    return static_cast<std::int64_t>(quantum);
  }
  auto Process(const WaveformBuffer& input, const contracts::TxEmission& emission,
               const contracts::RecordedChannelResponse& response) const
      -> Result<RecordedWaveforms> override {
    const RecordedWaveformInput item{&input, &emission, &response};
    return ProcessWindow(std::span{&item, 1}, 10'000'000);
  }

  auto ProcessWindow(std::span<const RecordedWaveformInput> inputs,
                     std::size_t maximum_samples) const -> Result<RecordedWaveforms> override {
    if(inputs.empty() || !maximum_samples)
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Empty recorded receiver window"});
    // First is desired; sort only interferers to avoid insertion-order effects.
    std::vector<RecordedWaveformInput> ordered(inputs.begin(), inputs.end());
    for(const auto& i : ordered)
      if(!i.waveform || !i.emission || !i.response ||
         i.response->operator_key() >= links().size())
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid recorded mixture input"});
    std::sort(ordered.begin()+1, ordered.end(), [](const auto& a, const auto& b) {
      return a.emission->transmission_id().value() < b.emission->transmission_id().value();
    });
    const auto& desired = ordered.front();
    const auto& dl = links()[desired.response->operator_key()];
    auto arrival = contracts::CheckedAdd(desired.emission->started_at(), desired.response->reference_delay());
    if(!arrival) return std::unexpected(Error{ErrorCode::kOverflow,"Desired arrival overflow"});
    auto first = SampleIndex(arrival->nanoseconds(), dl.passband_rate_hz);
    if(!first) return std::unexpected(first.error());
    std::vector<double> sum;
    for(std::size_t k=0; k<ordered.size(); ++k) {
      const auto& i = ordered[k];
      const auto& l = links()[i.response->operator_key()];
      auto recording_offset=RecordingSampleIndex(i.emission->started_at().nanoseconds(),
          l.channel->delay_rate_hz);
      if(!recording_offset) return std::unexpected(recording_offset.error());
      if(static_cast<std::uint64_t>(*recording_offset) >
          std::numeric_limits<std::uint64_t>::max()-l.start_sample ||
         i.response->start_sample()!=l.start_sample+static_cast<std::uint64_t>(*recording_offset))
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Proxy recording time reference mismatch"});
      for(std::size_t j=0; j<k; ++j)
        if(ordered[j].emission->transmission_id() == i.emission->transmission_id())
          return std::unexpected(Error{ErrorCode::kInvalidArgument,"Duplicate mixture transmission"});
      if(i.emission->transmission_id() != i.response->transmission_id() ||
         i.emission->sender_node_id() != l.transmitter || i.response->receiver_node_id() != l.receiver ||
         l.receiver != dl.receiver || i.waveform->sample_rate_hz() != l.baseband_rate_hz ||
         i.emission->center_frequency_hz() != dl.channel->carrier_hz ||
         i.emission->bandwidth_hz() > l.usable_bandwidth_hz ||
         i.response->reference_delay() != l.modeled_reference_delay)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Recorded mixture identity or band mismatch"});
      auto count = RecordedWaveformChannel::OutputCount(l, i.waveform->sample_count());
      if(!count) return std::unexpected(count.error());
      if(count->baseband_count > maximum_samples ||
         i.response->receive_duration().nanoseconds() != static_cast<std::int64_t>(
             std::ceil(static_cast<long double>(count->baseband_count)*1e9L/l.baseband_rate_hz)))
        return std::unexpected(Error{ErrorCode::kOutOfRange,"Recorded mixture envelope or budget mismatch"});
      auto at = contracts::CheckedAdd(i.emission->started_at(), i.response->reference_delay());
      if(!at) return std::unexpected(Error{ErrorCode::kOverflow,"Interferer arrival overflow"});
      auto start = SampleIndex(at->nanoseconds(), dl.passband_rate_hz);
      if(!start) return std::unexpected(start.error());
      auto up = recorded::Resample(i.waveform->samples(), l.passband_rate_hz, l.baseband_rate_hz);
      if(!up) return std::unexpected(up.error());
      std::vector<double> passband(up->size());
      for(std::size_t n=0; n<passband.size(); ++n)
        passband[n] = std::real((*up)[n] * std::polar(1.0,
            2*std::numbers::pi*l.channel->carrier_hz*n/l.passband_rate_hz));
      auto propagated = recorded::Replay(*l.channel, passband, l.passband_rate_hz, i.response->start_sample());
      if(!propagated) return std::unexpected(propagated.error());
      if(propagated->size() > 10'000'000)
        return std::unexpected(Error{ErrorCode::kOutOfRange,"Proxy passband sample budget exceeded"});
      if(k == 0) sum.resize(propagated->size());
      if(*start > std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(propagated->size()) ||
         *first > std::numeric_limits<std::int64_t>::max() - static_cast<std::int64_t>(sum.size()))
        return std::unexpected(Error{ErrorCode::kOverflow,"Proxy receiver interval overflow"});
      const auto begin = std::max(*first, *start);
      const auto end = std::min(*first + static_cast<std::int64_t>(sum.size()),
                               *start + static_cast<std::int64_t>(propagated->size()));
      for(auto n=begin; n<end; ++n)
        sum[static_cast<std::size_t>(n-*first)] +=
            (*propagated)[static_cast<std::size_t>(n-*start)] * l.relative_amplitude;
    }
    if(dl.noise && (static_cast<std::uint64_t>(*first) > dl.noise->size() ||
                   sum.size() > dl.noise->size()-static_cast<std::size_t>(*first)))
      return std::unexpected(Error{ErrorCode::kOutOfRange,"Receiver noise does not cover window"});
    std::vector<recorded::Complex> signal(sum.size()), noise(sum.size());
    for(std::size_t n=0; n<sum.size(); ++n) {
      const auto phase = std::polar(2.0, -2*std::numbers::pi*dl.channel->carrier_hz*n/dl.passband_rate_hz);
      signal[n] = sum[n]*phase;
      noise[n] = (dl.noise ? (*dl.noise)[static_cast<std::size_t>(*first)+n] : 0.0)*phase;
    }
    auto bb = recorded::Resample(signal, dl.baseband_rate_hz, dl.passband_rate_hz);
    auto nb = recorded::Resample(noise, dl.baseband_rate_hz, dl.passband_rate_hz);
    if(!bb) return std::unexpected(bb.error());
    if(!nb) return std::unexpected(nb.error());
    if(bb->size() > maximum_samples)
      return std::unexpected(Error{ErrorCode::kOutOfRange,"Proxy baseband sample budget exceeded"});
    auto output = WaveformBuffer::Create(dl.baseband_rate_hz, std::move(*bb));
    auto ambient = WaveformBuffer::Create(dl.baseband_rate_hz, std::move(*nb));
    if(!output) return std::unexpected(output.error());
    if(!ambient) return std::unexpected(ambient.error());
    return RecordedWaveforms{std::move(*output),std::move(*ambient)};
  }

 private:
  static auto SampleIndex(std::int64_t ns, std::size_t rate) -> Result<std::int64_t> {
    if(ns < 0 || !rate || rate > 1'000'000'000)
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid proxy sample clock"});
    const auto divisor = std::gcd(rate, std::size_t{1'000'000'000});
    const auto denominator = 1'000'000'000 / divisor, numerator = rate / divisor;
    if(static_cast<std::uint64_t>(ns) % denominator)
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Fractional proxy sample alignment unsupported"});
    const auto quotient = static_cast<std::uint64_t>(ns)/denominator;
    if(quotient > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())/numerator)
      return std::unexpected(Error{ErrorCode::kOverflow,"Proxy sample offset overflow"});
    return static_cast<std::int64_t>(quotient*numerator);
  }
  // UWA Blue's 9765.625 Hz is rational, not an integer clock. Preserve it
  // exactly; this is integer indexing, NOT a fractional-delay approximation.
  static auto RecordingSampleIndex(std::int64_t ns,double rate)->Result<std::int64_t> {
    constexpr std::uint64_t scale=1'000'000;
    const auto scaled=rate*scale;
    if(!std::isfinite(scaled)||scaled<=0||scaled>1e15||scaled!=std::floor(scaled)||ns<0)
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Unsupported rational recording clock"});
    const auto numerator0=static_cast<std::uint64_t>(scaled);
    const auto divisor=std::gcd(numerator0,1'000'000'000ULL*scale);
    const std::uint64_t denominator=1'000'000'000ULL*scale/divisor;
    const std::uint64_t numerator=numerator0/divisor;
    if(static_cast<std::uint64_t>(ns)%denominator)
      return std::unexpected(Error{ErrorCode::kInvalidArgument,"Emission is off recording sample grid"});
    const auto quotient=static_cast<std::uint64_t>(ns)/denominator;
    if(quotient>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())/numerator)
      return std::unexpected(Error{ErrorCode::kOverflow,"Recording sample offset overflow"});
    return static_cast<std::int64_t>(quotient*numerator);
  }
  explicit RecordedProxyWaveformChannel(RecordedWaveformChannel channel): RecordedWaveformChannel(std::move(channel)) {}
};
} // namespace ns3_factory::phy::internal
