#pragma once

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>
#include <ns3_factory/contracts/state.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/planning_input.hpp>
#include <ns3_factory/contracts/planning_context.hpp>

namespace ns3_factory::planning::internal {

class IProtocolCyclePlanner {
 public:
  virtual ~IProtocolCyclePlanner() = default;
  virtual auto uses_flow_planning() const noexcept -> bool { return false; }
  virtual auto BuildFlow(const contracts::WorldSnapshot&, const contracts::StructureSnapshot&,
      const contracts::PlanningInput&, const contracts::FlowPlanningState&) const
      -> contracts::Result<contracts::ProtocolCyclePlan> {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kUnsupported, "Flow planning is not supported by this interface"});
  }

  [[nodiscard]] virtual auto Build(
      const contracts::WorldSnapshot& world_snapshot,
      const contracts::StructureSnapshot& structure_snapshot) const
      -> contracts::Result<contracts::ProtocolCyclePlan> = 0;

  [[nodiscard]] virtual auto Build(
      const contracts::WorldSnapshot& world_snapshot,
      const contracts::StructureSnapshot& structure_snapshot,
      const contracts::PlanningInput& input) const
      -> contracts::Result<contracts::ProtocolCyclePlan> {
    const auto valid = contracts::ValidatePlanningInput(
        input, world_snapshot, structure_snapshot);
    if(!valid) return std::unexpected(valid.error());
    return Build(world_snapshot, structure_snapshot);
  }
};

}  // namespace ns3_factory::planning::internal
