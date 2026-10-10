#pragma once

#include <span>
#include <cstdint>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>
#include <ns3_factory/contracts/planning_context.hpp>

#include "internal/cycle_working_state.hpp"

namespace ns3_factory::assembly::internal {

enum class ApplicationInputTiming {
  kLegacyFirstTxOpportunity,
  kCycleStart,
};

class IScenarioCycleApplication {
 public:
  virtual ~IScenarioCycleApplication() = default;
  virtual auto forecast_flows(contracts::PlanningCycleId) const
      -> contracts::Result<std::vector<contracts::FlowInputForecast>> {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kUnsupported,
        "Application has not provided explicit flow identities"});
  }

  [[nodiscard]] virtual auto input_node_ids() const noexcept
      -> std::span<const contracts::NodeId> = 0;

  [[nodiscard]] virtual auto input_timing() const noexcept
      -> ApplicationInputTiming {
    return ApplicationInputTiming::kLegacyFirstTxOpportunity;
  }

  // Pure forecast of packets to be enqueued at this cycle's INPUT_READY event.
  // It neither generates packets nor advances any application state.
  [[nodiscard]] virtual auto forecast_packet_count(
      contracts::PlanningCycleId, contracts::NodeId) const
      -> contracts::Result<std::uint64_t> { return std::uint64_t{0}; }

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
