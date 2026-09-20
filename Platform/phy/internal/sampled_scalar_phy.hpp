#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <random>
#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

#include "physical_artifact_lifecycle.hpp"
#include "rate_based_tx_phy.hpp"
#include "scalar_ber_rx_phy.hpp"
#include "waveform_frame_codec.hpp"

namespace ns3_factory::phy::internal {

// Comparison-only scalar frame: no waveform, but the same payload + CRC32
// framing and airtime as the waveform baseline.
struct ScalarFrameArtifact final {
  contracts::TransmissionId transmission_id;
  contracts::PacketId packet_id;
  std::vector<std::byte> original_payload;
  std::vector<std::byte> framed_bytes;
};

class ScalarFrameStore final : public IPhysicalArtifactLifecycle {
 public:
  [[nodiscard]] auto Publish(ScalarFrameArtifact artifact)
      -> contracts::Status {
    const auto position = std::lower_bound(
        artifacts_.begin(), artifacts_.end(), artifact.transmission_id,
        [](const auto& entry, auto id) {
          return entry.transmission_id < id;
        });
    if(position != artifacts_.end() &&
       position->transmission_id == artifact.transmission_id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kAlreadyExists,
          "Scalar frame already exists for TransmissionId"});
    }
    artifacts_.insert(position, std::move(artifact));
    return {};
  }

  [[nodiscard]] auto Find(contracts::TransmissionId id) const
      -> contracts::Result<const ScalarFrameArtifact*> {
    const auto position = std::lower_bound(
        artifacts_.begin(), artifacts_.end(), id,
        [](const auto& entry, auto sought) {
          return entry.transmission_id < sought;
        });
    if(position == artifacts_.end() || position->transmission_id != id) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kNotFound,
          "Scalar frame is unavailable for TransmissionId"});
    }
    return &*position;
  }

  void ReleaseCycleArtifacts() noexcept override { artifacts_.clear(); }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return artifacts_.size();
  }

 private:
  std::vector<ScalarFrameArtifact> artifacts_;
};

[[nodiscard]] inline auto MixScalarSamplingSeed(std::uint64_t value)
    -> std::uint64_t {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

struct SampledScalarFrame final {
  std::vector<std::byte> bytes;
  std::size_t payload_bit_errors;
};

// Sampling is a pure function of the frame, probability, explicit seed and
// stable identities. It never uses callback order or mutable global RNG.
[[nodiscard]] inline auto SampleScalarFrameBits(
    std::span<const std::byte> framed_bytes,
    double bit_error_probability,
    std::uint64_t deterministic_seed,
    contracts::TransmissionId transmission_id,
    contracts::NodeId receiver_node_id)
    -> contracts::Result<SampledScalarFrame> {
  if(framed_bytes.size() <= kWaveformCrcBytes ||
     !std::isfinite(bit_error_probability) ||
     bit_error_probability < 0.0 || bit_error_probability > 1.0) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,
        "Scalar sampling requires a framed payload and finite BER in [0,1]"});
  }
  const auto mixed = MixScalarSamplingSeed(
      deterministic_seed ^
      MixScalarSamplingSeed(transmission_id.value()) ^
      MixScalarSamplingSeed(receiver_node_id.value() +
                            0x517cc1b727220a95ULL) ^
      0xb7e151628aed2a6bULL);
  std::mt19937_64 generator{mixed};
  constexpr double kUnit53 = 1.0 / 9007199254740992.0;
  const auto payload_bytes = framed_bytes.size() - kWaveformCrcBytes;
  std::vector<std::byte> corrupted{framed_bytes.begin(), framed_bytes.end()};
  std::size_t payload_errors = 0U;
  for(std::size_t byte_index = 0; byte_index < corrupted.size();
      ++byte_index) {
    auto value = std::to_integer<std::uint8_t>(corrupted[byte_index]);
    for(unsigned bit = 0U; bit < 8U; ++bit) {
      const auto unit =
          static_cast<double>(generator() >> 11U) * kUnit53;
      if(unit < bit_error_probability) {
        value ^= static_cast<std::uint8_t>(1U << bit);
        if(byte_index < payload_bytes) ++payload_errors;
      }
    }
    corrupted[byte_index] = static_cast<std::byte>(value);
  }
  return SampledScalarFrame{std::move(corrupted), payload_errors};
}

struct SampledScalarReceptionSummary final {
  contracts::TransmissionId transmission_id;
  contracts::NodeId receiver_node_id;
  std::size_t payload_bit_count;
  std::size_t bit_error_count;
  bool crc_ok;
  double modeled_bit_error_probability;
};

class ISampledScalarReceptionObserver {
 public:
  virtual ~ISampledScalarReceptionObserver() = default;
  virtual void Observe(const SampledScalarReceptionSummary&) noexcept = 0;
};

class SampledScalarTxPhy final : public contracts::ITxPhy {
 public:
  [[nodiscard]] static auto Create(RateBasedTxPhyConfig config,
                                   ScalarFrameStore& store)
      -> contracts::Result<SampledScalarTxPhy> {
    auto inner = RateBasedTxPhy::Create(config);
    if(!inner) return std::unexpected(inner.error());
    return SampledScalarTxPhy{std::move(*inner), store};
  }

  [[nodiscard]] auto Encode(
      const contracts::DigitalPacket& packet,
      const contracts::TxEncodeRequest& request) const
      -> contracts::Result<contracts::TxEmission> override {
    auto framed = AddWaveformCrc(packet.payload);
    if(!framed) return std::unexpected(framed.error());
    auto on_air = packet;
    on_air.payload = *framed;
    auto emission = inner_.Encode(on_air, request);
    if(!emission) return std::unexpected(emission.error());
    auto published = store_.get().Publish(ScalarFrameArtifact{
        request.transmission_id, packet.packet_id, packet.payload,
        std::move(*framed)});
    if(!published) return std::unexpected(published.error());
    return emission;
  }

 private:
  SampledScalarTxPhy(RateBasedTxPhy inner, ScalarFrameStore& store)
      : inner_(std::move(inner)), store_(store) {}

  RateBasedTxPhy inner_;
  std::reference_wrapper<ScalarFrameStore> store_;
};

class SampledScalarRxPhy final : public contracts::IRxPhy {
 public:
  [[nodiscard]] static auto Create(
      std::uint64_t bit_rate_bits_per_second,
      std::uint64_t deterministic_seed,
      const ScalarFrameStore& store,
      ISampledScalarReceptionObserver& observer)
      -> contracts::Result<SampledScalarRxPhy> {
    if(bit_rate_bits_per_second == 0U) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Sampled scalar receiver requires positive bit rate"});
    }
    return SampledScalarRxPhy{bit_rate_bits_per_second,
                              deterministic_seed, store, observer};
  }

  [[nodiscard]] auto Decode(const contracts::RxDecodeRequest& request) const
      -> contracts::Result<contracts::RxDecodeResult> override {
    if(!request.receiver_window().overlapping_signals().empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kUnsupported,
          "Sampled scalar comparison does not model interference"});
    }
    const auto& signal = request.receiver_window().desired_signal();
    auto artifact = store_.get().Find(signal.transmission_id());
    if(!artifact) return std::unexpected(artifact.error());
    if((*artifact)->packet_id != signal.emission().packet_id()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kFailedPrecondition,
          "Scalar frame PacketId differs from desired signal"});
    }
    const auto quality =
        ComputeP0ModeledBpskAwgnQuality(request, bit_rate_);
    if(!quality) return std::unexpected(quality.error());
    auto sampled = SampleScalarFrameBits(
        (*artifact)->framed_bytes, quality->bit_error_rate(), seed_,
        signal.transmission_id(), signal.receiver_node_id());
    if(!sampled) return std::unexpected(sampled.error());
    const bool crc_ok = CheckWaveformCrc(sampled->bytes);
    auto result = contracts::RxDecodeResult::Create(
        signal.transmission_id(), signal.emission().packet_id(),
        signal.receiver_node_id(),
        crc_ok ? contracts::DecodeOutcome::kDecoded
               : contracts::DecodeOutcome::kNotDecoded);
    if(!result) return std::unexpected(result.error());
    if(crc_ok) {
      sampled->bytes.resize(sampled->bytes.size() - kWaveformCrcBytes);
      result = result->WithRecoveredPayload(std::move(sampled->bytes));
      if(!result) return std::unexpected(result.error());
    }
    observer_.get().Observe(SampledScalarReceptionSummary{
        signal.transmission_id(), signal.receiver_node_id(),
        (*artifact)->original_payload.size() * 8U,
        sampled->payload_bit_errors, crc_ok, quality->bit_error_rate()});
    return result;
  }

 private:
  SampledScalarRxPhy(std::uint64_t rate, std::uint64_t seed,
                     const ScalarFrameStore& store,
                     ISampledScalarReceptionObserver& observer)
      : bit_rate_(rate), seed_(seed), store_(store), observer_(observer) {}

  std::uint64_t bit_rate_;
  std::uint64_t seed_;
  std::reference_wrapper<const ScalarFrameStore> store_;
  std::reference_wrapper<ISampledScalarReceptionObserver> observer_;
};

}  // namespace ns3_factory::phy::internal
