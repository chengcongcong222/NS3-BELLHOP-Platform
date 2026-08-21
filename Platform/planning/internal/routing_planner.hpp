#pragma once

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/routing.hpp>
#include <ns3_factory/contracts/structure.hpp>

namespace ns3_factory::planning::internal {

class IRoutingPlanner {
 public:
  virtual ~IRoutingPlanner() = default;

  [[nodiscard]] virtual auto Build(
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::RoutingPlan> = 0;
};

}  // namespace ns3_factory::planning::internal
