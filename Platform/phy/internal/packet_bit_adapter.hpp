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

[[nodiscard]] inline auto RecoverPayloadBytes(const BitFrame& frame)
    -> Result<std::vector<std::byte>> {
  constexpr auto kBitsPerByte = 8U;
  if(frame.bit_count() % kBitsPerByte != 0U) {
    return std::unexpected(
        Error{ErrorCode::kFailedPrecondition,
              "Recovered bit count must be a whole number of bytes"});
  }

  std::vector<std::byte> payload;
  payload.reserve(frame.bit_count() / kBitsPerByte);
  for(std::size_t byte_index = 0U;
      byte_index < frame.bit_count() / kBitsPerByte;
      ++byte_index) {
    unsigned int value = 0U;
    for(std::size_t bit_index = 0U; bit_index < kBitsPerByte;
        ++bit_index) {
      value = (value << 1U) |
              frame.bits()[byte_index * kBitsPerByte + bit_index];
    }
    payload.push_back(static_cast<std::byte>(value));
  }
  return payload;
}

}  // namespace ns3_factory::phy::internal
