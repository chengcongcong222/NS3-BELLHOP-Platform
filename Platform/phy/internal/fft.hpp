#pragma once

#include <algorithm>
#include <bit>
#include <cmath>
#include <complex>
#include <cstddef>
#include <numbers>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::phy::internal {

[[nodiscard]] inline auto TransformRadix2(
    std::vector<std::complex<double>>& values,
    bool inverse) -> contracts::Status {
  const auto size = values.size();
  if(size == 0U || !std::has_single_bit(size)) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "FFT length must be a non-zero power of two"});
  }

  for(std::size_t index = 1U, reverse = 0U; index < size; ++index) {
    auto bit = size >> 1U;
    for(; (reverse & bit) != 0U; bit >>= 1U) reverse ^= bit;
    reverse ^= bit;
    if(index < reverse) std::swap(values[index], values[reverse]);
  }

  for(std::size_t length = 2U; length <= size; length <<= 1U) {
    const auto angle = (inverse ? 2.0 : -2.0) *
                       std::numbers::pi_v<double> /
                       static_cast<double>(length);
    const auto step = std::polar(1.0, angle);
    for(std::size_t begin = 0U; begin < size; begin += length) {
      auto rotation = std::complex<double>{1.0, 0.0};
      for(std::size_t offset = 0U; offset < length / 2U; ++offset) {
        const auto even = values[begin + offset];
        const auto odd =
            values[begin + offset + length / 2U] * rotation;
        values[begin + offset] = even + odd;
        values[begin + offset + length / 2U] = even - odd;
        rotation *= step;
      }
    }
    if(length == size) break;
  }

  if(inverse) {
    for(auto& value : values) value /= static_cast<double>(size);
  }
  return {};
}

}  // namespace ns3_factory::phy::internal
