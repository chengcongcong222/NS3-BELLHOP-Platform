#include <cstdlib>
#include <limits>
#include <utility>

#include <ns3_factory/contracts/connectivity.hpp>
#include <ns3_factory/contracts/structure.hpp>
#include <ns3_factory/contracts/topology.hpp>

#include "internal/acceptance_scenario_config.hpp"
#include "internal/composite_protocol_cycle_planner.hpp"
#include "internal/configured_tdma_mac_planner.hpp"
#include "internal/configured_tdma_policy.hpp"
#include "internal/direct_to_sink_routing_planner.hpp"

using namespace ns3_factory::assembly::internal;
using namespace ns3_factory::contracts;

namespace {

auto TestStandardProfiles() -> bool {
  const auto acceptance = MakeAcceptance4NodeConfig();
  const auto extended = MakeExtended6NodeConfig();
  if(!acceptance || !extended) return false;
  const auto world = acceptance->InitialWorldSnapshot();
  const auto roles = acceptance->RoleBindings();
  const auto fusion = world ? world->FindNode(acceptance->fusion_center_node_id())
                            : std::nullopt;
  return acceptance->nodes().size() == 4 &&
         acceptance->sensor_node_ids().size() == 3 &&
         acceptance->communication_rate_bits_per_second() == 60 &&
         acceptance->maximum_packet_airtime().nanoseconds() ==
             2'000'000'000 &&
         acceptance->tdma_guard_interval().nanoseconds() == 2'000'000'000 &&
         acceptance->tdma_slot_duration().nanoseconds() == 4'000'000'000 &&
         acceptance->communication_cycle_duration().nanoseconds() ==
             12'000'000'000 &&
         acceptance->network_update_interval_cycles() == 10 &&
         acceptance->ber_requirement() == 1.0e-4 &&
         acceptance->minimum_bearing_points() == 5 &&
         acceptance->maximum_fusion_period().nanoseconds() ==
             180'000'000'000 &&
         acceptance->target_x_meters() == 200.0 &&
         acceptance->target_y_meters() == 150.0 &&
         acceptance->average_initial_horizontal_range_meters() > 900.0 &&
         acceptance->average_initial_horizontal_range_meters() < 1'100.0 &&
         world && world->nodes().size() == 4 && fusion &&
         fusion->get().motion.position.z_meters == -8.0 &&
         fusion->get().motion.velocity == Velocity3d{0.0, 0.0, 0.0} &&
         std::count_if(roles.begin(), roles.end(), [](RoleBinding binding) {
           return binding.role == ProtocolRole::kSink;
         }) == 1 &&
         extended->nodes().size() == 6 &&
         extended->sensor_node_ids().size() == 5 &&
         extended->communication_cycle_duration().nanoseconds() ==
             20'000'000'000;
}

auto TestValidationFailures() -> bool {
  auto base = Acceptance4NodeParameters();
  if(!base) return false;

  auto too_few = *base;
  too_few.nodes.erase(too_few.nodes.begin() + 2,
                      too_few.nodes.end());
  auto too_many = *base;
  while(too_many.nodes.size() < 7) {
    auto node = too_many.nodes.front();
    node.initial_state.node_id = NodeId{100 + too_many.nodes.size()};
    too_many.nodes.push_back(node);
  }
  auto zero_rate = *base;
  zero_rate.communication_rate_bits_per_second = 0;
  auto negative_guard = *base;
  negative_guard.tdma_guard_interval =
      SimDuration::FromNanoseconds(-1);
  auto zero_interval = *base;
  zero_interval.network_update_interval_cycles = 0;
  auto zero_ber = *base;
  zero_ber.ber_requirement = 0.0;
  auto one_ber = *base;
  one_ber.ber_requirement = 1.0;
  auto short_slot = *base;
  short_slot.tdma_slot_duration =
      SimDuration::FromNanoseconds(3'999'999'999);
  auto insufficient_points = *base;
  insufficient_points.minimum_bearing_points = 4;
  auto zero_fusion_period = *base;
  zero_fusion_period.maximum_fusion_period = SimDuration::Zero();
  auto invalid_target = *base;
  invalid_target.target_x_meters =
      std::numeric_limits<double>::infinity();

  return !AcceptanceScenarioConfig::Create(std::move(too_few)) &&
         !AcceptanceScenarioConfig::Create(std::move(too_many)) &&
         !AcceptanceScenarioConfig::Create(std::move(zero_rate)) &&
         !AcceptanceScenarioConfig::Create(std::move(negative_guard)) &&
         !AcceptanceScenarioConfig::Create(std::move(zero_interval)) &&
         !AcceptanceScenarioConfig::Create(std::move(zero_ber)) &&
         !AcceptanceScenarioConfig::Create(std::move(one_ber)) &&
         !AcceptanceScenarioConfig::Create(std::move(short_slot)) &&
         !AcceptanceScenarioConfig::Create(std::move(insufficient_points)) &&
         !AcceptanceScenarioConfig::Create(std::move(zero_fusion_period)) &&
         !AcceptanceScenarioConfig::Create(std::move(invalid_target));
}

auto TestExtendedScenarioPlanningValidation() -> bool {
  const auto config = MakeExtended6NodeConfig();
  if(!config) return false;
  const auto world = config->InitialWorldSnapshot();
  if(!world) return false;
  std::vector<NodeId> node_ids;
  std::vector<DirectedLink> directed_links;
  std::vector<LogicalLink> logical_links;
  for(const auto& node : world->nodes()) node_ids.push_back(node.node_id);
  for(const auto sensor : config->sensor_node_ids()) {
    directed_links.push_back(
        DirectedLink{sensor, config->fusion_center_node_id()});
    logical_links.push_back(
        LogicalLink{sensor, config->fusion_center_node_id()});
  }
  auto connectivity = ConnectivityGraph::Create(directed_links, node_ids);
  if(!connectivity) return false;
  auto topology = LogicalTopology::Create(
      logical_links, node_ids, *connectivity);
  auto roles = RoleTable::Create(config->RoleBindings(), node_ids);
  if(!topology || !roles) return false;
  auto structure = StructureSnapshot::Create(PlanningCycleId{0},
                                             SnapshotVersion{0},
                                             std::move(*roles),
                                             std::move(*connectivity),
                                             std::move(*topology));
  auto tdma =
      ns3_factory::planning::internal::ConfiguredTdmaPolicy::Create(
          config->tdma_slot_duration(),
          config->tdma_guard_interval(),
          std::vector<NodeId>{config->sensor_node_ids().begin(),
                              config->sensor_node_ids().end()});
  if(!structure || !tdma) return false;
  const ns3_factory::planning::internal::DirectToSinkRoutingPlanner routing;
  const ns3_factory::planning::internal::ConfiguredTdmaMacPlanner mac{*tdma};
  const ns3_factory::planning::internal::CompositeProtocolCyclePlanner planner{
      routing, mac};
  const auto plan = planner.Build(*world, *structure);
  return plan && plan->mac_plan().tx_opportunities().size() == 5 &&
         plan->routing_plan() && plan->routing_plan()->entries().size() == 5 &&
         plan->timing().closes_at().nanoseconds() == 20'000'000'000;
}

}  // namespace

auto main() -> int {
  return TestStandardProfiles() && TestValidationFailures() &&
                 TestExtendedScenarioPlanningValidation()
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
