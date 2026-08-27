#pragma once

#include <span>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/cycle_working_state.hpp"

namespace ns3_factory::assembly::internal {

class IScenarioCycleApplication {
 public:
  virtual ~IScenarioCycleApplication() = default;

  [[nodiscard]] virtual auto input_node_ids() const noexcept
      -> std::span<const contracts::NodeId> = 0;

  [[nodiscard]] virtual auto OnInputReady(
      contracts::PlanningCycleId cycle_id,
      contracts::NodeId node_id,
      contracts::SimTime now,
      const runtime::internal::CycleWorkingState& working_state)
      -> contracts::Status = 0;

  [[nodiscard]] virtual auto OnRuntimeDecision(
      contracts::PlanningCycleId cycle_id,
      contracts::SimTime now,
      const runtime::internal::CycleWorkingState& working_state)
      -> contracts::Status = 0;
};

}  // namespace ns3_factory::assembly::internal
