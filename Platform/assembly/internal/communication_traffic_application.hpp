#pragma once

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

// Generic deterministic traffic source. It creates network payloads only and
// deliberately has no feature decoding or fusion behavior.
class CommunicationTrafficApplication final : public IScenarioCycleApplication {
 public:
  CommunicationTrafficApplication(
      std::vector<contracts::NodeId> sources,
      contracts::NodeId destination,
      runtime::internal::PacketQueueStore& queues,
      contracts::PacketId first_packet_id = contracts::PacketId{1'000'000})
      : sources_(std::move(sources)), destination_(destination), queues_(queues),
        next_packet_id_(first_packet_id.value()) {}

  auto input_node_ids() const noexcept
      -> std::span<const contracts::NodeId> override { return sources_; }

  auto OnInputReady(contracts::PlanningCycleId,
                    contracts::NodeId node_id,
                    contracts::SimTime,
                    const runtime::internal::CycleWorkingState&)
      -> contracts::Status override {
    if(next_packet_id_ == std::numeric_limits<std::uint64_t>::max()) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kOverflow,
                                               "Traffic packet identity exhausted"});
    }
    std::vector<std::byte> payload{
        std::byte{0x43}, std::byte{0x4f}, std::byte{0x4d}, std::byte{0x4d}};
    auto status = queues_.get().Enqueue(
        node_id, contracts::DigitalPacket{
                     contracts::PacketId{next_packet_id_++}, node_id,
                     contracts::UnicastDestination{destination_},
                     std::move(payload)});
    return status;
  }

  auto OnRuntimeDecision(contracts::PlanningCycleId,
                         contracts::SimTime,
                         const runtime::internal::CycleWorkingState&)
      -> contracts::Status override { return {}; }

 private:
  std::vector<contracts::NodeId> sources_;
  contracts::NodeId destination_;
  std::reference_wrapper<runtime::internal::PacketQueueStore> queues_;
  std::uint64_t next_packet_id_;
};

}  // namespace ns3_factory::assembly::internal
