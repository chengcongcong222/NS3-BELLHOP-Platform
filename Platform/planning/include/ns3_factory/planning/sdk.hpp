#pragma once
// Public v2 entry. No JSON, Runtime/internal, ns-3, queue owner or clock.
#include <ns3_factory/contracts/planning_context.hpp>
#include <ns3_factory/planning/mac_algorithm.hpp>

namespace ns3_factory::planning {
using contracts::PlanningContext;
using contracts::FlowDemandKey;

// Optional dense index for algorithms using matrices. Never cast a NodeId to
// an array subscript; the mapping belongs to this exact immutable snapshot.
inline auto NodeIndex(const PlanningContext& context,contracts::NodeId id) -> std::optional<std::size_t> {
  const auto nodes=context.data().world.nodes();
  for(std::size_t i=0;i<nodes.size();++i) if(nodes[i].node_id==id) return i;
  return std::nullopt;
}

// Canonical FNV-1a64 input fingerprint, not authentication or a signature.
// Every fixed-width value uses little-endian bytes; strings are length-framed.
inline auto ContextFingerprint(const PlanningContext& context) -> std::uint64_t {
  std::uint64_t hash = 14695981039346656037ULL;
  auto integer = [&](std::uint64_t value) { for(int i=0;i<8;++i) { hash ^= value & 255U; hash *= 1099511628211ULL; value >>= 8; } };
  auto text = [&](const std::string& value) { integer(value.size()); for(unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; } };
  auto real = [&](double value) { integer(std::bit_cast<std::uint64_t>(value)); };
  const auto& d = context.data();
  text("planning-context-v2-fnv1a64"); text(d.run_id);
  text(d.algorithm.id); text(d.algorithm.version); text(d.algorithm.instance);
  integer(d.structure.cycle_id().value()); integer(d.world.version().value());
  integer(d.world.committed_at().nanoseconds()); integer(d.duration.nanoseconds());
  integer(d.structure_origin.cycle_id.value()); integer(d.structure_origin.snapshot_version.value()); integer(d.structure_origin.generated_at.nanoseconds());
  integer(d.physical.maximum_propagation_tail().nanoseconds()); integer(d.deterministic_seed);
  integer(d.budgets.maximum_links); integer(d.budgets.maximum_demands); integer(d.budgets.maximum_grants);
  integer(d.world.nodes().size());
  for(const auto& node : d.world.nodes()) {
    integer(node.node_id.value()); integer(node.capability.can_transmit); integer(node.capability.can_receive);
    integer(static_cast<std::uint64_t>(node.capability.duplex_mode));
    real(node.motion.position.x_meters); real(node.motion.position.y_meters); real(node.motion.position.z_meters);
    real(node.motion.velocity.x_meters_per_second); real(node.motion.velocity.y_meters_per_second); real(node.motion.velocity.z_meters_per_second);
  }
  integer(d.structure.connectivity_graph().edges().size());
  for(const auto& edge : d.structure.connectivity_graph().edges()) { integer(edge.source_node_id.value()); integer(edge.target_node_id.value()); }
  integer(d.structure.logical_topology().links().size());
  for(const auto& edge : d.structure.logical_topology().links()) { integer(edge.source_node_id.value()); integer(edge.target_node_id.value()); }
  integer(d.structure.role_table().bindings().size());
  for(const auto& role : d.structure.role_table().bindings()) { integer(role.node_id.value()); integer(static_cast<std::uint64_t>(role.role)); }
  integer(d.routing.entries().size());
  for(const auto& route : d.routing.entries()) { integer(route.forwarding_node_id.value()); integer(route.destination_node_id.value()); integer(route.next_hop_node_id.value()); }
  integer(d.physical.demands().size());
  for(const auto& item : d.physical.demands()) { integer(item.node_id.value()); integer(item.queued_packet_count); integer(item.ready_packet_count); integer(item.maximum_packet_airtime.nanoseconds()); }
  integer(d.demands.size());
  for(const auto& item : d.demands) {
    integer(item.key.queue_owner_node_id.value()); integer(item.key.flow_id.value()); integer(item.source_node_id.value()); integer(item.destination_node_id.value());
    integer(item.next_hop.has_value()); if(item.next_hop) integer(item.next_hop->value());
    integer(item.queued_packets); integer(item.ready_packets); integer(item.maximum_airtime.nanoseconds());
  }
  integer(d.links.size());
  for(const auto& link : d.links) {
    integer(link.sender_node_id.value()); integer(link.receiver_node_id.value()); integer(link.observed_at.nanoseconds());
    real(link.center_frequency_hz); real(link.bandwidth_hz); text(link.environment_id); text(link.environment_version);
    text(link.environment_checksum); text(link.method_version); integer(static_cast<std::uint64_t>(link.status));
    integer(link.loss_db.has_value()); if(link.loss_db) real(*link.loss_db);
    integer(link.first_delay.has_value()); if(link.first_delay) integer(link.first_delay->nanoseconds());
    integer(link.path_count.has_value()); if(link.path_count) integer(*link.path_count);
    integer(link.maximum_excess_delay.has_value()); if(link.maximum_excess_delay) integer(link.maximum_excess_delay->nanoseconds());
  }
  integer(d.previous.cycle_id.has_value());
  if(d.previous.cycle_id) {
    integer(d.previous.cycle_id->value()); integer(d.previous.committed_version->value());
    integer(d.previous.started_at->nanoseconds()); integer(d.previous.ended_at->nanoseconds());
  }
  integer(d.previous.links.size());
  for(const auto& row : d.previous.links) {
    integer(row.sender_node_id.value()); integer(row.receiver_node_id.value()); integer(row.intended_target);
    integer(row.attempts); integer(row.arrivals); integer(row.decoded); integer(row.not_decoded); integer(row.deliveries); integer(row.relays);
    integer(row.crc_passed); integer(row.crc_failed); integer(row.compared_bits); integer(row.error_bits);
  }
  integer(d.effective_parameters.size());
  for(const auto& [key,value] : d.effective_parameters) { text(key); text(value); }
  return hash;
}

// Named deterministic stream: no RunId, wall clock, global engine, or mutable
// package state. SplitMix64 transition and 53-bit uniform conversion are fixed.
class NamedRandomStream final {
 public:
  static auto Create(const PlanningContext& context,const std::string& purpose)
      -> contracts::Result<NamedRandomStream> {
    if(purpose.empty() || purpose.size()>128) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kInvalidArgument,"Random purpose must be a bounded nonempty name"});
    std::uint64_t hash=14695981039346656037ULL;
    auto integer=[&](std::uint64_t value) { for(int i=0;i<8;++i) { hash^=value&255U;hash*=1099511628211ULL;value>>=8; } };
    auto text=[&](const std::string& value) { integer(value.size());for(unsigned char c:value) { hash^=c;hash*=1099511628211ULL; } };
    const auto& data=context.data();
    text("planning-rng-splitmix64-v1");integer(data.deterministic_seed);
    text(data.algorithm.id);text(data.algorithm.version);text(data.algorithm.instance);
    integer(data.structure.cycle_id().value());text(purpose);
    return NamedRandomStream{hash};
  }
  auto Next() -> std::uint64_t {
    auto z=(state_+=0x9e3779b97f4a7c15ULL);
    z=(z^(z>>30))*0xbf58476d1ce4e5b9ULL;
    z=(z^(z>>27))*0x94d049bb133111ebULL;
    return z^(z>>31);
  }
  auto Uniform() -> double { return static_cast<double>(Next()>>11)*0x1.0p-53; }
 private:
  explicit NamedRandomStream(std::uint64_t state):state_(state) {}
  std::uint64_t state_;
};

struct ProposedFlowGrant final {
  FlowDemandKey demand_key;
  contracts::SimDuration start_offset;
  contracts::SimDuration extra_guard;
};

class FlowTransmissionPlan final {
 public:
  static auto Create(const PlanningContext& context, const std::vector<ProposedFlowGrant>& grants)
      -> contracts::Result<FlowTransmissionPlan> {
    using namespace contracts;
    const auto& data = context.data();
    const auto invalid = [](std::string reason) -> Result<FlowTransmissionPlan> {
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "PlanningPlanInvalid: " + reason});
    };
    if(grants.size() > data.budgets.maximum_grants) return invalid("grant deployment budget exceeded");
    const auto end = CheckedAdd(data.world.committed_at(), data.duration);
    auto timing = CycleTiming::Create(data.structure.cycle_id(), data.world.version(), data.world.committed_at(), *end);
    if(!timing) return std::unexpected(timing.error());
    const auto fingerprint = ContextFingerprint(context);
    auto previous_end = data.world.committed_at();
    std::map<FlowDemandKey, std::uint64_t> allocated;
    std::vector<TxOpportunity> opportunities;
    for(const auto& grant : grants) {
      const auto* demand = context.FindDemand(grant.demand_key);
      if(!demand || !demand->next_hop || grant.start_offset < SimDuration::Zero() || grant.extra_guard < SimDuration::Zero())
        return invalid("unknown flow, no route or negative timing");
      if(++allocated[grant.demand_key] > demand->queued_packets + demand->ready_packets)
        return invalid("grant exceeds known flow demand");
      auto width = CheckedAdd(demand->maximum_airtime, data.physical.maximum_propagation_tail());
      if(width) width = CheckedAdd(*width, grant.extra_guard);
      const auto start = CheckedAdd(data.world.committed_at(), grant.start_offset);
      if(!width || !start) return invalid("nanosecond overflow");
      const auto reservation_end = CheckedAdd(*start, *width);
      if(!reservation_end || *start < previous_end || *start >= *end || *reservation_end > *end)
        return invalid("unsorted, overlapping or out-of-cycle grant");
      opportunities.push_back({grant.demand_key.queue_owner_node_id, *start,
          FlowSelectionConstraint{GrantId{opportunities.size()}, grant.demand_key.flow_id,
            demand->destination_node_id, *demand->next_hop, data.structure.cycle_id(), data.world.version(),
            fingerprint, *reservation_end}});
      previous_end = *reservation_end;
    }
    auto mac = MacPlan::Create(*timing, std::move(opportunities));
    if(!mac) return std::unexpected(mac.error());
    auto plan = ProtocolCyclePlan::Create(data.routing, *timing, std::move(*mac));
    if(!plan) return std::unexpected(plan.error());
    return FlowTransmissionPlan{std::move(*plan), fingerprint, grants};
  }
  auto Validate(const PlanningContext& context) const -> contracts::Status {
    auto rebuilt = Create(context, proposals_);
    if(!rebuilt) return std::unexpected(rebuilt.error());
    if(fingerprint_ != rebuilt->fingerprint_ || plan_ != rebuilt->plan_) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "PlanningPlanInvalid: context does not match sealed plan"});
    return {};
  }
  auto protocol_plan() const -> const contracts::ProtocolCyclePlan& { return plan_; }
  auto proposals() const -> const std::vector<ProposedFlowGrant>& { return proposals_; }
 private:
  FlowTransmissionPlan(contracts::ProtocolCyclePlan plan, std::uint64_t hash, std::vector<ProposedFlowGrant> proposals)
      : plan_(std::move(plan)), fingerprint_(hash), proposals_(std::move(proposals)) {}
  contracts::ProtocolCyclePlan plan_;
  std::uint64_t fingerprint_;
  std::vector<ProposedFlowGrant> proposals_;
};

using BuildFlowMac = contracts::Result<FlowTransmissionPlan> (*)(const PlanningContext&, const Parameters&);
struct InputCapability final {
  std::string name;
  bool required;
};
struct FlowMacAlgorithm final {
  std::string id, version, package_sha256, label, description;
  std::vector<Parameter> parameters;
  BuildFlowMac build;
  std::string knowledge, output;
  std::vector<InputCapability> inputs;
};
}  // namespace ns3_factory::planning
