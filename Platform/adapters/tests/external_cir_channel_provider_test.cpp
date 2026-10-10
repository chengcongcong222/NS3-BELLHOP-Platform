#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string_view>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>

#include "internal/external_cir_channel_provider.hpp"
#include "internal/channel_processor.hpp"
#include "internal/modulation.hpp"
#include "internal/noise_generator.hpp"
#include "internal/waveform_pipeline.hpp"

namespace {

using ns3_factory::adapters::internal::ExternalCirChannelProvider;
using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ChannelQuery;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::phy::internal::ApplyMultipath;
using ns3_factory::phy::internal::BitFrame;
using ns3_factory::phy::internal::BuildBasebandMultipathTaps;
using ns3_factory::phy::internal::ChannelEqualizationMode;
using ns3_factory::phy::internal::BuildDelayAlignmentMatrix;
using ns3_factory::phy::internal::ModulationConfig;
using ns3_factory::phy::internal::ModulationScheme;
using ns3_factory::phy::internal::OsdmConfig;
using ns3_factory::phy::internal::NoNoiseConfig;
using ns3_factory::phy::internal::NoiseProfile;
using ns3_factory::phy::internal::RunWaveformPipeline;
using ns3_factory::phy::internal::WaveformBuffer;
using ns3_factory::phy::internal::WaveformPipelineConfig;

auto Check(bool condition, std::string_view message) -> bool {
  if(!condition) std::cerr << "FAILED: " << message << '\n';
  return condition;
}

class TemporaryCirPackage final {
 public:
  TemporaryCirPackage()
      : directory_(std::filesystem::temp_directory_path() /
                   "ns3_factory_channel3_external_cir_test") {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
    std::filesystem::create_directories(directory_);
  }

  ~TemporaryCirPackage() {
    std::error_code ignored;
    std::filesystem::remove_all(directory_, ignored);
  }

  [[nodiscard]] auto descriptor_file() const -> std::filesystem::path {
    return directory_ / "channel.cir.json";
  }

  [[nodiscard]] auto data_file() const -> std::filesystem::path {
    return directory_ / "channel.cir.bin";
  }

 private:
  std::filesystem::path directory_;
};

template <typename Scalar>
void WriteLittleEndian(std::ofstream& output, Scalar value) {
  auto bytes = std::bit_cast<std::array<std::byte, sizeof(Scalar)>>(value);
  if constexpr(std::endian::native == std::endian::big) {
    std::reverse(bytes.begin(), bytes.end());
  }
  output.write(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

void WriteSamples(const std::filesystem::path& file,
                  const std::vector<std::complex<double>>& samples) {
  std::ofstream output{file, std::ios::binary};
  for(const auto& sample : samples) {
    WriteLittleEndian(output, sample.real());
    WriteLittleEndian(output, sample.imag());
  }
}

void WriteDescriptor(const std::filesystem::path& file,
                     std::size_t delay_bins,
                     std::size_t receivers,
                     std::size_t snapshots) {
  std::ofstream output{file};
  output << "{\n"
         << "  \"format_version\": 1,\n"
         << "  \"data_file\": \"channel.cir.bin\",\n"
         << "  \"scalar_encoding\": \"float64\",\n"
         << "  \"byte_order\": \"little\",\n"
         << "  \"complex_layout\": \"iq_interleaved\",\n"
         << "  \"tensor_layout\": \"snapshot_receiver_delay\",\n"
         << "  \"delay_bin_count\": " << delay_bins << ",\n"
         << "  \"receiver_count\": " << receivers << ",\n"
         << "  \"snapshot_count\": " << snapshots << ",\n"
         << "  \"delay_sample_rate_hz\": 1000.0,\n"
         << "  \"snapshot_rate_hz\": 2.0,\n"
         << "  \"center_frequency_hz\": 12345.0,\n"
         << "  \"bandwidth_hz\": 2000.0,\n"
         << "  \"center_frequency_tolerance_hz\": 0.0,\n"
         << "  \"cir_origin_delay_ns\": 2000000,\n"
         << "  \"snapshot_origin_ns\": 1000000000,\n"
         << "  \"minimum_tap_magnitude\": 1e-12,\n"
         << "  \"sender_node_id\": 1,\n"
         << "  \"receiver_bindings\": [\n"
         << "    {\"node_id\": 2, \"receiver_index\": 0},\n"
         << "    {\"node_id\": 3, \"receiver_index\": 1}\n"
         << "  ]\n"
         << "}\n";
}

auto MakeQuery(TransmissionId transmission,
               NodeId receiver,
               std::int64_t emitted_at_ns,
               double center_frequency_hz = 12345.0,
               double bandwidth_hz = 1000.0)
    -> ns3_factory::contracts::Result<ChannelQuery> {
  return ChannelQuery::Create(transmission,
                              NodeId{1},
                              receiver,
                              Position3d{0.0, 0.0, 0.0},
                              Position3d{100.0, 0.0, 0.0},
                              SimTime::FromNanoseconds(emitted_at_ns),
                              center_frequency_hz,
                              bandwidth_hz);
}

auto Response(const ExternalCirChannelProvider& provider,
              const ChannelQuery& query)
    -> ns3_factory::contracts::Result<ChannelFieldResponse> {
  auto outcome = provider.Query(query);
  if(!outcome) return std::unexpected(outcome.error());
  const auto* response = std::get_if<ChannelFieldResponse>(&*outcome);
  if(response == nullptr) {
    return std::unexpected(ns3_factory::contracts::Error{
        ns3_factory::contracts::ErrorCode::kFailedPrecondition,
        "Expected external CIR response"});
  }
  return *response;
}

auto TestDescriptorAndTensorSelection() -> bool {
  TemporaryCirPackage package;
  constexpr auto delay_bins = std::size_t{4};
  constexpr auto receivers = std::size_t{2};
  constexpr auto snapshots = std::size_t{2};
  WriteDescriptor(package.descriptor_file(), delay_bins, receivers, snapshots);

  std::vector<std::complex<double>> samples(
      delay_bins * receivers * snapshots);
  samples[1] = std::polar(0.8, 0.3);
  samples[3] = std::polar(0.2, -0.4);
  samples[delay_bins] = std::polar(0.6, -0.2);
  samples[delay_bins * receivers] = std::polar(0.5, 0.7);
  samples[delay_bins * receivers + delay_bins + 2] =
      std::polar(0.4, -0.8);
  WriteSamples(package.data_file(), samples);

  const auto provider =
      ExternalCirChannelProvider::Create(package.descriptor_file());
  const auto query0 = MakeQuery(TransmissionId{10}, NodeId{2}, 1'000'000'000);
  const auto query1 = MakeQuery(TransmissionId{11}, NodeId{3}, 1'600'000'000);
  if(!provider || !query0 || !query1) {
    return Check(false, "External CIR provider fixtures");
  }
  const auto response0 = Response(*provider, *query0);
  const auto response1 = Response(*provider, *query1);
  if(!response0 || !response1) {
    return Check(false, "External CIR provider queries");
  }
  const auto taps0 = BuildBasebandMultipathTaps(*response0, 12345.0);
  const auto taps1 = BuildBasebandMultipathTaps(*response1, 12345.0);

  return Check(response0->first_arrival_delay().nanoseconds() == 3'000'000,
               "Leading delay bin contributes to first arrival") &&
         Check(taps0 && taps0->size() == 2U,
               "External CIR active tap count") &&
         Check(std::abs((*taps0)[0].pressure_gain -
                        std::polar(0.8, 0.3)) < 1e-12,
               "First imported complex tap is preserved") &&
         Check(std::abs((*taps0)[1].pressure_gain -
                        std::polar(0.2, -0.4)) < 1e-12,
               "Delayed imported complex tap is preserved") &&
         Check(std::abs((*taps0)[1].excess_delay_seconds - 0.002) < 1e-15,
               "External CIR delay spacing") &&
         Check(response1->first_arrival_delay().nanoseconds() == 4'000'000,
               "Time-varying snapshot first arrival") &&
         Check(taps1 && taps1->size() == 1U &&
                   std::abs((*taps1)[0].pressure_gain -
                            std::polar(0.4, -0.8)) < 1e-12,
               "Emission time and receiver select tensor slice");
}

auto TestCoverageAndFileValidation() -> bool {
  TemporaryCirPackage package;
  WriteDescriptor(package.descriptor_file(), 1U, 2U, 2U);
  WriteSamples(package.data_file(),
               std::vector<std::complex<double>>(4U, {1.0, 0.0}));
  const auto provider =
      ExternalCirChannelProvider::Create(package.descriptor_file());
  if(!provider) return Check(false, "Coverage provider creation");

  const auto before = MakeQuery(TransmissionId{20}, NodeId{2}, 999'999'999);
  const auto after = MakeQuery(TransmissionId{21}, NodeId{2}, 2'000'000'000);
  const auto receiver =
      MakeQuery(TransmissionId{22}, NodeId{99}, 1'000'000'000);
  const auto frequency = MakeQuery(
      TransmissionId{23}, NodeId{2}, 1'000'000'000, 12000.0);
  if(!before || !after || !receiver || !frequency) {
    return Check(false, "Coverage query fixtures");
  }

  const auto before_result = provider->Query(*before);
  const auto after_result = provider->Query(*after);
  const auto receiver_result = provider->Query(*receiver);
  const auto frequency_result = provider->Query(*frequency);

  std::ofstream truncated{
      package.data_file(), std::ios::binary | std::ios::trunc};
  WriteLittleEndian(truncated, 1.0);
  truncated.close();
  const auto bad_size =
      ExternalCirChannelProvider::Create(package.descriptor_file());

  return Check(!before_result, "Reject query before CIR coverage") &&
         Check(!after_result, "Reject query after CIR coverage") &&
         Check(!receiver_result, "Reject unbound receiver") &&
         Check(!frequency_result, "Reject incompatible center frequency") &&
         Check(!bad_size, "Reject binary size mismatch");
}

auto TestChannel3ReusesWaveformPipeline() -> bool {
  TemporaryCirPackage package;
  WriteDescriptor(package.descriptor_file(), 1U, 2U, 1U);
  WriteSamples(package.data_file(),
               {{std::polar(0.9, 0.4)}, {0.0, 0.0}});
  const auto provider =
      ExternalCirChannelProvider::Create(package.descriptor_file());
  const auto query = MakeQuery(TransmissionId{30}, NodeId{2}, 1'000'000'000);
  if(!provider || !query) {
    return Check(false, "Channel 3 pipeline fixtures");
  }
  const auto response = Response(*provider, *query);
  if(!response) return Check(false, "Channel 3 response");
  const auto taps = BuildBasebandMultipathTaps(*response, 12345.0);
  const auto source = BitFrame::Create({0, 1, 1, 0, 1, 0, 0, 1});
  if(!taps || !source) return Check(false, "Channel 3 taps and bits");

  const ModulationConfig modulation{ModulationScheme::kOsdm,
                                    8000.0,
                                    8000.0,
                                    1.0,
                                    12345.0,
                                    0.0,
                                    OsdmConfig{8U, 2U, 2U}};
  const auto result = RunWaveformPipeline(
      *source,
      WaveformPipelineConfig{modulation,
                             *taps,
                             NoiseProfile{NoNoiseConfig{}},
                             {ChannelEqualizationMode::kIdealFrequencyDomainMmse},
                             true});
  return Check(result && result->recovered() == *source,
               "External CIR reuses the OSDM waveform modules") &&
         Check(result && result->bit_error_count() == 0U,
               "Channel 3 round trip has no fixture bit errors");
}

auto TestNonIntegerRateAlignmentMatrix() -> bool {
  TemporaryCirPackage package;
  // External CIR: 1000 Hz. Waveform: 7350 Hz. Bin 1 lands at sample 7.35.
  WriteDescriptor(package.descriptor_file(), 2U, 2U, 1U);
  WriteSamples(package.data_file(),
               {{1.0, 0.0}, {0.4, -0.2}, {0.0, 0.0}, {0.0, 0.0}});
  const auto provider =
      ExternalCirChannelProvider::Create(package.descriptor_file());
  const auto query = MakeQuery(TransmissionId{31}, NodeId{2}, 1'000'000'000);
  if(!provider || !query) return Check(false, "Rate alignment fixtures");
  const auto response = Response(*provider, *query);
  if(!response) return Check(false, "Rate alignment CIR response");
  const auto taps = BuildBasebandMultipathTaps(*response, 12345.0);
  if(!taps) return Check(false, "Rate alignment path conversion");

  const auto matrix = BuildDelayAlignmentMatrix(*taps, 7350.0);
  if(!matrix || matrix->row_count != 9U ||
     matrix->column_count != 2U || matrix->entries.size() != 3U) {
    return Check(false, "Sparse delay alignment matrix dimensions");
  }
  const auto& entries = matrix->entries;
  if(!Check(entries[0].waveform_delay_index == 0U &&
                entries[0].path_index == 0U &&
                std::abs(entries[0].weight - 1.0) < 1e-12,
            "Direct path matrix column") ||
     !Check(entries[1].waveform_delay_index == 7U &&
                entries[1].path_index == 1U &&
                std::abs(entries[1].weight - 0.65) < 1e-12,
            "Fractional path lower matrix weight") ||
     !Check(entries[2].waveform_delay_index == 8U &&
                entries[2].path_index == 1U &&
                std::abs(entries[2].weight - 0.35) < 1e-12,
            "Fractional path upper matrix weight")) {
    return false;
  }
  const std::vector<std::complex<double>> gains{
      (*taps)[0].pressure_gain, (*taps)[1].pressure_gain};
  const auto aligned = matrix->Multiply(gains);
  const auto impulse = WaveformBuffer::Create(
      7350.0, {{1.0, 0.0}, {0.0, 0.0}});
  if(!aligned || !impulse) return Check(false, "Matrix product fixtures");
  const auto output = ApplyMultipath(*impulse, *taps);
  return Check(aligned->size() == 9U &&
                   std::abs((*aligned)[7] -
                            std::complex<double>{0.26, -0.13}) < 1e-12 &&
                   std::abs((*aligned)[8] -
                            std::complex<double>{0.14, -0.07}) < 1e-12,
               "Matrix product aligns complex CIR without phase loss") &&
         Check(output && output->sample_count() == 10U &&
                   std::abs(output->samples()[7] - (*aligned)[7]) < 1e-12 &&
                   std::abs(output->samples()[8] - (*aligned)[8]) < 1e-12,
               "Waveform channel uses the matrix-aligned CIR");
}

}  // namespace

int main() {
  const auto ok = TestDescriptorAndTensorSelection() &&
                  TestCoverageAndFileValidation() &&
                  TestChannel3ReusesWaveformPipeline() &&
                  TestNonIntegerRateAlignmentMatrix();
  return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}
