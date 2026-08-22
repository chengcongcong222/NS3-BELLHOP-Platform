#include <cstddef>
#include <cmath>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/channel_processor.hpp"
#include "internal/modulation.hpp"
#include "internal/noise_generator.hpp"
#include "internal/packet_bit_adapter.hpp"
#include "internal/waveform.hpp"
#include "internal/waveform_pipeline.hpp"
#include "internal/waveform_statistics.hpp"

namespace {

using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PropagationPath;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::UnicastDestination;
using ns3_factory::phy::internal::ApplyMultipath;
using ns3_factory::phy::internal::AwgnNoiseConfig;
using ns3_factory::phy::internal::BitFrame;
using ns3_factory::phy::internal::BuildMultipathTaps;
using ns3_factory::phy::internal::CreateDemodulator;
using ns3_factory::phy::internal::CreateModulator;
using ns3_factory::phy::internal::ExtractPayloadBitFrame;
using ns3_factory::phy::internal::GenerateNoise;
using ns3_factory::phy::internal::ModulationConfig;
using ns3_factory::phy::internal::ModulationScheme;
using ns3_factory::phy::internal::MultipathTap;
using ns3_factory::phy::internal::NoiseProfile;
using ns3_factory::phy::internal::RecoverPayloadBytes;
using ns3_factory::phy::internal::RunWaveformPipeline;
using ns3_factory::phy::internal::WaveformBuffer;
using ns3_factory::phy::internal::WaveformPipelineConfig;
using ns3_factory::phy::internal::WaveformStatisticsAccumulator;
using ns3_factory::phy::internal::WenzCompositePsdDb;
using ns3_factory::phy::internal::WenzNoiseConfig;

auto Check(bool condition, std::string_view message) -> bool {
  if(!condition) {
    std::cerr << "FAILED: " << message << '\n';
  }
  return condition;
}

auto TestDigitalPacketPayloadExtraction() -> bool {
  const DigitalPacket packet{PacketId{10},
                             NodeId{1},
                             UnicastDestination{NodeId{2}},
                             {std::byte{0xA5}, std::byte{0x03}}};
  const auto frame = ExtractPayloadBitFrame(packet);
  const auto expected = BitFrame::Create(
      {1, 0, 1, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 1});
  if(!frame || !expected) {
    return Check(false, "DigitalPacket payload extraction");
  }

  const auto recovered_payload = RecoverPayloadBytes(*frame);
  const auto partial_frame = BitFrame::Create({1, 0, 1});
  if(!partial_frame) {
    return Check(false, "Partial-byte frame fixture creation");
  }
  const auto partial_payload = RecoverPayloadBytes(*partial_frame);

  const DigitalPacket empty_packet{PacketId{11},
                                   NodeId{1},
                                   UnicastDestination{NodeId{2}},
                                   {}};
  const auto empty_frame = ExtractPayloadBitFrame(empty_packet);

  return Check(*frame == *expected, "MSB-first payload bit order") &&
         Check(recovered_payload && *recovered_payload == packet.payload,
               "Recovered payload bytes") &&
         Check(!partial_payload, "Partial-byte payload rejection") &&
         Check(!empty_frame, "Empty DigitalPacket payload rejection");
}

auto TestChannelPathAdapterAndFractionalDelay() -> bool {
  auto first =
      PropagationPath::Create(SimDuration::Zero(), 0.8, 0.25);
  auto second = PropagationPath::Create(
      SimDuration::FromNanoseconds(62'500), 0.2, -0.5);
  if(!first || !second) {
    return Check(false, "PropagationPath fixture creation");
  }
  auto response = ChannelFieldResponse::Create(
      TransmissionId{7},
      NodeId{3},
      40.0,
      SimDuration::FromNanoseconds(1'000'000),
      std::vector<PropagationPath>{*second, *first});
  if(!response) {
    return Check(false, "ChannelFieldResponse fixture creation");
  }
  const auto taps = BuildMultipathTaps(*response);
  if(!taps || taps->size() != 2U ||
     (*taps)[0].excess_delay_seconds != 0.0) {
    return Check(false, "Channel path adapter canonical order");
  }

  auto impulse = WaveformBuffer::Create(
      8'000.0,
      std::vector<WaveformBuffer::Sample>{{1.0, 0.0}, {0.0, 0.0}});
  const std::vector<MultipathTap> fractional{
      MultipathTap{0.0, {1.0, 0.0}},
      MultipathTap{0.5 / 8'000.0, {0.5, 0.0}}};
  if(!impulse) {
    return Check(false, "Impulse waveform fixture creation");
  }
  const auto delayed = ApplyMultipath(*impulse, fractional);
  return Check(delayed && delayed->sample_count() == 3U,
               "Fractional-delay output length") &&
         Check(std::abs(delayed->samples()[0].real() - 1.25) <
                   1.0e-12,
               "Fractional-delay first sample") &&
         Check(std::abs(delayed->samples()[1].real() - 0.25) <
                   1.0e-12,
               "Fractional-delay second sample");
}

auto TestBpskAwgnRoundTrip() -> bool {
  const DigitalPacket packet{PacketId{20},
                             NodeId{1},
                             UnicastDestination{NodeId{2}},
                             {std::byte{0x69}}};
  const auto source = ExtractPayloadBitFrame(packet);
  if(!source) {
    return Check(false, "BPSK DigitalPacket payload extraction");
  }

  const ModulationConfig modulation{ModulationScheme::kBpsk,
                                    8'000.0,
                                    1'000.0,
                                    1.0e6,
                                    12'000.0,
                                    0.0};
  const WaveformPipelineConfig config{
      modulation,
      {MultipathTap{0.0, std::polar(0.9, 0.6)},
       MultipathTap{2.0 / modulation.sample_rate_hz,
                    std::polar(0.05, -0.2)}},
      NoiseProfile{AwgnNoiseConfig{-30.0, 1234U}},
      true};
  const auto result = RunWaveformPipeline(*source, config);
  if(!result) {
    return Check(false, "BPSK pipeline execution");
  }

  const auto recovered_payload = RecoverPayloadBytes(result->recovered());
  WaveformStatisticsAccumulator statistics;
  const auto observed = statistics.Observe(*result);
  return Check(result.has_value(), "BPSK pipeline execution") &&
         Check(result->recovered() == *source, "BPSK recovered bits") &&
         Check(result->bit_error_count() == 0U,
               "BPSK zero bit errors") &&
         Check(result->bit_error_rate() == 0.0, "BPSK zero BER") &&
         Check(!result->packet_error(), "BPSK zero packet error") &&
         Check(recovered_payload && *recovered_payload == packet.payload,
               "BPSK recovered payload bytes") &&
         Check(observed && statistics.packet_count() == 1U,
               "Pipeline-result statistics observation");
}

auto TestBfskWenzRoundTrip() -> bool {
  const DigitalPacket packet{PacketId{21},
                             NodeId{1},
                             UnicastDestination{NodeId{2}},
                             {std::byte{0xB2}}};
  const auto source = ExtractPayloadBitFrame(packet);
  if(!source) {
    return Check(false, "BFSK DigitalPacket payload extraction");
  }

  const ModulationConfig modulation{ModulationScheme::kBfsk,
                                    16'000.0,
                                    1'000.0,
                                    1.0e6,
                                    16'000.0,
                                    500.0};
  const WaveformPipelineConfig config{
      modulation,
      {MultipathTap{0.0, std::polar(1.0, -0.4)},
       MultipathTap{2.0 / modulation.sample_rate_hz,
                    std::polar(0.03, 0.7)}},
      NoiseProfile{WenzNoiseConfig{0.4, 4.0, 5678U}},
      true};
  const auto result = RunWaveformPipeline(*source, config);
  if(!result) {
    return Check(false, "BFSK pipeline execution");
  }

  const auto recovered_payload = RecoverPayloadBytes(result->recovered());
  return Check(result.has_value(), "BFSK pipeline execution") &&
         Check(result->recovered() == *source, "BFSK recovered bits") &&
         Check(result->bit_error_count() == 0U,
               "BFSK zero bit errors") &&
         Check(recovered_payload && *recovered_payload == packet.payload,
               "BFSK recovered payload bytes");
}

auto TestNoiseDeterminismAndSelection() -> bool {
  const NoiseProfile awgn{AwgnNoiseConfig{-20.0, 42U}};
  const auto first_awgn =
      GenerateNoise(awgn, 64U, 8'000.0, 12'000.0);
  const auto second_awgn =
      GenerateNoise(awgn, 64U, 8'000.0, 12'000.0);
  const auto other_awgn = GenerateNoise(
      NoiseProfile{AwgnNoiseConfig{-20.0, 43U}},
      64U,
      8'000.0,
      12'000.0);

  const NoiseProfile wenz{WenzNoiseConfig{0.5, 5.0, 99U}};
  const auto first_wenz =
      GenerateNoise(wenz, 64U, 8'000.0, 12'000.0);
  const auto second_wenz =
      GenerateNoise(wenz, 64U, 8'000.0, 12'000.0);
  const auto low_wind =
      WenzCompositePsdDb(5'000.0, 0.5, 0.0);
  const auto high_wind =
      WenzCompositePsdDb(5'000.0, 0.5, 10.0);

  return Check(first_awgn && second_awgn && other_awgn,
               "AWGN generation") &&
         Check(*first_awgn == *second_awgn,
               "AWGN same-seed determinism") &&
         Check(*first_awgn != *other_awgn,
               "AWGN different-seed separation") &&
         Check(first_wenz && second_wenz, "Wenz noise generation") &&
         Check(*first_wenz == *second_wenz,
               "Wenz same-seed determinism") &&
         Check(low_wind && high_wind && *high_wind > *low_wind,
               "Wenz wind-dependent PSD");
}

auto TestMultiPacketStatistics() -> bool {
  WaveformStatisticsAccumulator statistics;
  const auto first = statistics.Observe(8U, 0U);
  const auto second = statistics.Observe(8U, 2U);
  const auto third = statistics.Observe(16U, 0U);
  const auto invalid = statistics.Observe(7U, 8U);

  WaveformStatisticsAccumulator overflow_statistics;
  const auto maximum = overflow_statistics.Observe(
      std::numeric_limits<std::size_t>::max(), 0U);
  const auto overflow = overflow_statistics.Observe(1U, 0U);

  return Check(first && second && third,
               "Multi-packet statistics observations") &&
         Check(!invalid, "Invalid packet statistics rejection") &&
         Check(statistics.packet_count() == 3U,
               "Statistics packet count") &&
         Check(statistics.packet_error_count() == 1U,
               "Statistics packet error count") &&
         Check(statistics.total_bit_count() == 32U,
               "Statistics total bit count") &&
         Check(statistics.total_bit_error_count() == 2U,
               "Statistics total bit error count") &&
         Check(std::abs(statistics.bit_error_rate() - 0.0625) <
                   1.0e-12,
               "Aggregate BER") &&
         Check(std::abs(statistics.packet_error_rate() - 1.0 / 3.0) <
                   1.0e-12,
               "Aggregate PER") &&
         Check(maximum && !overflow, "Statistics overflow rejection");
}

auto TestQpskExtensionPoint() -> bool {
  const auto modulator = CreateModulator(ModulationScheme::kQpsk);
  const auto demodulator = CreateDemodulator(ModulationScheme::kQpsk);
  return Check(!modulator && !demodulator,
               "QPSK remains an explicit extension point");
}

}  // namespace

int main() {
  const auto passed =
      TestDigitalPacketPayloadExtraction() &&
      TestChannelPathAdapterAndFractionalDelay() &&
      TestBpskAwgnRoundTrip() &&
      TestBfskWenzRoundTrip() &&
      TestNoiseDeterminismAndSelection() &&
      TestMultiPacketStatistics() &&
      TestQpskExtensionPoint();
  if(!passed) {
    return 1;
  }
  std::cout << "waveform PHY pipeline tests passed\n";
  return 0;
}
