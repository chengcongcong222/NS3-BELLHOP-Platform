#pragma once

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/role.hpp>
#include <ns3_factory/contracts/state.hpp>

namespace ns3_factory::structure::internal {

class IRoleAssignmentPolicy {
 public:
  virtual ~IRoleAssignmentPolicy() = default;

  [[nodiscard]] virtual auto Assign(
      const contracts::WorldSnapshot& snapshot,
      const contracts::ConnectivityGraph& connectivity_graph,
      contracts::PlanningCycleId cycle_id) const
      -> contracts::Result<contracts::RoleTable> = 0;
};

}  // namespace ns3_factory::structure::internal
