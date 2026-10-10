#pragma once

#include <array>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <span>
#include <vector>

#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/packet.hpp>

#include "internal/packet_queue_store.hpp"
#include "internal/scenario_cycle_application.hpp"

namespace ns3_factory::assembly::internal {

inline constexpr std::array kCommunicationTrafficPayload{
    std::byte{0x43}, std::byte{0x4f}, std::byte{0x4d}, std::byte{0x4d}};

struct CommunicationTrafficFlow final {
  contracts::NodeId source_node_id;
  contracts::NodeId destination_node_id;
  std::size_t packets_per_cycle;
  std::optional<contracts::FlowId> flow_id{};
};

// Generic deterministic traffic source. It creates network payloads only and
// deliberately has no feature decoding or fusion behavior.
class CommunicationTrafficApplication final : public IScenarioCycleApplication {
 public:
  auto forecast_flows(contracts::PlanningCycleId) const
      -> contracts::Result<std::vector<contracts::FlowInputForecast>> override {
    std::vector<contracts::FlowInputForecast> result;
    for(const auto& flow : flows_) {
      if(!flow.flow_id) return std::unexpected(contracts::Error{contracts::ErrorCode::kFailedPrecondition,
          "Flow planning needs frozen business flow identities"});
      result.push_back({*flow.flow_id,flow.source_node_id,flow.destination_node_id,flow.packets_per_cycle});
    }
    return result;
  }
  CommunicationTrafficApplication(
      std::vector<contracts::NodeId> sources,
      contracts::NodeId destination,
      runtime::internal::PacketQueueStore& queues,
      contracts::PacketId first_packet_id = contracts::PacketId{1'000'000})
      : sources_(std::move(sources)), queues_(queues),
        next_packet_id_(first_packet_id.value()) {
    for(const auto source : sources_) flows_.push_back({source, destination, 1});
  }

  CommunicationTrafficApplication(
      std::vector<CommunicationTrafficFlow> flows,
      runtime::internal::PacketQueueStore& queues,
      contracts::PacketId first_packet_id = contracts::PacketId{1'000'000})
      : flows_(std::move(flows)), queues_(queues),
        next_packet_id_(first_packet_id.value()),
        timing_(ApplicationInputTiming::kCycleStart) {
    std::sort(flows_.begin(), flows_.end(), [](const auto& a, const auto& b) {
      if(a.source_node_id!=b.source_node_id) return a.source_node_id<b.source_node_id;
      if(a.destination_node_id!=b.destination_node_id) return a.destination_node_id<b.destination_node_id;
      return a.flow_id<b.flow_id;
    });
    for(const auto& flow : flows_) {
      if(sources_.empty() || sources_.back() != flow.source_node_id)
        sources_.push_back(flow.source_node_id);
    }
  }

  auto input_node_ids() const noexcept
      -> std::span<const contracts::NodeId> override { return sources_; }

  auto input_timing() const noexcept -> ApplicationInputTiming override {
    return timing_;
  }

  auto forecast_packet_count(contracts::PlanningCycleId,
                             contracts::NodeId node_id) const
      -> contracts::Result<std::uint64_t> override {
    std::uint64_t count = 0;
    const auto nodes = queues_.get().node_ids();
    for(std::size_t i = 0; i < flows_.size(); ++i) {
      const auto& flow = flows_[i];
      if(flow.packets_per_cycle == 0 ||
         flow.source_node_id == flow.destination_node_id ||
         !std::binary_search(nodes.begin(), nodes.end(), flow.source_node_id) ||
         !std::binary_search(nodes.begin(), nodes.end(), flow.destination_node_id)) {
        return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,
            "Communication flow endpoints or packet count are invalid"});
      }
      for(std::size_t j = 0; j < i; ++j) {
        if((flow.flow_id && flows_[j].flow_id==flow.flow_id) ||
           (!flow.flow_id && flows_[j].source_node_id == flow.source_node_id &&
           flows_[j].destination_node_id == flow.destination_node_id)) {
          return std::unexpected(contracts::Error{contracts::ErrorCode::kAlreadyExists,
              "Communication flow is duplicated"});
        }
      }
      if(flow.source_node_id != node_id) continue;
      if(flow.packets_per_cycle > std::numeric_limits<std::uint64_t>::max() - count) {
        return std::unexpected(contracts::Error{contracts::ErrorCode::kOverflow,
            "Communication forecast packet count overflows"});
      }
      count += flow.packets_per_cycle;
    }
    return count;
  }

  auto OnInputReady(contracts::PlanningCycleId cycle_id,
                    contracts::NodeId node_id,
                    contracts::SimTime,
                    const runtime::internal::CycleWorkingState&)
      -> contracts::Status override {
    const auto forecast = forecast_packet_count(cycle_id, node_id);
    if(!forecast) return std::unexpected(forecast.error());
    if(*forecast == 0) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition,
        "Communication input node has no configured flow"});
    if(*forecast > std::numeric_limits<std::uint64_t>::max() - next_packet_id_) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kOverflow,
                                               "Traffic packet identity exhausted"});
    }
    for(const auto& flow : flows_) {
      if(flow.source_node_id != node_id) continue;
      for(std::size_t i = 0; i < flow.packets_per_cycle; ++i) {
        std::vector<std::byte> payload{kCommunicationTrafficPayload.begin(),
                                       kCommunicationTrafficPayload.end()};
        const auto status = queues_.get().Enqueue(
            node_id, contracts::DigitalPacket{
                contracts::PacketId{next_packet_id_++}, node_id,
                contracts::UnicastDestination{flow.destination_node_id},
                                     std::move(payload), flow.flow_id});
        if(!status) return status;
      }
    }
    return {};
  }

  auto OnRuntimeDecision(contracts::PlanningCycleId,
                         contracts::SimTime,
                         const runtime::internal::CycleWorkingState&)
      -> contracts::Status override { return {}; }

 private:
  std::vector<contracts::NodeId> sources_;
  std::vector<CommunicationTrafficFlow> flows_;
  std::reference_wrapper<runtime::internal::PacketQueueStore> queues_;
  std::uint64_t next_packet_id_;
  ApplicationInputTiming timing_{ApplicationInputTiming::kLegacyFirstTxOpportunity};
};

}  // namespace ns3_factory::assembly::internal
