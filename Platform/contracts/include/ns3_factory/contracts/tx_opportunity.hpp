#pragma once

#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::contracts {

struct TxOpportunity final {
  NodeId sender_node_id;
  SimTime eligible_at;

  constexpr auto operator==(const TxOpportunity&) const -> bool = default;
};

}  // namespace ns3_factory::contracts
