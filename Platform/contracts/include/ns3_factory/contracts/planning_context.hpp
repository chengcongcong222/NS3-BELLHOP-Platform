#pragma once

#include <bit>
#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <ns3_factory/contracts/planning_input.hpp>

namespace ns3_factory::contracts {

struct FlowDemandKey final {
  NodeId queue_owner_node_id;
  FlowId flow_id;
  auto operator<=>(const FlowDemandKey&) const = default;
};
struct FlowDemand final {
  FlowDemandKey key;
  NodeId source_node_id;
  NodeId destination_node_id;
  std::optional<NodeId> next_hop;
  std::uint64_t queued_packets;
  std::uint64_t ready_packets;
  SimDuration maximum_airtime;
  auto operator==(const FlowDemand&) const -> bool = default;
};
struct StructureOrigin final {
  PlanningCycleId cycle_id;
  SnapshotVersion snapshot_version;
  SimTime generated_at;
};
enum class LinkPredictionStatus { kAvailable, kNoArrival, kUnavailable };
struct LinkPrediction final {
  NodeId sender_node_id;
  NodeId receiver_node_id;
  Position3d tx_position;
  Position3d rx_position;
  SimTime observed_at;
  double center_frequency_hz;
  double bandwidth_hz;
  std::string environment_id;
  std::string environment_version;
  std::string environment_checksum;
  std::string method_version;
  LinkPredictionStatus status;
  std::optional<double> loss_db;
  std::optional<SimDuration> first_delay;
  std::optional<SimDuration> maximum_excess_delay;
  std::optional<std::uint64_t> path_count;
  auto key() const { return std::pair{sender_node_id, receiver_node_id}; }
};
struct LinkFeedback final {
  NodeId sender_node_id;
  NodeId receiver_node_id;
  // Overheard physical processing never enters target-delivery denominators.
  bool intended_target;
  std::uint64_t attempts{}, arrivals{}, decoded{}, not_decoded{}, deliveries{}, relays{};
  std::uint64_t crc_passed{}, crc_failed{}, compared_bits{}, error_bits{};
  auto key() const { return std::tuple{sender_node_id, receiver_node_id, intended_target}; }
  auto operator==(const LinkFeedback&) const -> bool = default;
};
struct PreviousCycleFeedback final {
  // nullopt = NoHistory. Missing link row = NoSamples, never BER=0.
  std::optional<PlanningCycleId> cycle_id;
  std::optional<SnapshotVersion> committed_version;
  std::optional<SimTime> started_at, ended_at;
  std::vector<LinkFeedback> links;
};
struct PlanningBudgets final {
  std::size_t maximum_links{4096}, maximum_demands{4096}, maximum_grants{100000};
};
struct PlanningAlgorithmIdentity final {
  std::string id,version,instance;
};
struct PlanningContextData final {
  std::string run_id;
  WorldSnapshot world;
  StructureSnapshot structure;
  RoutingPlan routing;
  PlanningInput physical;
  StructureOrigin structure_origin;
  SimDuration duration;
  std::vector<FlowDemand> demands;
  std::vector<LinkPrediction> links;
  PreviousCycleFeedback previous;
  std::map<std::string, std::string> effective_parameters;
  std::uint64_t deterministic_seed{};
  PlanningBudgets budgets{};
  PlanningAlgorithmIdentity algorithm;
};

struct FlowInputForecast final {
  FlowId flow_id;
  NodeId source_node_id;
  NodeId destination_node_id;
  std::uint64_t packets;
};
struct FlowPlanningState final {
  StructureOrigin structure_origin;
  std::vector<FlowDemand> demands;
  PreviousCycleFeedback previous;
};

class PlanningContext final {
 public:
  static auto Create(PlanningContextData data) -> Result<PlanningContext> {
    const auto bad = [](std::string message) -> Result<PlanningContext> {
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "PlanningInputUnavailable: " + message});
    };
    const auto valid = ValidatePlanningInput(data.physical, data.world, data.structure);
    if(!valid) return std::unexpected(valid.error());
    if(data.run_id.empty() || data.algorithm.id.empty() || data.algorithm.version.empty() || data.algorithm.instance.empty() ||
       data.duration <= SimDuration::Zero() ||
       !CheckedAdd(data.world.committed_at(), data.duration) ||
       data.structure_origin.generated_at > data.world.committed_at() ||
       data.structure_origin.snapshot_version > data.world.version() ||
       data.structure_origin.cycle_id > data.structure.cycle_id()) return bad("invalid time/origin");
    const auto routes = RoutingPlan::Create(
        {data.routing.entries().begin(), data.routing.entries().end()}, data.structure);
    if(!routes || data.routing.cycle_id() != data.structure.cycle_id() ||
       data.routing.base_snapshot_version() != data.world.version()) return bad("stale routing");
    if(!data.budgets.maximum_links || !data.budgets.maximum_demands || !data.budgets.maximum_grants ||
       data.demands.size() > data.budgets.maximum_demands || data.links.size() > data.budgets.maximum_links)
      return bad("deployment budget exceeded");
    std::sort(data.demands.begin(), data.demands.end(), [](const auto& a, const auto& b) { return a.key < b.key; });
    std::map<NodeId, std::pair<std::uint64_t, std::uint64_t>> totals;
    std::map<FlowId, std::pair<NodeId, NodeId>> identities;
    std::optional<FlowDemandKey> last;
    for(const auto& demand : data.demands) {
      const auto owner = data.world.FindNode(demand.key.queue_owner_node_id);
      if(last == demand.key || !owner || !owner->get().capability.can_transmit ||
         !data.world.FindNode(demand.source_node_id) || !data.world.FindNode(demand.destination_node_id) ||
         demand.key.queue_owner_node_id == demand.destination_node_id ||
         demand.maximum_airtime <= SimDuration::Zero() ||
         (demand.ready_packets && demand.key.queue_owner_node_id != demand.source_node_id) ||
         demand.ready_packets > UINT64_MAX - demand.queued_packets) return bad("invalid flow demand");
      last = demand.key;
      const auto route = data.routing.FindNextHop(demand.key.queue_owner_node_id, demand.destination_node_id);
      if(route != demand.next_hop) return bad("demand does not use frozen route");
      auto [identity, inserted] = identities.emplace(demand.key.flow_id,
          std::pair{demand.source_node_id, demand.destination_node_id});
      if(!inserted && identity->second != std::pair{demand.source_node_id, demand.destination_node_id})
        return bad("FlowId belongs to conflicting endpoints");
      auto& total = totals[demand.key.queue_owner_node_id];
      if(demand.queued_packets > UINT64_MAX - total.first || demand.ready_packets > UINT64_MAX - total.second)
        return bad("flow count overflow");
      total.first += demand.queued_packets; total.second += demand.ready_packets;
      auto physical = std::find_if(data.physical.demands().begin(), data.physical.demands().end(),
          [&](const auto& item) { return item.node_id == demand.key.queue_owner_node_id; });
      if(physical == data.physical.demands().end() || demand.maximum_airtime != physical->maximum_packet_airtime)
        return bad("airtime is not the formal physical bound");
    }
    for(const auto& demand : data.physical.demands()) {
      if(totals[demand.node_id] != std::pair{demand.queued_packet_count, demand.ready_packet_count})
        return bad("flow totals differ from formal queue/input counts");
    }
    std::sort(data.links.begin(), data.links.end(), [](const auto& a, const auto& b) { return a.key() < b.key(); });
    std::optional<std::pair<NodeId, NodeId>> last_link;
    for(const auto& link : data.links) {
      auto tx = data.world.FindNode(link.sender_node_id), rx = data.world.FindNode(link.receiver_node_id);
      if(!tx || !rx || link.sender_node_id == link.receiver_node_id || last_link == link.key() ||
         !tx->get().capability.can_transmit || !rx->get().capability.can_receive ||
         tx->get().motion.position != link.tx_position || rx->get().motion.position != link.rx_position ||
         link.observed_at != data.world.committed_at() || !std::isfinite(link.center_frequency_hz) ||
         link.center_frequency_hz <= 0 || !std::isfinite(link.bandwidth_hz) || link.bandwidth_hz <= 0 ||
         link.environment_id.empty() || link.environment_version.empty() || link.environment_checksum.empty() ||
         link.method_version.empty()) return bad("invalid link prediction provenance");
      last_link = link.key();
      if(link.status == LinkPredictionStatus::kAvailable) {
        if(!link.loss_db || !std::isfinite(*link.loss_db) || !link.first_delay || *link.first_delay < SimDuration::Zero() ||
           link.path_count.has_value() != link.maximum_excess_delay.has_value() ||
           (link.path_count && (!*link.path_count || *link.maximum_excess_delay < SimDuration::Zero())))
          return bad("invalid available prediction");
        const auto tail=CheckedAdd(*link.first_delay,link.maximum_excess_delay.value_or(SimDuration::Zero()));
        if(!tail || *tail>data.physical.maximum_propagation_tail()) return bad("prediction exceeds formal global tail");
      } else if(link.status == LinkPredictionStatus::kNoArrival || link.status == LinkPredictionStatus::kUnavailable) {
        if(link.loss_db || link.first_delay || link.path_count || link.maximum_excess_delay)
          return bad("unavailable prediction contains invented values");
      } else return bad("unknown prediction status");
    }
    std::size_t expected_links = 0;
    for(const auto& tx : data.world.nodes()) if(tx.capability.can_transmit)
      for(const auto& rx : data.world.nodes()) if(rx.capability.can_receive && tx.node_id != rx.node_id) {
        if(++expected_links > data.budgets.maximum_links) return bad("link budget exceeded");
      }
    if(data.links.size() != expected_links) return bad("link scope incomplete");
    auto& previous = data.previous;
    if(!previous.cycle_id) {
      if(previous.committed_version || previous.started_at || previous.ended_at || !previous.links.empty())
        return bad("NoHistory must be empty");
    } else {
      if(!previous.committed_version || !previous.started_at || !previous.ended_at ||
         *previous.ended_at != data.world.committed_at() || *previous.started_at >= *previous.ended_at ||
         *previous.committed_version != data.world.version() ||
         previous.cycle_id->value() == UINT64_MAX || previous.cycle_id->value()+1 != data.structure.cycle_id().value())
        return bad("feedback not from previous complete cycle");
      std::sort(previous.links.begin(), previous.links.end(), [](const auto& a, const auto& b) { return a.key() < b.key(); });
      for(std::size_t i=0; i<previous.links.size(); ++i) {
        const auto& row = previous.links[i];
        if(!data.world.FindNode(row.sender_node_id) || !data.world.FindNode(row.receiver_node_id) ||
           row.sender_node_id==row.receiver_node_id || row.decoded>row.arrivals || row.not_decoded>row.arrivals-row.decoded ||
           row.deliveries>row.decoded || row.relays>row.decoded-row.deliveries ||
           row.crc_passed>row.decoded || row.crc_failed>row.not_decoded ||
           row.error_bits > row.compared_bits || row.arrivals > row.attempts ||
           (i && previous.links[i-1].key() == row.key()) ||
           (!row.intended_target && (row.deliveries || row.relays))) return bad("invalid formal feedback");
      }
    }
    return PlanningContext{std::move(data)};
  }
  auto data() const noexcept -> const PlanningContextData& { return data_; }
  auto FindDemand(FlowDemandKey key) const -> const FlowDemand* {
    auto it = std::lower_bound(data_.demands.begin(), data_.demands.end(), key,
        [](const auto& value, auto wanted) { return value.key < wanted; });
    return it != data_.demands.end() && it->key == key ? &*it : nullptr;
  }
  auto FindLink(NodeId tx, NodeId rx) const -> const LinkPrediction* {
    auto it = std::lower_bound(data_.links.begin(), data_.links.end(), std::pair{tx,rx},
        [](const auto& value, auto key) { return value.key() < key; });
    return it != data_.links.end() && it->key() == std::pair{tx,rx} ? &*it : nullptr;
  }
 private:
  explicit PlanningContext(PlanningContextData data) : data_(std::move(data)) {}
  PlanningContextData data_;
};

}  // namespace ns3_factory::contracts
