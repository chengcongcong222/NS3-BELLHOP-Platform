#pragma once
#include <ns3_factory/planning/sdk.hpp>
namespace example_channel_aware {
using namespace ns3_factory;
struct Configuration final {
  double delay_weight;
  contracts::SimDuration guard;
  static auto Bind(const planning::Parameters& values) -> contracts::Result<Configuration> {
    auto weight=planning::NumberParameter(values,"delay_weight");
    auto guard=planning::IntegerParameter(values,"guard_margin_ns");
    if(values.size()!=2 || !weight || !guard || *weight<0 || *weight>1 || *guard<0)
      return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,"Invalid channel ordering parameters"});
    return Configuration{*weight,contracts::SimDuration::FromNanoseconds(*guard)};
  }
};

inline auto Plan(const planning::PlanningContext& context,const planning::Parameters& values)
    -> contracts::Result<planning::FlowTransmissionPlan> {
  using namespace contracts;
  const auto config=Configuration::Bind(values);
  if(!config) return std::unexpected(config.error());
  struct Candidate { const FlowDemand* demand; double loss,delay; };
  std::vector<Candidate> candidates;
  for(const auto& flow:context.data().demands) {
    if(!flow.next_hop) continue;
    auto link=context.FindLink(flow.key.queue_owner_node_id,*flow.next_hop);
    if(!link || link->status!=LinkPredictionStatus::kAvailable) continue;
    candidates.push_back({&flow,*link->loss_db,static_cast<double>(link->first_delay->nanoseconds())/1e9});
  }
  // 各列在本周期候选中归一化，避免将 dB 和秒直接相加。
  double loss_min=0,loss_max=0,delay_min=0,delay_max=0;
  if(!candidates.empty()) {
    loss_min=loss_max=candidates.front().loss; delay_min=delay_max=candidates.front().delay;
    for(const auto& c:candidates) {
      loss_min=std::min(loss_min,c.loss); loss_max=std::max(loss_max,c.loss);
      delay_min=std::min(delay_min,c.delay); delay_max=std::max(delay_max,c.delay);
    }
  }
  const auto score=[&](const Candidate& c) {
    auto loss=loss_max>loss_min ? (c.loss-loss_min)/(loss_max-loss_min) : 0;
    auto delay=delay_max>delay_min ? (c.delay-delay_min)/(delay_max-delay_min) : 0;
    return (1-config->delay_weight)*loss+config->delay_weight*delay;
  };
  std::sort(candidates.begin(),candidates.end(),[&](const auto& a,const auto& b) {
    auto left=score(a),right=score(b);
    return left!=right ? left<right : a.demand->key<b.demand->key;
  });
  auto offset=SimDuration::Zero();
  std::vector<planning::ProposedFlowGrant> grants;
  for(const auto& candidate:candidates) {
    const auto& flow=*candidate.demand;
    auto width=CheckedAdd(flow.maximum_airtime,context.data().physical.maximum_propagation_tail());
    if(width) width=CheckedAdd(*width,config->guard);
    if(!width) return std::unexpected(Error{ErrorCode::kOverflow,"Planning reservation overflows"});
    for(std::uint64_t i=0;i<flow.queued_packets+flow.ready_packets;++i) {
      auto end=CheckedAdd(offset,*width);
      if(!end) return std::unexpected(Error{ErrorCode::kOverflow,"Planning cycle offset overflows"});
      if(*end>context.data().duration) break;
      if(grants.size()>=context.data().budgets.maximum_grants) return std::unexpected(Error{
          ErrorCode::kInvalidArgument,"Planning grant deployment budget exceeded"});
      grants.push_back({flow.key,offset,config->guard}); offset=*end;
    }
  }
  return planning::FlowTransmissionPlan::Create(context,grants);
}
}  // namespace example_channel_aware
