#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "fft.hpp"
#include "osdm.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

// Physical receive failures are not configuration/Runtime failures. Keep this
// typed indication internal; do not classify errors by their message text.
enum class OsdmReceptionFailure { kNone, kSynchronization, kTruncatedFrame, kNoEnergy };

struct OsdmChannelEstimate final {
  std::size_t training_body_start_sample;
  double normalized_correlation;
  std::vector<std::complex<double>> frequency_response;
};

[[nodiscard]] inline auto SynchronizeAndEstimateOsdmChannel(
    const WaveformBuffer& received,
    double training_amplitude_upa,
    const OsdmConfig& config,
    OsdmReceptionFailure* failure = nullptr) -> Result<OsdmChannelEstimate> {
  if(failure) *failure = OsdmReceptionFailure::kNone;
  if(!config.training_preamble_enabled) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "OSDM training synchronization requires a training preamble"});
  }
  auto training = BuildOsdmTrainingSequence(config, training_amplitude_upa);
  if(!training) return std::unexpected(training.error());
  const auto length = config.block_length_symbols;
  if(received.sample_count() < length) {
    if(failure) *failure = OsdmReceptionFailure::kTruncatedFrame;
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Received OSDM waveform is shorter than the training sequence"});
  }

  const auto maximum_candidate =
      std::min(config.maximum_sync_search_samples,
               received.sample_count() - length);
  long double training_energy = 0.0L;
  for(const auto sample : *training) {
    training_energy += static_cast<long double>(std::norm(sample));
  }

  std::vector<double> correlations(maximum_candidate + 1U);
  auto best_correlation = 0.0;
  for(std::size_t candidate = 0U; candidate <= maximum_candidate;
      ++candidate) {
    std::complex<long double> correlation{};
    long double received_energy = 0.0L;
    for(std::size_t index = 0U; index < length; ++index) {
      const auto sample = received.samples()[candidate + index];
      correlation +=
          std::conj(std::complex<long double>{
              (*training)[index].real(), (*training)[index].imag()}) *
          std::complex<long double>{sample.real(), sample.imag()};
      received_energy += static_cast<long double>(std::norm(sample));
    }
    const auto denominator = std::sqrt(training_energy * received_energy);
    const auto normalized =
        denominator > 0.0L
            ? static_cast<double>(std::abs(correlation) / denominator)
            : 0.0;
    correlations[candidate] = normalized;
    best_correlation = std::max(best_correlation, normalized);
  }
  if(!std::isfinite(best_correlation) ||
     best_correlation < config.minimum_sync_correlation) {
    if(failure) *failure = OsdmReceptionFailure::kSynchronization;
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "OSDM training preamble was not detected"});
  }

  // A CAZAC training sequence produces one correlation peak per channel path.
  // Select the earliest significant peak so frame timing follows the first
  // arrival rather than whichever later path happens to be strongest.
  const auto relative_threshold =
      best_correlation * config.minimum_sync_correlation;
  auto training_start = maximum_candidate;
  for(std::size_t candidate = 0U; candidate <= maximum_candidate;
      ++candidate) {
    if(correlations[candidate] >= relative_threshold) {
      training_start = candidate;
      break;
    }
  }

  std::vector<std::complex<double>> received_training(length);
  std::copy_n(received.samples().begin() +
                  static_cast<std::ptrdiff_t>(training_start),
              length, received_training.begin());
  auto training_spectrum = *training;
  auto transformed = TransformRadix2(received_training, false);
  if(!transformed) return std::unexpected(transformed.error());
  transformed = TransformRadix2(training_spectrum, false);
  if(!transformed) return std::unexpected(transformed.error());

  auto maximum_training_power = 0.0;
  for(const auto value : training_spectrum) {
    maximum_training_power =
        std::max(maximum_training_power, std::norm(value));
  }
  const auto spectral_floor =
      std::max(maximum_training_power *
                   std::numeric_limits<double>::epsilon(),
               std::numeric_limits<double>::min());
  std::vector<std::complex<double>> response(length);
  for(std::size_t index = 0U; index < length; ++index) {
    const auto denominator = std::norm(training_spectrum[index]);
    if(!std::isfinite(denominator) || denominator <= spectral_floor) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "OSDM training sequence has an unusable spectrum"});
    }
    response[index] =
        received_training[index] / training_spectrum[index];
  }
  return OsdmChannelEstimate{training_start,
                             correlations[training_start],
                             std::move(response)};
}

[[nodiscard]] inline auto EqualizeOsdmWithTraining(
    const WaveformBuffer& received,
    std::size_t expected_bit_count,
    double transmitted_symbol_amplitude_upa,
    double noise_power_upa2,
    const OsdmConfig& config,
    OsdmReceptionFailure* failure = nullptr) -> Result<WaveformBuffer> {
  if(failure) *failure = OsdmReceptionFailure::kNone;
  if(expected_bit_count == 0U ||
     !std::isfinite(transmitted_symbol_amplitude_upa) ||
     transmitted_symbol_amplitude_upa <= 0.0 ||
     !std::isfinite(noise_power_upa2) || noise_power_upa2 < 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Training-based OSDM equalization parameters are invalid"});
  }
  const auto expected_samples =
      OsdmModulatedSampleCount(expected_bit_count, config);
  if(!expected_samples) return std::unexpected(expected_samples.error());
  const auto data_block_count =
      OsdmDataBlockCount(expected_bit_count, config);
  if(!data_block_count) return std::unexpected(data_block_count.error());
  auto estimate = SynchronizeAndEstimateOsdmChannel(
      received, transmitted_symbol_amplitude_upa, config, failure);
  if(!estimate) return std::unexpected(estimate.error());

  const auto length = config.block_length_symbols;
  const auto samples_per_block = length + config.cyclic_prefix_samples;
  const auto first_data_body =
      estimate->training_body_start_sample + length +
      config.cyclic_prefix_samples;
  if(first_data_body > received.sample_count()) {
    if(failure) *failure = OsdmReceptionFailure::kTruncatedFrame;
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Synchronized OSDM frame has no data block"});
  }

  auto maximum_channel_power = 0.0;
  for(const auto value : estimate->frequency_response) {
    maximum_channel_power =
        std::max(maximum_channel_power, std::norm(value));
  }
  if(!std::isfinite(maximum_channel_power) ||
     maximum_channel_power <= 0.0) {
    if(failure) *failure = OsdmReceptionFailure::kNoEnergy;
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Estimated OSDM channel has no recoverable energy"});
  }
  const auto signal_power =
      transmitted_symbol_amplitude_upa * transmitted_symbol_amplitude_upa;
  const auto numerical_floor =
      std::max(maximum_channel_power *
                   std::numeric_limits<double>::epsilon(),
               std::numeric_limits<double>::min());
  const auto regularization =
      std::max(noise_power_upa2 / signal_power, numerical_floor);

  std::vector<std::complex<double>> equalized_frame(*expected_samples);
  for(std::size_t block_index = 0U; block_index < *data_block_count;
      ++block_index) {
    const auto received_body =
        first_data_body + block_index * samples_per_block;
    if(received_body > received.sample_count() ||
       received.sample_count() - received_body < length) {
      if(failure) *failure = OsdmReceptionFailure::kTruncatedFrame;
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "Received OSDM waveform is shorter than its synchronized frame"});
    }
    std::vector<std::complex<double>> spectrum(length);
    std::copy_n(received.samples().begin() +
                    static_cast<std::ptrdiff_t>(received_body),
                length, spectrum.begin());
    auto transformed = TransformRadix2(spectrum, false);
    if(!transformed) return std::unexpected(transformed.error());
    for(std::size_t index = 0U; index < length; ++index) {
      const auto channel = estimate->frequency_response[index];
      spectrum[index] =
          std::conj(channel) * spectrum[index] /
          (std::norm(channel) + regularization);
    }
    transformed = TransformRadix2(spectrum, true);
    if(!transformed) return std::unexpected(transformed.error());

    const auto framed_body =
        samples_per_block + config.cyclic_prefix_samples +
        block_index * samples_per_block;
    std::copy(spectrum.begin(), spectrum.end(),
              equalized_frame.begin() +
                  static_cast<std::ptrdiff_t>(framed_body));
  }
  return WaveformBuffer::Create(received.sample_rate_hz(),
                                std::move(equalized_frame));
}

}  // namespace ns3_factory::phy::internal
