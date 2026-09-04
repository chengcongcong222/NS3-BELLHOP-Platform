#pragma once

#include <cstdint>
#include <functional>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "internal/physical_artifact_lifecycle.hpp"

namespace ns3_factory::assembly::internal {

enum class PhyExecutionPath : std::uint8_t {
  kAbstract = 1,
  kWaveform = 2,
};

// Assembly is the product-mode composition point. Runtime consumes the same
// stable Tx/channel/noise/Rx contracts for both paths and only receives the
// optional lifecycle boundary needed to release waveform-sized artifacts.
class PhyExecutionComposition final {
 public:
  [[nodiscard]] static auto Abstract(
      const contracts::ITxPhy& tx_phy,
      const contracts::IChannelFieldProvider& channel_provider,
      const contracts::INoiseFieldProvider& noise_provider,
      const contracts::IRxPhy& rx_phy) noexcept -> PhyExecutionComposition {
    return PhyExecutionComposition{PhyExecutionPath::kAbstract,
                                   tx_phy,
                                   channel_provider,
                                   noise_provider,
                                   rx_phy,
                                   nullptr};
  }

  [[nodiscard]] static auto Waveform(
      const contracts::ITxPhy& tx_phy,
      const contracts::IChannelFieldProvider& channel_provider,
      const contracts::INoiseFieldProvider& noise_provider,
      const contracts::IRxPhy& rx_phy,
      phy::internal::IPhysicalArtifactLifecycle& artifacts) noexcept
      -> PhyExecutionComposition {
    return PhyExecutionComposition{PhyExecutionPath::kWaveform,
                                   tx_phy,
                                   channel_provider,
                                   noise_provider,
                                   rx_phy,
                                   &artifacts};
  }

  [[nodiscard]] constexpr auto path() const noexcept -> PhyExecutionPath {
    return path_;
  }
  [[nodiscard]] constexpr auto tx_phy() const noexcept
      -> const contracts::ITxPhy& { return tx_phy_; }
  [[nodiscard]] constexpr auto channel_provider() const noexcept
      -> const contracts::IChannelFieldProvider& { return channel_provider_; }
  [[nodiscard]] constexpr auto noise_provider() const noexcept
      -> const contracts::INoiseFieldProvider& { return noise_provider_; }
  [[nodiscard]] constexpr auto rx_phy() const noexcept
      -> const contracts::IRxPhy& { return rx_phy_; }
  [[nodiscard]] constexpr auto artifact_lifecycle() const noexcept
      -> phy::internal::IPhysicalArtifactLifecycle* { return artifacts_; }

 private:
  PhyExecutionComposition(
      PhyExecutionPath path,
      const contracts::ITxPhy& tx_phy,
      const contracts::IChannelFieldProvider& channel_provider,
      const contracts::INoiseFieldProvider& noise_provider,
      const contracts::IRxPhy& rx_phy,
      phy::internal::IPhysicalArtifactLifecycle* artifacts) noexcept
      : path_(path), tx_phy_(tx_phy), channel_provider_(channel_provider),
        noise_provider_(noise_provider), rx_phy_(rx_phy), artifacts_(artifacts) {}

  PhyExecutionPath path_;
  std::reference_wrapper<const contracts::ITxPhy> tx_phy_;
  std::reference_wrapper<const contracts::IChannelFieldProvider>
      channel_provider_;
  std::reference_wrapper<const contracts::INoiseFieldProvider>
      noise_provider_;
  std::reference_wrapper<const contracts::IRxPhy> rx_phy_;
  phy::internal::IPhysicalArtifactLifecycle* artifacts_;
};

}  // namespace ns3_factory::assembly::internal
