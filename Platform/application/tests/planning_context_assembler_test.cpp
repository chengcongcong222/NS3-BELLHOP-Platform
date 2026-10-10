#include <iostream>
#include "internal/planning_context_assembler.hpp"
#include "flow_context_fixture.hpp"
using namespace ns3_factory;
using namespace contracts;
using namespace environment::internal;
using namespace application::internal;
#define CHECK(x) do { if(!(x)) { std::cerr << "line " << __LINE__ << '\n'; return 1; } } while(false)
auto Provider(double loss,bool no_arrival=false) -> Result<AcousticFieldChannelProvider> {
  auto frame=EnvironmentCoordinateFrame::Create(0,VerticalAxisDirection::kPositiveUp);
  std::vector<AcousticFieldCell> cells;
  for(int i=0;i<2;++i) {
    if(no_arrival) cells.emplace_back(AcousticFieldNoArrivalCell{});
    else cells.emplace_back(AcousticFieldSignalCell{loss+10*i,SimDuration::FromNanoseconds(5+i),{}});
  }
  auto asset=AcousticFieldAsset::Create(1,"prediction gate",*frame,{25000},{10},{10},{0,200},std::move(cells));
  if(!asset) return std::unexpected(asset.error());
  return AcousticFieldChannelProvider::Create(std::make_shared<const AcousticFieldAsset>(std::move(*asset)),
      *DiscreteFrequencySelectionPolicy::Create(0));
}
int main() {
  auto data=planning::test::Fixture();
  FlowPlanningState state{data.structure_origin,data.demands,{}};
  auto first=Provider(30),second=Provider(70),silent=Provider(0,true);CHECK(first && second && silent);
  const auto assemble=[&](const auto& world,const auto& provider,std::string id) {
    return AssemblePlanningContext("run",world,data.structure,data.routing,data.physical,state,
        PlanningEnvironment{provider,id,"fnv1a64:"+id,id},25000,4000,data.duration,42,{},data.algorithm);
  };
  auto a=assemble(data.world,*first,"first"),b=assemble(data.world,*second,"second");CHECK(a && b);
  auto left=a->FindLink(NodeId{0},NodeId{99}),right=b->FindLink(NodeId{0},NodeId{99});CHECK(left && right);
  CHECK(left->environment_id=="first" && right->environment_id=="second");
  CHECK(*right->loss_db-*left->loss_db==40);
  CHECK(!left->path_count && !left->maximum_excess_delay);
  const auto query=ChannelQuery::Create(TransmissionId{77},NodeId{0},NodeId{99},left->tx_position,left->rx_position,
      SimTime::Zero(),25000,4000);CHECK(query);
  auto sampled=first->Sample(left->tx_position,left->rx_position,25000);
  auto actual=first->Query(*query);CHECK(sampled && actual);
  const auto& scalar=std::get<AcousticFieldSignalCell>(*sampled);
  CHECK(scalar.aggregate_transmission_loss_db==*left->loss_db && scalar.first_arrival_delay==*left->first_delay);
  // No query identities or RNG/trace handles are supplied to Assemble; doing
  // another prediction must not alter the same formal communication query.
  auto again=assemble(data.world,*first,"first");CHECK(again);
  auto actual_again=first->Query(*query);CHECK(actual_again && *actual_again==*actual);
  auto none=assemble(data.world,*silent,"silent");CHECK(none);
  CHECK(none->FindLink(NodeId{0},NodeId{99})->status==LinkPredictionStatus::kNoArrival);
  std::vector<NodeCommittedState> nodes(data.world.nodes().begin(),data.world.nodes().end());
  nodes[3].motion.position.x_meters=150;
  auto moved=WorldSnapshot::Create(data.world.version(),data.world.committed_at(),nodes);CHECK(moved);
  auto c=assemble(*moved,*first,"first");CHECK(c);
  CHECK(c->FindLink(NodeId{0},NodeId{99})->loss_db!=left->loss_db);
  nodes[3].motion.position.x_meters=1000;
  auto outside=WorldSnapshot::Create(data.world.version(),data.world.committed_at(),nodes);CHECK(outside);
  CHECK(!assemble(*outside,*first,"first")); // out of coverage is not NoArrival
  CHECK(a->data().previous.links.empty() && !a->data().previous.cycle_id);
  std::cout << "ENVIRONMENT_GEOMETRY_SHARED_SAMPLER_AND_NO_SIDE_EFFECTS_OK\n";
}
