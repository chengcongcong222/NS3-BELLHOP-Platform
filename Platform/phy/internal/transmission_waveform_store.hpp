#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>

#include "modulation.hpp"
#include "physical_artifact_lifecycle.hpp"
#include "waveform.hpp"

namespace ns3_factory::phy::internal {

// One modulation result for one physical Transmission. The original frame is
// retained beside X so a waveform receiver can compute measured bit errors.
// This object is transient PHY state, never authoritative world state or
// trace payload.
class TransmissionWaveformArtifact final {
 public:
  TransmissionWaveformArtifact(contracts::TransmissionId transmission_id,
                               contracts::PacketId packet_id,
                               BitFrame original_frame,
                               WaveformBuffer transmitted_waveform)
      : transmission_id_(transmission_id),
        packet_id_(packet_id),
        original_frame_(std::move(original_frame)),
        transmitted_waveform_(std::move(transmitted_waveform)) {}

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> contracts::TransmissionId {
    return transmission_id_;
  }

  [[nodiscard]] constexpr auto packet_id() const noexcept
      -> contracts::PacketId {
    return packet_id_;
  }

  [[nodiscard]] constexpr auto original_frame() const noexcept
      -> const BitFrame& {
    return original_frame_;
  }

  [[nodiscard]] constexpr auto transmitted_waveform() const noexcept
      -> const WaveformBuffer& {
    return transmitted_waveform_;
  }

 private:
  contracts::TransmissionId transmission_id_;
  contracts::PacketId packet_id_;
  BitFrame original_frame_;
  WaveformBuffer transmitted_waveform_;
};

using SharedTransmissionWaveformArtifact =
    std::shared_ptr<const TransmissionWaveformArtifact>;

// Waveform Tx publishes exactly once; every receiver resolves the same
// immutable artifact. A future HIL-backed implementation may satisfy this
// boundary without changing the Runtime or public contracts.
class ITransmissionWaveformArtifacts
    : public IPhysicalArtifactLifecycle {
 public:
  ~ITransmissionWaveformArtifacts() override = default;

  [[nodiscard]] virtual auto Publish(
      TransmissionWaveformArtifact artifact)
      -> contracts::Result<SharedTransmissionWaveformArtifact> = 0;

  [[nodiscard]] virtual auto Find(
      contracts::TransmissionId transmission_id) const
      -> contracts::Result<SharedTransmissionWaveformArtifact> = 0;
};

// Run-owned in-memory implementation. Entries are kept in deterministic
// TransmissionId order and are bounded by ScenarioRuntime's cycle cleanup.
class TransmissionWaveformStore final
    : public ITransmissionWaveformArtifacts {
 public:
  [[nodiscard]] auto Publish(TransmissionWaveformArtifact artifact)
      -> contracts::Result<SharedTransmissionWaveformArtifact> override {
    const auto position = LowerBound(artifact.transmission_id());
    if(position != artifacts_.end() &&
       (*position)->transmission_id() == artifact.transmission_id()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kAlreadyExists,
                           "Transmission waveform already exists"});
    }
    SharedTransmissionWaveformArtifact shared =
        std::make_shared<TransmissionWaveformArtifact>(std::move(artifact));
    artifacts_.insert(position, shared);
    return shared;
  }

  [[nodiscard]] auto Find(
      contracts::TransmissionId transmission_id) const
      -> contracts::Result<SharedTransmissionWaveformArtifact> override {
    const auto position = LowerBound(transmission_id);
    if(position == artifacts_.end() ||
       (*position)->transmission_id() != transmission_id) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kNotFound,
                           "Transmission waveform does not exist"});
    }
    return *position;
  }

  void ReleaseCycleArtifacts() noexcept override { artifacts_.clear(); }

  [[nodiscard]] auto size() const noexcept -> std::size_t {
    return artifacts_.size();
  }

 private:
  using Container = std::vector<SharedTransmissionWaveformArtifact>;

  [[nodiscard]] auto LowerBound(
      contracts::TransmissionId transmission_id) noexcept
      -> Container::iterator {
    return std::lower_bound(
        artifacts_.begin(), artifacts_.end(), transmission_id,
        [](const auto& artifact, const auto id) {
          return artifact->transmission_id() < id;
        });
  }

  [[nodiscard]] auto LowerBound(
      contracts::TransmissionId transmission_id) const noexcept
      -> Container::const_iterator {
    return std::lower_bound(
        artifacts_.begin(), artifacts_.end(), transmission_id,
        [](const auto& artifact, const auto id) {
          return artifact->transmission_id() < id;
        });
  }

  Container artifacts_;
};

}  // namespace ns3_factory::phy::internal
