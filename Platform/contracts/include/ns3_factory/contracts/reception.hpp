#pragma once

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

struct Reception final {
  ReceptionId reception_id;
  TransmissionId transmission_id;
  NodeId receiver_node_id;
  SimTime arrival_at;

  constexpr auto operator==(const Reception&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
