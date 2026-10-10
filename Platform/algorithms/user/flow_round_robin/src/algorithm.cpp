#include "../algorithm.hpp"
namespace example_flow_round_robin {
using namespace ns3_factory;
auto Plan(const planning::PlanningContext& context,const planning::Parameters& parameters)
    -> contracts::Result<planning::FlowTransmissionPlan> {
  using namespace contracts;
  auto quota=planning::IntegerParameter(parameters,"packets_per_turn");
  if(parameters.size()!=1 || !quota || *quota<1 || *quota>16)
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Invalid round-robin quota"});
  struct Cursor { const FlowDemand* flow; std::uint64_t remaining; SimDuration width; };
  std::vector<Cursor> cursors;
  for(const auto& flow:context.data().demands) {
    if(!flow.next_hop) continue;
    const auto* link=context.FindLink(flow.key.queue_owner_node_id,*flow.next_hop);
    if(!link || link->status!=LinkPredictionStatus::kAvailable) continue;
    auto width=CheckedAdd(flow.maximum_airtime,context.data().physical.maximum_propagation_tail());
    if(!width) return std::unexpected(Error{ErrorCode::kOverflow,"Reservation overflow"});
    cursors.push_back({&flow,flow.queued_packets+flow.ready_packets,*width});
  }
  // Context supplies canonical (owner, FlowId) order; never treat NodeId as an index.
  auto offset=SimDuration::Zero();
  std::vector<planning::ProposedFlowGrant> grants;
  bool progress=true;
  while(progress) {
    progress=false;
    for(auto& cursor:cursors) for(std::int64_t turn=0;turn<*quota && cursor.remaining;++turn) {
      auto end=CheckedAdd(offset,cursor.width);
      if(!end) return std::unexpected(Error{ErrorCode::kOverflow,"Cycle offset overflow"});
      if(*end>context.data().duration) break;
      if(grants.size()>=context.data().budgets.maximum_grants)
        return std::unexpected(Error{ErrorCode::kInvalidArgument,"Grant budget exceeded"});
      grants.push_back({cursor.flow->key,offset,SimDuration::Zero()});
      offset=*end;--cursor.remaining;progress=true;
    }
  }
  return planning::FlowTransmissionPlan::Create(context,grants);
}
}
