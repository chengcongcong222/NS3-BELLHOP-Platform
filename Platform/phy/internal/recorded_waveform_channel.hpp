#pragma once

#include <memory>
#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>
#include "recorded_replay.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

struct RecordedWaveforms {
  WaveformBuffer channel_output;
  WaveformBuffer noise;
};

struct RecordedWaveformInput {
  const WaveformBuffer* waveform;
  const contracts::TxEmission* emission;
  const contracts::RecordedChannelResponse* response;
};

// Run-owned bridge: numerical samples never enter runtime DTOs or traces.
class IRecordedWaveformChannel {
 public:
  virtual ~IRecordedWaveformChannel() = default;
  virtual auto Process(const WaveformBuffer&, const contracts::TxEmission&,
      const contracts::RecordedChannelResponse&) const -> Result<RecordedWaveforms> = 0;
  virtual auto SupportsMixture() const noexcept -> bool { return false; }
  virtual auto ProcessWindow(std::span<const RecordedWaveformInput>, std::size_t) const
      -> Result<RecordedWaveforms> {
    return std::unexpected(Error{ErrorCode::kUnsupported,"Recorded mixture is not enabled"});
  }
};

struct RecordedLink {
  contracts::NodeId transmitter,receiver;
  std::shared_ptr<const recorded::Channel> channel;
  std::uint64_t start_sample;
  contracts::SimDuration modeled_reference_delay;
  // Relative to the library normalization, never dB re 1 uPa.
  double relative_amplitude;
  std::size_t baseband_rate_hz, passband_rate_hz;
  double usable_bandwidth_hz;
  // One continuous, frozen passband-noise realization. Empty means explicit
  // noiseless experiment; never regenerate/normalize independently per packet.
  std::shared_ptr<const std::vector<double>> noise;
};

class RecordedWaveformChannel : public contracts::IChannelFieldProvider,
                                      public IRecordedWaveformChannel {
 public:
  static auto Create(std::vector<RecordedLink> links) -> Result<RecordedWaveformChannel> {
    if(links.empty()) return std::unexpected(Error{ErrorCode::kInvalidArgument,"No recorded links bound"});
    for(std::size_t i=0;i<links.size();++i) {
      const auto& l=links[i];
      if(!l.channel||l.transmitter==l.receiver||!l.baseband_rate_hz||!l.passband_rate_hz||
         !std::isfinite(l.relative_amplitude)||l.relative_amplitude<=0||
         !std::isfinite(l.usable_bandwidth_hz)||l.usable_bandwidth_hz<=0||
         l.modeled_reference_delay.nanoseconds()<0||
         !std::isfinite(l.channel->carrier_hz)||l.channel->carrier_hz<=0||
         !std::isfinite(l.channel->delay_rate_hz)||l.channel->delay_rate_hz<=0||
         !std::isfinite(l.channel->snapshot_rate_hz)||
         l.passband_rate_hz<=2*l.channel->carrier_hz+l.usable_bandwidth_hz||
         !l.channel->delay_rate_hz||l.channel->snapshots<2||l.channel->snapshot_rate_hz<=0)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid recorded link configuration"});
      for(std::size_t j=0;j<i;++j) if(links[j].transmitter==l.transmitter&&links[j].receiver==l.receiver)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Duplicate directed recorded link"});
    }
    return RecordedWaveformChannel{std::move(links)};
  }

  auto Query(const contracts::ChannelQuery& q) const -> Result<contracts::ChannelFieldOutcome> override {
    for(std::size_t i=0;i<links_.size();++i) {
      const auto& l=links_[i];
      if(l.transmitter!=q.sender_node_id()||l.receiver!=q.receiver_node_id()) continue;
      if(q.center_frequency_hz()!=l.channel->carrier_hz||q.bandwidth_hz()>l.usable_bandwidth_hz||
         q.encoded_duration().nanoseconds()<=0||q.emitted_at().nanoseconds()<0)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Recorded link band/time mismatch"});
      const auto n=static_cast<std::size_t>(std::llround(
          static_cast<long double>(q.encoded_duration().nanoseconds())*l.baseband_rate_hz/1e9L));
      auto count=OutputCount(l,n);
      if(!count) return std::unexpected(count.error());
      const auto offset=static_cast<long double>(q.emitted_at().nanoseconds())*l.channel->delay_rate_hz/1e9L;
      if(offset>std::numeric_limits<std::uint64_t>::max()-l.start_sample)
        return std::unexpected(Error{ErrorCode::kOverflow,"Recording offset overflow"});
      const auto start=l.start_sample+static_cast<std::uint64_t>(std::floor(offset));
      // Full library window and phase coverage, not just the packet start.
      if(start>std::numeric_limits<std::uint64_t>::max()-count->library_count||
         static_cast<long double>(start+count->library_count-1)/l.channel->delay_rate_hz>
             (l.channel->snapshots-1)/l.channel->snapshot_rate_hz||
         (!l.channel->phase.empty()&&start+count->library_count>l.channel->phase.size()))
        return std::unexpected(Error{ErrorCode::kOutOfRange,"Recording window is too short for packet and channel tail"});
      const auto duration=contracts::SimDuration::FromNanoseconds(static_cast<std::int64_t>(
          std::ceil(static_cast<long double>(count->baseband_count)*1e9L/l.baseband_rate_hz)));
      auto response=contracts::RecordedChannelResponse::Create(q.transmission_id(),q.receiver_node_id(),
          i,start,l.modeled_reference_delay,duration);
      if(!response) return std::unexpected(response.error());
      return contracts::ChannelFieldOutcome{*response};
    }
    return std::unexpected(Error{ErrorCode::kNotFound,
        "No measured recording bound for this directed physical link; reverse links are not inferred"});
  }

  auto Process(const WaveformBuffer& input,const contracts::TxEmission& e,
      const contracts::RecordedChannelResponse& response) const -> Result<RecordedWaveforms> override {
    if(response.operator_key()>=links_.size())
      return std::unexpected(Error{ErrorCode::kNotFound,"Recorded operator key unavailable"});
    const auto& l=links_[response.operator_key()];
    if(e.transmission_id()!=response.transmission_id()||e.sender_node_id()!=l.transmitter||
       response.receiver_node_id()!=l.receiver||input.sample_rate_hz()!=l.baseband_rate_hz)
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,"Recorded operator identity mismatch"});
    auto up=recorded::Resample(input.samples(),l.passband_rate_hz,l.baseband_rate_hz);
    if(!up) return std::unexpected(up.error());
    std::vector<double> passband(up->size());
    for(std::size_t i=0;i<passband.size();++i)
      passband[i]=std::real((*up)[i]*std::polar(1.0,2*std::numbers::pi*l.channel->carrier_hz*i/l.passband_rate_hz));
    auto output=recorded::Replay(*l.channel,passband,l.passband_rate_hz,response.start_sample());
    if(!output) return std::unexpected(output.error());
    const auto at=contracts::CheckedAdd(e.started_at(),response.reference_delay());
    if(!at||at->nanoseconds()<0) return std::unexpected(Error{ErrorCode::kOverflow,"Noise time overflow"});
    const auto first=static_cast<std::size_t>(std::floor(
        static_cast<long double>(at->nanoseconds())*l.passband_rate_hz/1e9L));
    if(l.noise && (first>l.noise->size()||output->size()>l.noise->size()-first))
      return std::unexpected(Error{ErrorCode::kOutOfRange,"Continuous noise does not cover receive window"});
    std::vector<recorded::Complex> mixed(output->size()),noise(output->size());
    for(std::size_t i=0;i<output->size();++i) {
      const auto phase=std::polar(2.0,-2*std::numbers::pi*l.channel->carrier_hz*i/l.passband_rate_hz);
      mixed[i]=(*output)[i]*l.relative_amplitude*phase;
      noise[i]=(l.noise?(*l.noise)[first+i]:0.0)*phase;
    }
    auto bb=recorded::Resample(mixed,l.baseband_rate_hz,l.passband_rate_hz);
    auto nb=recorded::Resample(noise,l.baseband_rate_hz,l.passband_rate_hz);
    if(!bb) return std::unexpected(bb.error());
    if(!nb) return std::unexpected(nb.error());
    auto expected=OutputCount(l,input.sample_count());
    if(!expected||bb->size()!=expected->baseband_count||
       response.receive_duration().nanoseconds()!=static_cast<std::int64_t>(
           std::ceil(static_cast<long double>(bb->size())*1e9L/l.baseband_rate_hz)))
      return std::unexpected(Error{ErrorCode::kFailedPrecondition,"Recorded receive envelope does not match waveform"});
    auto signal=WaveformBuffer::Create(input.sample_rate_hz(),std::move(*bb));
    auto ambient=WaveformBuffer::Create(input.sample_rate_hz(),std::move(*nb));
    if(!signal) return std::unexpected(signal.error());
    if(!ambient) return std::unexpected(ambient.error());
    return RecordedWaveforms{std::move(*signal),std::move(*ambient)};
  }

  struct Counts { std::size_t library_count,baseband_count; };
  static auto OutputCount(const RecordedLink& l,std::size_t input) -> Result<Counts> {
    const auto ceil_ratio=[](std::size_t n,std::size_t u,std::size_t d)->Result<std::size_t> {
      if(!d||!u||!n||n>(std::numeric_limits<std::size_t>::max()-d+1)/u)
        return std::unexpected(Error{ErrorCode::kOutOfRange,"Recorded sample count overflow"});
      const auto value=(n*u+d-1)/d;
      if(value>10'000'000) return std::unexpected(Error{ErrorCode::kOutOfRange,"Recorded sample budget exceeded"});
      return value;
    };
    auto ratio=recorded::Ratio(l.channel->delay_rate_hz/l.passband_rate_hz);
    if(!ratio) return std::unexpected(ratio.error());
    auto count=ceil_ratio(input,l.passband_rate_hz,l.baseband_rate_hz);
    if(count) count=ceil_ratio(*count,ratio->first,ratio->second);
    if(!count) return std::unexpected(count.error());
    const auto library=*count+l.channel->delay_taps;
    count=ceil_ratio(library,ratio->second,ratio->first);
    if(count) count=ceil_ratio(*count,l.channel->resample_up,l.channel->resample_down);
    if(count) count=ceil_ratio(*count,l.baseband_rate_hz,l.passband_rate_hz);
    if(!count) return std::unexpected(count.error());
    return Counts{library,*count};
  }
  auto links() const noexcept -> std::span<const RecordedLink> { return links_; }
 private:
  explicit RecordedWaveformChannel(std::vector<RecordedLink> links):links_(std::move(links)){}
  std::vector<RecordedLink> links_;
};
} // namespace ns3_factory::phy::internal
