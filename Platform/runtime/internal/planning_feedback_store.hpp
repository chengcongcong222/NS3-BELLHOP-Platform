#pragma once
#include <map>
#include <ns3_factory/contracts/planning_context.hpp>
#include "internal/transmission_session.hpp"
#include "internal/reception_disposition_service.hpp"

namespace ns3_factory::runtime::internal {

// Formal run input, deliberately independent of Trace, observers and transport.
class PlanningFeedbackStore final {
 public:
  using Key = std::tuple<contracts::NodeId, contracts::NodeId, bool>;
  auto OnTransmission(const TransmissionSession& session) -> contracts::Status {
    const auto& tx = session.transmission();
    for(const auto& channel : session.channel_outcomes()) {
      const bool intended = IsTarget(tx, channel.receiver_node_id);
      auto& row = Row(tx.sender_node_id, channel.receiver_node_id, intended);
      auto status = Add(row.attempts, 1);
      if(!status) return status;
    }
    return {};
  }
  auto OnArrival(const contracts::Transmission& tx, contracts::NodeId receiver) -> contracts::Status {
    auto& row = Row(tx.sender_node_id, receiver, IsTarget(tx, receiver));
    if(row.arrivals >= row.attempts) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kFailedPrecondition, "Planning feedback arrival without attempt"});
    return Add(row.arrivals, 1);
  }
  auto OnReception(const contracts::Transmission& tx, const ReceptionSession& session,
                   const ReceptionDisposition& disposition) -> contracts::Status {
    using namespace contracts;
    const auto& decode = session.decode_result();
    auto& row = Row(tx.sender_node_id, decode.receiver_node_id(), IsTarget(tx, decode.receiver_node_id()));
    auto status = Add(decode.outcome() == DecodeOutcome::kDecoded ? row.decoded : row.not_decoded, 1);
    if(!status) return status;
    if(std::holds_alternative<LocalDeliveryReception>(disposition)) status = Add(row.deliveries, 1);
    if(std::holds_alternative<RelayEnqueueReception>(disposition)) status = Add(row.relays, 1);
    if(!status) return status;
    if(const auto& bits = decode.bit_comparison()) {
      status = Add(bits->crc_ok ? row.crc_passed : row.crc_failed, 1);
      if(!status) return status;
      status = Add(row.compared_bits, bits->compared_payload_bits);
      if(!status) return status;
      status = Add(row.error_bits, bits->erroneous_payload_bits);
    }
    return status;
  }
  auto Complete(contracts::PlanningCycleId cycle, contracts::SnapshotVersion version,
                contracts::SimTime start, contracts::SimTime end) const -> contracts::PreviousCycleFeedback {
    contracts::PreviousCycleFeedback result{cycle, version, start, end, {}};
    for(const auto& [key, row] : rows_) result.links.push_back(row);
    return result;
  }
 private:
  static auto Add(std::uint64_t& target, std::uint64_t count) -> contracts::Status {
    if(count > UINT64_MAX-target) return std::unexpected(contracts::Error{
        contracts::ErrorCode::kOverflow, "Planning feedback counter overflow"});
    target += count; return {};
  }
  static auto IsTarget(const contracts::Transmission& tx, contracts::NodeId receiver) -> bool {
    if(const auto* target = std::get_if<contracts::UnicastTransmissionTarget>(&tx.target)) return target->node_id == receiver;
    return true;
  }
  auto Row(contracts::NodeId sender, contracts::NodeId receiver, bool target) -> contracts::LinkFeedback& {
    return rows_.try_emplace({sender,receiver,target}, contracts::LinkFeedback{sender,receiver,target}).first->second;
  }
  std::map<Key, contracts::LinkFeedback> rows_;
};
}  // namespace ns3_factory::runtime::internal
