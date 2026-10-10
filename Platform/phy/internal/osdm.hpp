#pragma once

#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

#include "fft.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

struct OsdmConfig final {
  std::size_t block_length_symbols{256U};
  std::size_t vector_length_symbols{16U};
  std::size_t cyclic_prefix_samples{64U};

  auto operator==(const OsdmConfig&) const -> bool = default;
};

[[nodiscard]] inline auto ValidateOsdmConfig(const OsdmConfig& config)
    -> contracts::Status {
  if(config.block_length_symbols == 0U ||
     config.vector_length_symbols == 0U ||
     config.block_length_symbols % config.vector_length_symbols != 0U ||
     config.cyclic_prefix_samples == 0U ||
     config.cyclic_prefix_samples > config.block_length_symbols) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM requires K=M*N and a CP length in [1,K]"});
  }
  const auto transform_length =
      config.block_length_symbols / config.vector_length_symbols;
  if(!std::has_single_bit(transform_length)) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM N=K/M must be a power of two"});
  }
  return {};
}

[[nodiscard]] inline auto OsdmModulatedSampleCount(
    std::size_t bit_count,
    const OsdmConfig& config) -> Result<std::size_t> {
  auto valid = ValidateOsdmConfig(config);
  if(!valid) return std::unexpected(valid.error());
  if(bit_count == 0U) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM requires at least one input bit"});
  }

  const auto block_count =
      1U + (bit_count - 1U) / config.block_length_symbols;
  if(config.cyclic_prefix_samples >
     std::numeric_limits<std::size_t>::max() -
         config.block_length_symbols) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "OSDM block plus CP length overflows"});
  }
  const auto samples_per_block =
      config.block_length_symbols + config.cyclic_prefix_samples;
  if(block_count >
     std::numeric_limits<std::size_t>::max() / samples_per_block) {
    return std::unexpected(
        Error{ErrorCode::kOverflow, "OSDM waveform length overflows"});
  }
  return block_count * samples_per_block;
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

[[nodiscard]] inline auto ModulateOsdmBpsk(
    std::span<const std::uint8_t> bits,
    double sample_rate_hz,
    double symbol_amplitude_upa,
    const OsdmConfig& config) -> Result<WaveformBuffer> {
  if(bits.empty() || !std::isfinite(sample_rate_hz) ||
     sample_rate_hz <= 0.0 || !std::isfinite(symbol_amplitude_upa) ||
     symbol_amplitude_upa <= 0.0) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "OSDM BPSK modulation parameters are invalid"});
  }
  for(const auto bit : bits) {
    if(bit > 1U) {
      return std::unexpected(
          Error{ErrorCode::kInvalidArgument,
                "OSDM BPSK input values must be binary"});
    }
  }

  const auto sample_count = OsdmModulatedSampleCount(bits.size(), config);
  if(!sample_count) return std::unexpected(sample_count.error());
  const auto block_count =
      1U + (bits.size() - 1U) / config.block_length_symbols;
  std::vector<WaveformBuffer::Sample> waveform;
  waveform.reserve(*sample_count);

  for(std::size_t block_index = 0U;
      block_index < block_count; ++block_index) {
    std::vector<std::complex<double>> symbols(
        config.block_length_symbols,
        {-symbol_amplitude_upa, 0.0});
    for(std::size_t symbol_index = 0U;
        symbol_index < config.block_length_symbols; ++symbol_index) {
      const auto bit_index =
          block_index * config.block_length_symbols + symbol_index;
      if(bit_index >= bits.size()) break;
      symbols[symbol_index] =
          bits[bit_index] == 0U
              ? std::complex<double>{-symbol_amplitude_upa, 0.0}
              : std::complex<double>{symbol_amplitude_upa, 0.0};
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

[[nodiscard]] inline auto DemodulateOsdmBpsk(
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
  const auto block_count =
      1U + (expected_bit_count - 1U) / config.block_length_symbols;
  std::vector<std::uint8_t> bits;
  bits.reserve(expected_bit_count);

  for(std::size_t block_index = 0U;
      block_index < block_count; ++block_index) {
    const auto payload_offset =
        block_index * samples_per_block + config.cyclic_prefix_samples;
    const auto block_samples =
        waveform.samples().subspan(payload_offset,
                                   config.block_length_symbols);
    auto symbols = OsdmInverseBlock(block_samples, config);
    if(!symbols) return std::unexpected(symbols.error());
    for(const auto symbol : *symbols) {
      if(bits.size() == expected_bit_count) break;
      bits.push_back(symbol.real() >= 0.0 ? 1U : 0U);
    }
  }
  return bits;
}

}  // namespace ns3_factory::phy::internal
