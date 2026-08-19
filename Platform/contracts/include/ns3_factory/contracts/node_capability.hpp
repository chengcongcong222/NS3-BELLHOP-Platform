#pragma once

#include <cstdint>

namespace ns3_factory::contracts {

enum class DuplexMode : std::uint8_t {
  kHalfDuplex = 1,
  kFullDuplex = 2,
};

struct NodeCapabilityProfile final {
  bool can_transmit;
  bool can_receive;
  DuplexMode duplex_mode;

  [[nodiscard]] constexpr auto can_communicate() const noexcept -> bool {
    return can_transmit || can_receive;
  }

  constexpr auto operator==(const NodeCapabilityProfile&) const
      -> bool = default;
};

}  // namespace ns3_factory::contracts
