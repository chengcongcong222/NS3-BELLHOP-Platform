#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <numbers>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "waveform.hpp"

namespace ns3_factory::phy::internal {

enum class ModulationScheme : std::uint8_t {
  kBpsk = 1,
  kBfsk = 2,
  kQpsk = 3,
};

struct ModulationConfig final {
  ModulationScheme scheme;
  double sample_rate_hz;
  double symbol_rate_baud;
  double symbol_amplitude_upa;
  double acoustic_carrier_hz;
  double bfsk_frequency_deviation_hz;

  auto operator==(const ModulationConfig&) const -> bool = default;
};

class BitFrame final {
 public:
  [[nodiscard]] static auto Create(std::vector<std::uint8_t> bits)
      -> Result<BitFrame> {
    if(bits.empty()) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "BitFrame must contain at least one bit"});
    }
    for(const auto bit : bits) {
      if(bit > 1U) {
        return std::unexpected(
            Error{ErrorCode::kInvalidArgument,
                  "BitFrame values must be binary"});
      }
    }
    return BitFrame{std::move(bits)};
  }

  [[nodiscard]] auto bits() const noexcept
      -> std::span<const std::uint8_t> {
    return bits_;
  }

  [[nodiscard]] auto bit_count() const noexcept -> std::size_t {
    return bits_.size();
  }

  auto operator==(const BitFrame&) const -> bool = default;

 private:
  explicit BitFrame(std::vector<std::uint8_t> bits)
      : bits_(std::move(bits)) {}

  std::vector<std::uint8_t> bits_;
};

[[nodiscard]] inline auto SamplesPerSymbol(const ModulationConfig& config)
    -> Result<std::size_t> {
  if(!std::isfinite(config.sample_rate_hz) ||
     !std::isfinite(config.symbol_rate_baud) ||
     !std::isfinite(config.symbol_amplitude_upa) ||
     !std::isfinite(config.acoustic_carrier_hz) ||
     !std::isfinite(config.bfsk_frequency_deviation_hz)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Modulation configuration values must be finite"});
  }
  if(config.sample_rate_hz <= 0.0 || config.symbol_rate_baud <= 0.0 ||
     config.symbol_amplitude_upa <= 0.0 ||
     config.acoustic_carrier_hz <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "Modulation rates, amplitude, and carrier must be positive"});
  }

  const auto ratio = config.sample_rate_hz / config.symbol_rate_baud;
  const auto rounded = std::round(ratio);
  if(rounded < 2.0 ||
     std::abs(ratio - rounded) >
         1.0e-9 * std::max(1.0, std::abs(ratio)) ||
     rounded > static_cast<double>(
                   std::numeric_limits<std::size_t>::max())) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Sample rate must provide an integer number of at least two "
              "samples per symbol"});
  }

  if(config.scheme == ModulationScheme::kBfsk &&
     (config.bfsk_frequency_deviation_hz <= 0.0 ||
      config.bfsk_frequency_deviation_hz >= config.sample_rate_hz / 2.0 ||
      config.acoustic_carrier_hz <=
          config.bfsk_frequency_deviation_hz)) {
    return std::unexpected(
        Error{ErrorCode::kOutOfRange,
              "BFSK frequency deviation is outside the valid band"});
  }

  return static_cast<std::size_t>(rounded);
}

class IModulator {
 public:
  virtual ~IModulator() = default;

  [[nodiscard]] virtual auto Modulate(
      const BitFrame& frame,
      const ModulationConfig& config) const -> Result<WaveformBuffer> = 0;
};

class IDemodulator {
 public:
  virtual ~IDemodulator() = default;

  [[nodiscard]] virtual auto Demodulate(
      const WaveformBuffer& waveform,
      std::size_t expected_bit_count,
      const ModulationConfig& config) const -> Result<BitFrame> = 0;
};

class BpskModulator final : public IModulator {
 public:
  [[nodiscard]] auto Modulate(
      const BitFrame& frame,
      const ModulationConfig& config) const
      -> Result<WaveformBuffer> override {
    if(config.scheme != ModulationScheme::kBpsk) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BPSK modulator received a non-BPSK configuration"});
    }
    const auto samples_per_symbol = SamplesPerSymbol(config);
    if(!samples_per_symbol) {
      return std::unexpected(samples_per_symbol.error());
    }

    std::vector<WaveformBuffer::Sample> samples;
    samples.reserve(frame.bit_count() * *samples_per_symbol);
    for(const auto bit : frame.bits()) {
      const auto symbol = bit == 0U ? -config.symbol_amplitude_upa
                                    : config.symbol_amplitude_upa;
      for(std::size_t index = 0; index < *samples_per_symbol; ++index) {
        samples.emplace_back(symbol, 0.0);
      }
    }
    return WaveformBuffer::Create(config.sample_rate_hz,
                                  std::move(samples));
  }
};

class BpskDemodulator final : public IDemodulator {
 public:
  [[nodiscard]] auto Demodulate(
      const WaveformBuffer& waveform,
      std::size_t expected_bit_count,
      const ModulationConfig& config) const
      -> Result<BitFrame> override {
    if(config.scheme != ModulationScheme::kBpsk) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BPSK demodulator received a non-BPSK configuration"});
    }
    const auto samples_per_symbol = SamplesPerSymbol(config);
    if(!samples_per_symbol) {
      return std::unexpected(samples_per_symbol.error());
    }
    if(waveform.sample_rate_hz() != config.sample_rate_hz ||
       expected_bit_count == 0U ||
       waveform.sample_count() <
           expected_bit_count * *samples_per_symbol) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BPSK waveform does not match the decode configuration"});
    }

    std::vector<std::uint8_t> bits;
    bits.reserve(expected_bit_count);
    for(std::size_t bit_index = 0; bit_index < expected_bit_count;
        ++bit_index) {
      double matched_sum = 0.0;
      const auto first_sample = bit_index * *samples_per_symbol;
      for(std::size_t sample_index = 0;
          sample_index < *samples_per_symbol;
          ++sample_index) {
        matched_sum +=
            waveform.samples()[first_sample + sample_index].real();
      }
      bits.push_back(matched_sum >= 0.0 ? 1U : 0U);
    }
    return BitFrame::Create(std::move(bits));
  }
};

class BfskModulator final : public IModulator {
 public:
  [[nodiscard]] auto Modulate(
      const BitFrame& frame,
      const ModulationConfig& config) const
      -> Result<WaveformBuffer> override {
    if(config.scheme != ModulationScheme::kBfsk) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BFSK modulator received a non-BFSK configuration"});
    }
    const auto samples_per_symbol = SamplesPerSymbol(config);
    if(!samples_per_symbol) {
      return std::unexpected(samples_per_symbol.error());
    }

    constexpr auto kTwoPi = 2.0 * std::numbers::pi_v<double>;
    std::vector<WaveformBuffer::Sample> samples;
    samples.reserve(frame.bit_count() * *samples_per_symbol);
    double phase = 0.0;
    for(const auto bit : frame.bits()) {
      const auto frequency =
          bit == 0U ? -config.bfsk_frequency_deviation_hz
                    : config.bfsk_frequency_deviation_hz;
      const auto phase_step =
          kTwoPi * frequency / config.sample_rate_hz;
      for(std::size_t index = 0; index < *samples_per_symbol; ++index) {
        samples.push_back(
            std::polar(config.symbol_amplitude_upa, phase));
        phase = std::remainder(phase + phase_step, kTwoPi);
      }
    }
    return WaveformBuffer::Create(config.sample_rate_hz,
                                  std::move(samples));
  }
};

class BfskDemodulator final : public IDemodulator {
 public:
  [[nodiscard]] auto Demodulate(
      const WaveformBuffer& waveform,
      std::size_t expected_bit_count,
      const ModulationConfig& config) const
      -> Result<BitFrame> override {
    if(config.scheme != ModulationScheme::kBfsk) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BFSK demodulator received a non-BFSK configuration"});
    }
    const auto samples_per_symbol = SamplesPerSymbol(config);
    if(!samples_per_symbol) {
      return std::unexpected(samples_per_symbol.error());
    }
    if(waveform.sample_rate_hz() != config.sample_rate_hz ||
       expected_bit_count == 0U ||
       waveform.sample_count() <
           expected_bit_count * *samples_per_symbol) {
      return std::unexpected(
          Error{ErrorCode::kFailedPrecondition,
                "BFSK waveform does not match the decode configuration"});
    }

    constexpr auto kTwoPi = 2.0 * std::numbers::pi_v<double>;
    std::vector<std::uint8_t> bits;
    bits.reserve(expected_bit_count);
    for(std::size_t bit_index = 0; bit_index < expected_bit_count;
        ++bit_index) {
      std::complex<double> zero_correlation{};
      std::complex<double> one_correlation{};
      const auto first_sample = bit_index * *samples_per_symbol;
      for(std::size_t sample_index = 0;
          sample_index < *samples_per_symbol;
          ++sample_index) {
        const auto sample =
            waveform.samples()[first_sample + sample_index];
        const auto time =
            static_cast<double>(sample_index) / config.sample_rate_hz;
        zero_correlation += sample * std::polar(
            1.0,
            kTwoPi * config.bfsk_frequency_deviation_hz * time);
        one_correlation += sample * std::polar(
            1.0,
            -kTwoPi * config.bfsk_frequency_deviation_hz * time);
      }
      bits.push_back(std::norm(one_correlation) >=
                             std::norm(zero_correlation)
                         ? 1U
                         : 0U);
    }
    return BitFrame::Create(std::move(bits));
  }
};

[[nodiscard]] inline auto CreateModulator(ModulationScheme scheme)
    -> Result<std::unique_ptr<IModulator>> {
  switch(scheme) {
    case ModulationScheme::kBpsk: {
      std::unique_ptr<IModulator> result =
          std::make_unique<BpskModulator>();
      return result;
    }
    case ModulationScheme::kBfsk: {
      std::unique_ptr<IModulator> result =
          std::make_unique<BfskModulator>();
      return result;
    }
    case ModulationScheme::kQpsk:
      return std::unexpected(
          Error{ErrorCode::kUnsupported,
                "QPSK is reserved but not implemented"});
  }
  return std::unexpected(
      Error{ErrorCode::kInvalidArgument,
            "Unknown modulation scheme"});
}

[[nodiscard]] inline auto CreateDemodulator(ModulationScheme scheme)
    -> Result<std::unique_ptr<IDemodulator>> {
  switch(scheme) {
    case ModulationScheme::kBpsk: {
      std::unique_ptr<IDemodulator> result =
          std::make_unique<BpskDemodulator>();
      return result;
    }
    case ModulationScheme::kBfsk: {
      std::unique_ptr<IDemodulator> result =
          std::make_unique<BfskDemodulator>();
      return result;
    }
    case ModulationScheme::kQpsk:
      return std::unexpected(
          Error{ErrorCode::kUnsupported,
                "QPSK is reserved but not implemented"});
  }
  return std::unexpected(
      Error{ErrorCode::kInvalidArgument,
            "Unknown modulation scheme"});
}

}  // namespace ns3_factory::phy::internal
