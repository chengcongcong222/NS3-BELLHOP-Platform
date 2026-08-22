#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/packet.hpp>

#include "modulation.hpp"

namespace ns3_factory::phy::internal {

// Payload bytes are serialized in network bit order: most-significant bit
// first within each byte. Packet metadata is deliberately not modulated.
[[nodiscard]] inline auto ExtractPayloadBitFrame(
    const contracts::DigitalPacket& packet) -> Result<BitFrame> {
  if(packet.payload.empty()) {
    return std::unexpected(
        Error{ErrorCode::kInvalidArgument,
              "DigitalPacket payload must contain at least one byte"});
  }

  std::vector<std::uint8_t> bits;
  constexpr auto kBitsPerByte = 8U;
  if(packet.payload.size() > bits.max_size() / kBitsPerByte) {
    return std::unexpected(
        Error{ErrorCode::kOverflow,
              "DigitalPacket payload is too large to serialize"});
  }
  bits.reserve(packet.payload.size() * kBitsPerByte);

  for(const auto payload_byte : packet.payload) {
    const auto value = std::to_integer<unsigned int>(payload_byte);
    for(auto bit_index = 0U; bit_index < kBitsPerByte; ++bit_index) {
      const auto shift = kBitsPerByte - 1U - bit_index;
      bits.push_back(
          static_cast<std::uint8_t>((value >> shift) & 1U));
    }
  }
  return BitFrame::Create(std::move(bits));
}

}  // namespace ns3_factory::phy::internal
