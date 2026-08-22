#pragma once

#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/transmission.hpp>

namespace ns3_factory::runtime::internal {

// Network metadata retained independently of receiver-specific physical
// signals and reception processing results.
class TransmissionRecord final {
 public:
  [[nodiscard]] static auto Create(
      contracts::DigitalPacket packet,
      contracts::Transmission transmission)
      -> contracts::Result<TransmissionRecord>;

  [[nodiscard]] constexpr auto packet() const noexcept
      -> const contracts::DigitalPacket& {
    return packet_;
  }

  [[nodiscard]] constexpr auto transmission() const noexcept
      -> const contracts::Transmission& {
    return transmission_;
  }

  [[nodiscard]] constexpr auto transmission_id() const noexcept
      -> contracts::TransmissionId {
    return transmission_.transmission_id;
  }

  auto operator==(const TransmissionRecord&) const -> bool = default;

 private:
  TransmissionRecord(contracts::DigitalPacket packet,
                     contracts::Transmission transmission)
      : packet_(std::move(packet)),
        transmission_(std::move(transmission)) {}

  contracts::DigitalPacket packet_;
  contracts::Transmission transmission_;
};

inline auto TransmissionRecord::Create(
    contracts::DigitalPacket packet,
    contracts::Transmission transmission)
    -> contracts::Result<TransmissionRecord> {
  if(packet.packet_id != transmission.packet_id) {
    return std::unexpected(
        contracts::Error{
            contracts::ErrorCode::kFailedPrecondition,
            "TransmissionRecord packet and transmission identities differ"});
  }
  return TransmissionRecord{std::move(packet), std::move(transmission)};
}

}  // namespace ns3_factory::runtime::internal
