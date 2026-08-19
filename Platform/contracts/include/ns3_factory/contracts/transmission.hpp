#pragma once

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>

namespace ns3_factory::contracts {

struct Transmission final {
  TransmissionId transmission_id;
  PacketId packet_id;
  NodeId sender_node_id;
  // Current-hop protocol/link intent, not a physical receiver set.
  TransmissionTarget target;
  SimTime started_at;
  SimTime ended_at;

  constexpr auto operator==(const Transmission&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
