#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

namespace ns3_factory::phy::internal {

struct RateBasedTxPhyConfig final {
  std::uint64_t bits_per_second;
  double center_frequency_hz;
  double occupied_bandwidth_hz;
  double source_level_db_re_1upa_at_1m;

  auto operator==(const RateBasedTxPhyConfig&) const -> bool = default;
};

// P0 acceptance airtime counts DigitalPacket payload bytes only. The exact
// rational duration is rounded upward to the next integer nanosecond so a
// planned slot never understates the configured-rate airtime.
[[nodiscard]] inline auto ComputePayloadAirtime(
    std::size_t payload_bytes,
    std::uint64_t bits_per_second)
    -> contracts::Result<contracts::SimDuration> {
  if(bits_per_second == 0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "Payload airtime requires a positive bit rate"});
  }
  constexpr auto kBitsPerByte = std::uint64_t{8};
  constexpr auto kNanosecondsPerSecond = std::uint64_t{1'000'000'000};
  constexpr auto kMaximumNanoseconds =
      std::numeric_limits<contracts::NanosecondCount>::max();
  if(payload_bytes == 0 ||
     payload_bytes >
         std::numeric_limits<std::uint64_t>::max() / kBitsPerByte) {
    return std::unexpected(
        contracts::Error{payload_bytes == 0
                             ? contracts::ErrorCode::kInvalidArgument
                             : contracts::ErrorCode::kOverflow,
                         "Payload airtime requires a non-empty, bounded "
                         "payload"});
  }

  const auto airtime_bits =
      static_cast<std::uint64_t>(payload_bytes) * kBitsPerByte;
  const auto whole_seconds = airtime_bits / bits_per_second;
  if(whole_seconds >
     static_cast<std::uint64_t>(kMaximumNanoseconds) /
         kNanosecondsPerSecond) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Payload airtime exceeds SimDuration range"});
  }

  const auto remainder_bits = airtime_bits % bits_per_second;
  if(remainder_bits != 0 &&
     remainder_bits >
         std::numeric_limits<std::uint64_t>::max() /
             kNanosecondsPerSecond) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Payload fractional airtime calculation overflowed"});
  }
  const auto fractional_numerator =
      remainder_bits * kNanosecondsPerSecond;
  const auto fractional_nanoseconds =
      fractional_numerator / bits_per_second +
      (fractional_numerator % bits_per_second == 0 ? 0 : 1);
  const auto whole_nanoseconds = whole_seconds * kNanosecondsPerSecond;
  if(fractional_nanoseconds >
     static_cast<std::uint64_t>(kMaximumNanoseconds) -
         whole_nanoseconds) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Payload airtime exceeds SimDuration range"});
  }
  return contracts::SimDuration::FromNanoseconds(
      static_cast<contracts::NanosecondCount>(whole_nanoseconds +
                                              fractional_nanoseconds));
}

class RateBasedTxPhy final : public contracts::ITxPhy {
 public:
  [[nodiscard]] static auto Create(RateBasedTxPhyConfig config)
      -> contracts::Result<RateBasedTxPhy>;

  [[nodiscard]] auto Encode(
      const contracts::DigitalPacket& packet,
      const contracts::TxEncodeRequest& request) const
      -> contracts::Result<contracts::TxEmission> override;

  [[nodiscard]] constexpr auto config() const noexcept
      -> const RateBasedTxPhyConfig& {
    return config_;
  }

 private:
  explicit constexpr RateBasedTxPhy(RateBasedTxPhyConfig config) noexcept
      : config_(std::move(config)) {}

  RateBasedTxPhyConfig config_;
};

inline auto RateBasedTxPhy::Create(RateBasedTxPhyConfig config)
    -> contracts::Result<RateBasedTxPhy> {
  if(config.bits_per_second == 0) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInvalidArgument,
                         "RateBasedTxPhy bit rate must be positive"});
  }
  if(!std::isfinite(config.center_frequency_hz) ||
     !std::isfinite(config.occupied_bandwidth_hz) ||
     !std::isfinite(config.source_level_db_re_1upa_at_1m) ||
     config.center_frequency_hz <= 0.0 ||
     config.occupied_bandwidth_hz <= 0.0 ||
     config.occupied_bandwidth_hz / 2.0 >
         config.center_frequency_hz) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kOutOfRange,
            "RateBasedTxPhy physical configuration must be finite and "
            "describe a positive occupied acoustic band"});
  }
  return RateBasedTxPhy{std::move(config)};
}

inline auto RateBasedTxPhy::Encode(
    const contracts::DigitalPacket& packet,
    const contracts::TxEncodeRequest& request) const
    -> contracts::Result<contracts::TxEmission> {
  const auto duration =
      ComputePayloadAirtime(packet.payload.size(), config_.bits_per_second);
  if(!duration) return std::unexpected(duration.error());
  return contracts::TxEmission::Create(
      request.transmission_id,
      packet.packet_id,
      request.sender_node_id,
      request.started_at,
      *duration,
      config_.center_frequency_hz,
      config_.occupied_bandwidth_hz,
      config_.source_level_db_re_1upa_at_1m);
}

}  // namespace ns3_factory::phy::internal
