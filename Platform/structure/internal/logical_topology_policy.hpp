#pragma once

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/topology.hpp>

namespace ns3_factory::structure::internal {

class ILogicalTopologyPolicy {
 public:
  virtual ~ILogicalTopologyPolicy() = default;

  [[nodiscard]] virtual auto Build(
      const contracts::WorldSnapshot& snapshot,
      const contracts::RoleTable& role_table,
      const contracts::ConnectivityGraph& connectivity_graph) const
      -> contracts::Result<contracts::LogicalTopology> = 0;
};

}  // namespace ns3_factory::structure::internal
