#pragma once
#include <string_view>
#include <ns3_factory/contracts/planning_context.hpp>

namespace ns3_factory::contracts {
enum class GrantExecutionStatus { kExecuted, kUnusedSelectedFlowEmpty, kRejected };
struct GrantExecutionEvidence final {
  TxOpportunity opportunity;
  SimTime actual_at;
  GrantExecutionStatus status;
  std::optional<PacketId> packet_id;
  std::optional<TransmissionId> transmission_id;
};
// Read-only diagnostic projection. Never used for algorithm feedback, queue
// ownership or event scheduling. Failures cannot change simulation decisions.
class IPlanningEvidenceSink {
 public:
  virtual ~IPlanningEvidenceSink()=default;
  virtual void OnPlan(const PlanningContext&,const ProtocolCyclePlan&,std::string_view package_sha256) noexcept=0;
  virtual void OnGrant(const GrantExecutionEvidence&) noexcept=0;
};
}  // namespace ns3_factory::contracts
