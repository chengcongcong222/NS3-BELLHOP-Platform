#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::phy::internal {

using contracts::Error;
using contracts::ErrorCode;
using contracts::Result;

class WaveformBuffer final {
 public:
  using Sample = std::complex<double>;

  [[nodiscard]] static auto Create(double sample_rate_hz,
                                   std::vector<Sample> samples)
      -> Result<WaveformBuffer> {
    if(!std::isfinite(sample_rate_hz) || sample_rate_hz <= 0.0) {
      return std::unexpected(
          Error{ErrorCode::kOutOfRange,
                "Waveform sample rate must be finite and positive"});
    }
    if(samples.empty()) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "Waveform must contain at least one sample"});
    }
    for(const auto sample : samples) {
      if(!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
        return std::unexpected(
            Error{ErrorCode::kInvalidArgument,
                  "Waveform samples must be finite"});
      }
    }
    return WaveformBuffer{sample_rate_hz, std::move(samples)};
  }

  [[nodiscard]] constexpr auto sample_rate_hz() const noexcept -> double {
    return sample_rate_hz_;
  }

  [[nodiscard]] auto samples() const noexcept -> std::span<const Sample> {
    return samples_;
  }

  [[nodiscard]] auto sample_count() const noexcept -> std::size_t {
    return samples_.size();
  }

  auto operator==(const WaveformBuffer&) const -> bool = default;

 private:
  WaveformBuffer(double sample_rate_hz, std::vector<Sample> samples)
      : sample_rate_hz_(sample_rate_hz), samples_(std::move(samples)) {}

  double sample_rate_hz_;
  std::vector<Sample> samples_;
};

[[nodiscard]] inline auto AddWaveforms(const WaveformBuffer& lhs,
                                       const WaveformBuffer& rhs)
    -> Result<WaveformBuffer> {
  if(lhs.sample_rate_hz() != rhs.sample_rate_hz() ||
     lhs.sample_count() != rhs.sample_count()) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Waveforms must have identical sample rate and length"});
  }

  std::vector<WaveformBuffer::Sample> samples(lhs.sample_count());
  for(std::size_t index = 0; index < samples.size(); ++index) {
    samples[index] = lhs.samples()[index] + rhs.samples()[index];
  }
  return WaveformBuffer::Create(lhs.sample_rate_hz(), std::move(samples));
}

[[nodiscard]] inline auto RotatePhase(const WaveformBuffer& waveform,
                                      double phase_radians)
    -> Result<WaveformBuffer> {
  if(!std::isfinite(phase_radians)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Waveform phase rotation must be finite"});
  }

  const auto rotation = std::polar(1.0, phase_radians);
  std::vector<WaveformBuffer::Sample> samples;
  samples.reserve(waveform.sample_count());
  for(const auto sample : waveform.samples()) {
    samples.push_back(sample * rotation);
  }
  return WaveformBuffer::Create(waveform.sample_rate_hz(),
                                std::move(samples));
}

}  // namespace ns3_factory::phy::internal
