#pragma once

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>

namespace ns3_factory::planning::internal {

class IProtocolCyclePlanner {
 public:
  virtual ~IProtocolCyclePlanner() = default;

  [[nodiscard]] virtual auto Build(
      const contracts::WorldSnapshot& world_snapshot,
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::ProtocolCyclePlan> = 0;
};

}  // namespace ns3_factory::planning::internal
