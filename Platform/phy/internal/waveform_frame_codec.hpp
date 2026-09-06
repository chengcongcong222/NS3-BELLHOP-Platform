#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>

namespace ns3_factory::phy::internal {

// Explicit reference frame: payload followed by little-endian CRC-32/ISO-HDLC.
// No preamble, FEC or hardware compatibility is implied. Receiver framing and
// phase remain ideal. CRC decisions never consult simulation truth.
inline constexpr std::size_t kWaveformCrcBytes = 4;

[[nodiscard]] inline auto WaveformCrc32(std::span<const std::byte> bytes)
    -> std::uint32_t {
  std::uint32_t crc = 0xffffffffU;
  for(const auto byte : bytes) {
    crc ^= std::to_integer<std::uint8_t>(byte);
    for(unsigned bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1U) ^ ((crc & 1U) ? 0xedb88320U : 0U);
    }
  }
  return ~crc;
}

[[nodiscard]] inline auto AddWaveformCrc(std::span<const std::byte> payload)
    -> contracts::Result<std::vector<std::byte>> {
  if(payload.empty()) {
    return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument, "Waveform payload is empty"});
  }
  std::vector<std::byte> frame{payload.begin(), payload.end()};
  const auto crc = WaveformCrc32(payload);
  for(unsigned shift = 0; shift < 32; shift += 8) {
    frame.push_back(static_cast<std::byte>((crc >> shift) & 0xffU));
  }
  return frame;
}

[[nodiscard]] inline auto CheckWaveformCrc(std::span<const std::byte> frame)
    -> bool {
  if(frame.size() <= kWaveformCrcBytes) return false;
  const auto payload_size = frame.size() - kWaveformCrcBytes;
  std::uint32_t expected = 0;
  for(unsigned i = 0; i < 4; ++i) {
    expected |= std::to_integer<std::uint32_t>(frame[payload_size + i])
                << (8U * i);
  }
  return WaveformCrc32(frame.first(payload_size)) == expected;
}

}  // namespace ns3_factory::phy::internal
