#pragma once
#include <ns3_factory/planning/sdk.hpp>
namespace example_flow_round_robin {
auto Plan(const ns3_factory::planning::PlanningContext& context,
          const ns3_factory::planning::Parameters& parameters)
    -> ns3_factory::contracts::Result<ns3_factory::planning::FlowTransmissionPlan>;
}
