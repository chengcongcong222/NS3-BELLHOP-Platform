#pragma once

#include <cstddef>
#include <limits>

#include <ns3_factory/contracts/errors.hpp>

#include "waveform_pipeline.hpp"

namespace ns3_factory::phy::internal {

class WaveformStatisticsAccumulator final {
 public:
  [[nodiscard]] auto Observe(std::size_t bit_count,
                             std::size_t bit_error_count)
      -> contracts::Status {
    if(bit_count == 0U || bit_error_count > bit_count) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Packet statistics require a positive bit count "
                           "and no more errors than bits"});
    }
    if(packet_count_ == std::numeric_limits<std::size_t>::max() ||
       total_bit_count_ >
           std::numeric_limits<std::size_t>::max() - bit_count ||
       total_bit_error_count_ >
           std::numeric_limits<std::size_t>::max() - bit_error_count) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Waveform statistics counters overflow"});
    }

    ++packet_count_;
    total_bit_count_ += bit_count;
    total_bit_error_count_ += bit_error_count;
    if(bit_error_count != 0U) {
      ++packet_error_count_;
    }
    return {};
  }

  [[nodiscard]] auto Observe(const WaveformPipelineResult& result)
      -> contracts::Status {
    return Observe(result.recovered().bit_count(),
                   result.bit_error_count());
  }

  [[nodiscard]] constexpr auto packet_count() const noexcept
      -> std::size_t {
    return packet_count_;
  }

  [[nodiscard]] constexpr auto packet_error_count() const noexcept
      -> std::size_t {
    return packet_error_count_;
  }

  [[nodiscard]] constexpr auto total_bit_count() const noexcept
      -> std::size_t {
    return total_bit_count_;
  }

  [[nodiscard]] constexpr auto total_bit_error_count() const noexcept
      -> std::size_t {
    return total_bit_error_count_;
  }

  [[nodiscard]] auto bit_error_rate() const noexcept -> double {
    if(total_bit_count_ == 0U) {
      return 0.0;
    }
    return static_cast<double>(total_bit_error_count_) /
           static_cast<double>(total_bit_count_);
  }

  [[nodiscard]] auto packet_error_rate() const noexcept -> double {
    if(packet_count_ == 0U) {
      return 0.0;
    }
    return static_cast<double>(packet_error_count_) /
           static_cast<double>(packet_count_);
  }

 private:
  std::size_t packet_count_ = 0U;
  std::size_t packet_error_count_ = 0U;
  std::size_t total_bit_count_ = 0U;
  std::size_t total_bit_error_count_ = 0U;
};

}  // namespace ns3_factory::phy::internal
