#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "modulation.hpp"
#include "packet_bit_adapter.hpp"

namespace ns3_factory::phy::internal {

struct ConfiguredTxPhyConfig final {
  ModulationConfig modulation;
  contracts::SimDuration sample_period;
  double occupied_bandwidth_hz;
  double source_level_db_re_1upa_at_1m;

  auto operator==(const ConfiguredTxPhyConfig&) const -> bool = default;
};

// Tx-only adapter for the frozen ITxPhy boundary. The generated waveform is
// used locally to derive deterministic duration metadata and is not retained
// by runtime or placed in DigitalPacket/TxEmission. Occupied bandwidth and
// source level remain explicit configuration rather than inferred defaults.
class ConfiguredTxPhy final : public contracts::ITxPhy {
 public:
  [[nodiscard]] static auto Create(ConfiguredTxPhyConfig config)
      -> contracts::Result<ConfiguredTxPhy>;

  ConfiguredTxPhy(const ConfiguredTxPhy&) = delete;
  auto operator=(const ConfiguredTxPhy&) -> ConfiguredTxPhy& = delete;
  ConfiguredTxPhy(ConfiguredTxPhy&&) noexcept = default;
  auto operator=(ConfiguredTxPhy&&) noexcept
      -> ConfiguredTxPhy& = default;

  [[nodiscard]] auto Encode(
      const contracts::DigitalPacket& packet,
      const contracts::TxEncodeRequest& request) const
      -> contracts::Result<contracts::TxEmission> override;

  [[nodiscard]] constexpr auto config() const noexcept
      -> const ConfiguredTxPhyConfig& {
    return config_;
  }

 private:
  ConfiguredTxPhy(ConfiguredTxPhyConfig config,
                  std::unique_ptr<IModulator> modulator) noexcept
      : config_(std::move(config)), modulator_(std::move(modulator)) {}

  [[nodiscard]] auto DurationForSamples(std::size_t sample_count) const
      -> contracts::Result<contracts::SimDuration>;

  ConfiguredTxPhyConfig config_;
  std::unique_ptr<IModulator> modulator_;
};

inline auto ConfiguredTxPhy::Create(ConfiguredTxPhyConfig config)
    -> contracts::Result<ConfiguredTxPhy> {
  const auto samples_per_symbol = SamplesPerSymbol(config.modulation);
  if(!samples_per_symbol) {
    return std::unexpected(samples_per_symbol.error());
  }
  if(config.sample_period <= contracts::SimDuration::Zero()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Configured Tx sample period must be positive"});
  }
  if(!std::isfinite(config.occupied_bandwidth_hz) ||
     !std::isfinite(config.source_level_db_re_1upa_at_1m) ||
     config.occupied_bandwidth_hz <= 0.0 ||
     config.occupied_bandwidth_hz / 2.0 >
         config.modulation.acoustic_carrier_hz) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Configured Tx occupied bandwidth must be finite "
                         "and fit within non-negative acoustic frequency"});
  }

  constexpr auto kNanosecondsPerSecond = 1'000'000'000.0L;
  const auto configured_samples_per_second =
      static_cast<long double>(config.modulation.sample_rate_hz) *
      static_cast<long double>(config.sample_period.nanoseconds());
  if(configured_samples_per_second != kNanosecondsPerSecond) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "Configured Tx sample period must exactly match sample rate"});
  }

  auto modulator = CreateModulator(config.modulation.scheme);
  if(!modulator) return std::unexpected(modulator.error());
  return ConfiguredTxPhy{std::move(config), std::move(*modulator)};
}

inline auto ConfiguredTxPhy::DurationForSamples(
    std::size_t sample_count) const
    -> contracts::Result<contracts::SimDuration> {
  const auto period = config_.sample_period.nanoseconds();
  constexpr auto kMaximum =
      std::numeric_limits<contracts::NanosecondCount>::max();
  const auto maximum_samples =
      static_cast<std::uint64_t>(kMaximum / period);
  if(sample_count > maximum_samples) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Configured Tx waveform duration overflows"});
  }
  return contracts::SimDuration::FromNanoseconds(
      static_cast<contracts::NanosecondCount>(sample_count) * period);
}

inline auto ConfiguredTxPhy::Encode(
    const contracts::DigitalPacket& packet,
    const contracts::TxEncodeRequest& request) const
    -> contracts::Result<contracts::TxEmission> {
  const auto frame = ExtractPayloadBitFrame(packet);
  if(!frame) return std::unexpected(frame.error());
  const auto waveform = modulator_->Modulate(*frame, config_.modulation);
  if(!waveform) return std::unexpected(waveform.error());
  const auto duration = DurationForSamples(waveform->sample_count());
  if(!duration) return std::unexpected(duration.error());

  return contracts::TxEmission::Create(
      request.transmission_id,
      packet.packet_id,
      request.sender_node_id,
      request.started_at,
      *duration,
      config_.modulation.acoustic_carrier_hz,
      config_.occupied_bandwidth_hz,
      config_.source_level_db_re_1upa_at_1m);
}

}  // namespace ns3_factory::phy::internal
