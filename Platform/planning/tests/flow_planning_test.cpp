#include <iostream>
#include <ns3_factory/planning/sdk.hpp>
#include "flow_context_fixture.hpp"
#include "internal/fifo_packet_selector.hpp"
#include "internal/tx_preparation.hpp"
using namespace ns3_factory::contracts;
using namespace ns3_factory::planning;
using namespace ns3_factory::runtime::internal;
#define CHECK(value) do { if(!(value)) { std::cerr << "failed line " << __LINE__ << '\n'; return 1; } } while(false)

using ns3_factory::planning::test::Fixture;

int main() {
  const auto context = PlanningContext::Create(Fixture()); CHECK(context);
  CHECK(context->data().previous.links.empty() && !context->data().previous.cycle_id);
  CHECK(context->data().world.FindNode(NodeId{0}));
  CHECK(!context->data().world.FindNode(NodeId{1}));
  CHECK(NodeIndex(*context,NodeId{0})==0);
  CHECK(NodeIndex(*context,NodeId{10})==1);
  CHECK(NodeIndex(*context,NodeId{20})==2);
  CHECK(NodeIndex(*context,NodeId{99})==3);
  CHECK(!NodeIndex(*context,NodeId{1}));
  auto shuffled = Fixture(); std::reverse(shuffled.links.begin(),shuffled.links.end());
  std::reverse(shuffled.demands.begin(),shuffled.demands.end());
  const auto reordered = PlanningContext::Create(shuffled); CHECK(reordered);
  CHECK(ContextFingerprint(*context)==ContextFingerprint(*reordered));
  auto renamed=Fixture();renamed.run_id="different-run";
  auto renamed_context=PlanningContext::Create(renamed);CHECK(renamed_context);
  auto random_a=NamedRandomStream::Create(*context,"selection");
  auto random_b=NamedRandomStream::Create(*renamed_context,"selection");CHECK(random_a && random_b);
  for(int i=0;i<64;++i) CHECK(random_a->Next()==random_b->Next());
  CHECK(!NamedRandomStream::Create(*context,""));
  auto random_other=NamedRandomStream::Create(*context,"tie-break");CHECK(random_other);
  auto random_first=NamedRandomStream::Create(*context,"selection");CHECK(random_first);
  CHECK(random_first->Next()!=random_other->Next());
  std::vector<ProposedFlowGrant> grants{{{NodeId{0},FlowId{22}},SimDuration::Zero(),SimDuration::Zero()},
    {{NodeId{0},FlowId{11}},SimDuration::FromNanoseconds(20),SimDuration::Zero()}};
  const auto plan = FlowTransmissionPlan::Create(*context,grants); CHECK(plan); CHECK(plan->Validate(*context));
  auto wrong = grants; wrong[0].demand_key.flow_id=FlowId{99}; CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  wrong=grants; wrong[1].start_offset=SimDuration::FromNanoseconds(19); CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  wrong=grants; wrong[1].start_offset=SimDuration::FromNanoseconds(100); CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  wrong=grants; wrong[1].demand_key=wrong[0].demand_key; CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  auto bad=Fixture(); bad.demands[0].next_hop=NodeId{99}; CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.demands[0].ready_packets=2; CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.links[0].observed_at=SimTime::FromNanoseconds(1); CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.links[0].status=LinkPredictionStatus::kNoArrival; CHECK(!PlanningContext::Create(bad));
  bad.links[0].loss_db.reset(); bad.links[0].first_delay.reset(); CHECK(PlanningContext::Create(bad));
  bad=Fixture(); bad.links.pop_back(); CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.budgets.maximum_links=2; CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.previous.ended_at=SimTime::Zero(); CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.demands[0].queued_packets=UINT64_MAX; CHECK(!PlanningContext::Create(bad));
  bad=Fixture(); bad.budgets.maximum_grants=1;
  auto bounded=PlanningContext::Create(bad);CHECK(bounded);
  CHECK(!FlowTransmissionPlan::Create(*bounded,grants));
  wrong=grants;wrong[0].extra_guard=SimDuration::FromNanoseconds(INT64_MAX);
  CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  wrong=grants;wrong[0].start_offset=SimDuration::FromNanoseconds(-1);
  CHECK(!FlowTransmissionPlan::Create(*context,wrong));
  CHECK(!context->data().links[0].maximum_excess_delay && !context->data().links[0].path_count);
  bad=Fixture();bad.links[0].path_count=1;CHECK(!PlanningContext::Create(bad));
  auto different=Fixture(); different.links[0].loss_db=60;
  const auto changed=PlanningContext::Create(different); CHECK(changed); CHECK(!plan->Validate(*changed));

  auto queues=PacketQueueStore::Create({NodeId{0},NodeId{10},NodeId{20},NodeId{99}}); CHECK(queues);
  const DigitalPacket first{PacketId{1},NodeId{0},UnicastDestination{NodeId{10}},{std::byte{1}},FlowId{11}};
  const DigitalPacket second{PacketId{2},NodeId{0},UnicastDestination{NodeId{20}},{std::byte{2}},FlowId{22}};
  CHECK(queues->Enqueue(NodeId{0},first)); CHECK(queues->Enqueue(NodeId{0},second));
  FifoPacketSelector fifo; TxPreparationService prepare{fifo};
  const auto opportunity=plan->protocol_plan().mac_plan().tx_opportunities().front();
  const auto selected=prepare.Prepare(opportunity,*queues,context->data().routing); CHECK(selected);
  CHECK(std::get<ReadyTxPreparation>(*selected).selected_packet.packet==second);
  CHECK(!queues->ConsumeFlow(NodeId{0},FlowId{22},PacketId{1}));
  CHECK(queues->ConsumeFlow(NodeId{0},FlowId{22},PacketId{2}));
  CHECK(**queues->PeekFront(NodeId{0})==first);
  const auto empty=prepare.Prepare(opportunity,*queues,context->data().routing); CHECK(empty);
  CHECK(std::holds_alternative<NoPacketTxPreparation>(*empty));
  const auto legacy=prepare.Prepare({NodeId{0},SimTime::Zero()},*queues,context->data().routing); CHECK(legacy);
  CHECK(std::get<ReadyTxPreparation>(*legacy).selected_packet.packet==first);
  std::cout << "FLOW_CONTEXT_AND_SELECTION_OK\n";
}
