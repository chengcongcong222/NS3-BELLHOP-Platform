#pragma once

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "fft.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

enum class OsdmConstellation : std::uint8_t {
  kBpsk = 1,
  kQpsk = 2,
};

struct OsdmConfig final {
  std::size_t block_length_symbols{256U};
  std::size_t vector_length_symbols{16U};
  std::size_t cyclic_prefix_samples{64U};
  OsdmConstellation constellation{OsdmConstellation::kBpsk};
  bool training_preamble_enabled{false};
  double minimum_sync_correlation{0.05};
  std::size_t maximum_sync_search_samples{128U};

  auto operator==(const OsdmConfig&) const -> bool = default;
};

[[nodiscard]] inline auto ValidateOsdmConfig(const OsdmConfig& config)
    -> contracts::Status {
  if(config.block_length_symbols == 0U ||
     config.vector_length_symbols == 0U ||
     config.block_length_symbols % config.vector_length_symbols != 0U ||
     config.cyclic_prefix_samples == 0U ||
     config.cyclic_prefix_samples > config.block_length_symbols ||
     (config.constellation != OsdmConstellation::kBpsk &&
      config.constellation != OsdmConstellation::kQpsk) ||
     !std::isfinite(config.minimum_sync_correlation) ||
     config.minimum_sync_correlation <= 0.0 ||
     config.minimum_sync_correlation > 1.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM framing, constellation, or synchronization is invalid"});
  }
  const auto transform_length =
      config.block_length_symbols / config.vector_length_symbols;
  if(!std::has_single_bit(transform_length)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM N=K/M must be a power of two"});
  }
  if(config.training_preamble_enabled &&
     (!std::has_single_bit(config.block_length_symbols) ||
      config.maximum_sync_search_samples <
          config.cyclic_prefix_samples)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "Training-based OSDM requires power-of-two K and a "
              "search window covering its CP"});
  }
  return {};
}

[[nodiscard]] inline auto BitsPerOsdmSymbol(
    const OsdmConfig& config) -> Result<std::size_t> {
  auto valid = ValidateOsdmConfig(config);
  if(!valid) return std::unexpected(valid.error());
  return config.constellation == OsdmConstellation::kQpsk ? 2U : 1U;
}

[[nodiscard]] inline auto OsdmDataSymbolCount(
    std::size_t bit_count,
    const OsdmConfig& config) -> Result<std::size_t> {
  if(bit_count == 0U) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM requires at least one input bit"});
  }
  const auto bits_per_symbol = BitsPerOsdmSymbol(config);
  if(!bits_per_symbol) return std::unexpected(bits_per_symbol.error());
  return 1U + (bit_count - 1U) / *bits_per_symbol;
}

[[nodiscard]] inline auto OsdmDataBlockCount(
    std::size_t bit_count,
    const OsdmConfig& config) -> Result<std::size_t> {
  const auto symbol_count = OsdmDataSymbolCount(bit_count, config);
  if(!symbol_count) return std::unexpected(symbol_count.error());
  return 1U + (*symbol_count - 1U) / config.block_length_symbols;
}

[[nodiscard]] inline auto BuildOsdmTrainingSequence(
    const OsdmConfig& config,
    double amplitude) -> Result<std::vector<std::complex<double>>> {
  auto valid = ValidateOsdmConfig(config);
  if(!valid) return std::unexpected(valid.error());
  if(!config.training_preamble_enabled || !std::isfinite(amplitude) ||
     amplitude <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM training sequence requires enabled framing and amplitude"});
  }

  const auto length = config.block_length_symbols;
  std::vector<std::complex<double>> sequence;
  sequence.reserve(length);
  constexpr auto pi = std::numbers::pi_v<double>;
  for(std::size_t index = 0U; index < length; ++index) {
    const auto n = static_cast<double>(index);
    const auto phase =
        length % 2U == 0U
            ? -pi * n * n / static_cast<double>(length)
            : -pi * n * (n + 1.0) / static_cast<double>(length);
    sequence.push_back(std::polar(amplitude, phase));
  }
  return sequence;
}

[[nodiscard]] inline auto OsdmModulatedSampleCount(
    std::size_t bit_count,
    const OsdmConfig& config) -> Result<std::size_t> {
  const auto block_count = OsdmDataBlockCount(bit_count, config);
  if(!block_count) return std::unexpected(block_count.error());
  if(config.cyclic_prefix_samples >
     std::numeric_limits<std::size_t>::max() -
         config.block_length_symbols) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "OSDM block plus CP length overflows"});
  }
  const auto samples_per_block =
      config.block_length_symbols + config.cyclic_prefix_samples;
  const auto total_block_count =
      *block_count + (config.training_preamble_enabled ? 1U : 0U);
  if(total_block_count < *block_count ||
     total_block_count >
     std::numeric_limits<std::size_t>::max() / samples_per_block) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "OSDM waveform length overflows"});
  }
  return total_block_count * samples_per_block;
}

// MATLAB-compatible core: reshape d as M-by-N, transpose, apply an N-point
// unitary IDFT column-wise, transpose back, then read out column-major.
// Equivalently, s=(F_N^H tensor I_M)d.
[[nodiscard]] inline auto OsdmForwardBlock(
    std::span<const std::complex<double>> symbols,
    const OsdmConfig& config)
    -> Result<std::vector<std::complex<double>>> {
  auto valid = ValidateOsdmConfig(config);
  if(!valid) return std::unexpected(valid.error());
  if(symbols.size() != config.block_length_symbols) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "OSDM input must contain exactly K symbols"});
  }

  const auto vector_length = config.vector_length_symbols;
  const auto transform_length =
      config.block_length_symbols / vector_length;
  const auto scale = std::sqrt(static_cast<double>(transform_length));
  std::vector<std::complex<double>> output(config.block_length_symbols);

  for(std::size_t vector_index = 0U;
      vector_index < vector_length; ++vector_index) {
    std::vector<std::complex<double>> column(transform_length);
    for(std::size_t transform_index = 0U;
        transform_index < transform_length; ++transform_index) {
      column[transform_index] =
          symbols[vector_index + transform_index * vector_length];
    }
    auto transformed = TransformRadix2(column, true);
    if(!transformed) return std::unexpected(transformed.error());
    for(std::size_t transform_index = 0U;
        transform_index < transform_length; ++transform_index) {
      output[vector_index + transform_index * vector_length] =
          column[transform_index] * scale;
    }
  }
  return output;
}

// MATLAB-compatible inverse: x=(F_N tensor I_M)r. The caller supplies one
// CP-free K-sample block.
[[nodiscard]] inline auto OsdmInverseBlock(
    std::span<const std::complex<double>> samples,
    const OsdmConfig& config)
    -> Result<std::vector<std::complex<double>>> {
  auto valid = ValidateOsdmConfig(config);
  if(!valid) return std::unexpected(valid.error());
  if(samples.size() != config.block_length_symbols) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "OSDM receive block must contain exactly K samples"});
  }

  const auto vector_length = config.vector_length_symbols;
  const auto transform_length =
      config.block_length_symbols / vector_length;
  const auto scale = std::sqrt(static_cast<double>(transform_length));
  std::vector<std::complex<double>> output(config.block_length_symbols);

  for(std::size_t vector_index = 0U;
      vector_index < vector_length; ++vector_index) {
    std::vector<std::complex<double>> column(transform_length);
    for(std::size_t transform_index = 0U;
        transform_index < transform_length; ++transform_index) {
      column[transform_index] =
          samples[vector_index + transform_index * vector_length];
    }
    auto transformed = TransformRadix2(column, false);
    if(!transformed) return std::unexpected(transformed.error());
    for(std::size_t transform_index = 0U;
        transform_index < transform_length; ++transform_index) {
      output[vector_index + transform_index * vector_length] =
          column[transform_index] / scale;
    }
  }
  return output;
}

[[nodiscard]] inline auto MapOsdmConstellationSymbol(
    std::span<const std::uint8_t> bits,
    std::size_t bit_offset,
    double amplitude,
    const OsdmConfig& config) -> Result<std::complex<double>> {
  const auto bits_per_symbol = BitsPerOsdmSymbol(config);
  if(!bits_per_symbol) return std::unexpected(bits_per_symbol.error());
  if(!std::isfinite(amplitude) || amplitude <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM constellation amplitude must be positive"});
  }
  const auto first = bit_offset < bits.size() ? bits[bit_offset] : 0U;
  if(first > 1U) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM constellation input must be binary"});
  }
  if(config.constellation == OsdmConstellation::kBpsk) {
    return std::complex<double>{first == 0U ? -amplitude : amplitude, 0.0};
  }
  const auto second =
      bit_offset + 1U < bits.size() ? bits[bit_offset + 1U] : 0U;
  if(second > 1U) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM constellation input must be binary"});
  }
  const auto component = amplitude / std::sqrt(2.0);
  return std::complex<double>{first == 0U ? -component : component,
                              second == 0U ? -component : component};
}

inline void AppendOsdmConstellationBits(
    std::complex<double> symbol,
    std::size_t expected_bit_count,
    const OsdmConfig& config,
    std::vector<std::uint8_t>& bits) {
  if(bits.size() == expected_bit_count) return;
  bits.push_back(symbol.real() >= 0.0 ? 1U : 0U);
  if(config.constellation == OsdmConstellation::kQpsk &&
     bits.size() < expected_bit_count) {
    bits.push_back(symbol.imag() >= 0.0 ? 1U : 0U);
  }
}

[[nodiscard]] inline auto ModulateOsdm(
    std::span<const std::uint8_t> bits,
    double sample_rate_hz,
    double symbol_amplitude_upa,
    const OsdmConfig& config) -> Result<WaveformBuffer> {
  if(bits.empty() || !std::isfinite(sample_rate_hz) ||
     sample_rate_hz <= 0.0 || !std::isfinite(symbol_amplitude_upa) ||
     symbol_amplitude_upa <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM modulation parameters are invalid"});
  }
  for(const auto bit : bits) {
    if(bit > 1U) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "OSDM input values must be binary"});
    }
  }

  const auto sample_count = OsdmModulatedSampleCount(bits.size(), config);
  if(!sample_count) return std::unexpected(sample_count.error());
  const auto block_count = OsdmDataBlockCount(bits.size(), config);
  if(!block_count) return std::unexpected(block_count.error());
  const auto bits_per_symbol = BitsPerOsdmSymbol(config);
  if(!bits_per_symbol) return std::unexpected(bits_per_symbol.error());
  std::vector<WaveformBuffer::Sample> waveform;
  waveform.reserve(*sample_count);

  if(config.training_preamble_enabled) {
    auto training = BuildOsdmTrainingSequence(config, symbol_amplitude_upa);
    if(!training) return std::unexpected(training.error());
    waveform.insert(
        waveform.end(),
        training->end() -
            static_cast<std::ptrdiff_t>(config.cyclic_prefix_samples),
        training->end());
    waveform.insert(waveform.end(), training->begin(), training->end());
  }

  for(std::size_t block_index = 0U;
      block_index < *block_count; ++block_index) {
    std::vector<std::complex<double>> symbols;
    symbols.reserve(config.block_length_symbols);
    for(std::size_t symbol_index = 0U;
        symbol_index < config.block_length_symbols; ++symbol_index) {
      const auto bit_offset =
          (block_index * config.block_length_symbols + symbol_index) *
          *bits_per_symbol;
      auto symbol = MapOsdmConstellationSymbol(
          bits, bit_offset, symbol_amplitude_upa, config);
      if(!symbol) return std::unexpected(symbol.error());
      symbols.push_back(*symbol);
    }

    auto block = OsdmForwardBlock(symbols, config);
    if(!block) return std::unexpected(block.error());
    waveform.insert(
        waveform.end(),
        block->end() -
            static_cast<std::ptrdiff_t>(config.cyclic_prefix_samples),
        block->end());
    waveform.insert(waveform.end(), block->begin(), block->end());
  }
  return WaveformBuffer::Create(sample_rate_hz, std::move(waveform));
}

[[nodiscard]] inline auto DemodulateOsdm(
    const WaveformBuffer& waveform,
    std::size_t expected_bit_count,
    double expected_sample_rate_hz,
    const OsdmConfig& config) -> Result<std::vector<std::uint8_t>> {
  const auto expected_samples =
      OsdmModulatedSampleCount(expected_bit_count, config);
  if(!expected_samples) return std::unexpected(expected_samples.error());
  if(waveform.sample_rate_hz() != expected_sample_rate_hz ||
     waveform.sample_count() < *expected_samples) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "OSDM waveform does not match the decode configuration"});
  }

  const auto samples_per_block =
      config.block_length_symbols + config.cyclic_prefix_samples;
  const auto block_count = OsdmDataBlockCount(expected_bit_count, config);
  if(!block_count) return std::unexpected(block_count.error());
  const auto data_offset =
      config.training_preamble_enabled ? samples_per_block : 0U;
  std::vector<std::uint8_t> bits;
  bits.reserve(expected_bit_count);

  for(std::size_t block_index = 0U;
      block_index < *block_count; ++block_index) {
    const auto payload_offset =
        data_offset + block_index * samples_per_block +
        config.cyclic_prefix_samples;
    const auto block_samples =
        waveform.samples().subspan(payload_offset,
                                   config.block_length_symbols);
    auto symbols = OsdmInverseBlock(block_samples, config);
    if(!symbols) return std::unexpected(symbols.error());
    for(const auto symbol : *symbols) {
      if(bits.size() == expected_bit_count) break;
      AppendOsdmConstellationBits(
          symbol, expected_bit_count, config, bits);
    }
  }
  return bits;
}

}  // namespace ns3_factory::phy::internal
