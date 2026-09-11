#include <iostream>
#include <stdexcept>
#include <ns3_factory/trajectory/csv_import.hpp>
#include <ns3_factory/trajectory/generator.hpp>
#include "internal/acceptance_scenario_config.hpp"
#include "internal/reference_environment_asset.hpp"
#include "internal/acoustic_field_channel_provider.hpp"
#include "internal/rate_based_tx_phy.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/commit_service.hpp"
#ifdef TRAJECTORY_NS3_TEST
#include "internal/event_dispatcher.hpp"
#include "internal/ns3_kernel_gateway.hpp"
#endif

using namespace ns3_factory;
using namespace contracts;
using namespace trajectory;
using namespace assembly::internal;
using namespace environment::internal;
using namespace runtime::internal;
template<class V> concept HasVelocityUpdate = requires(V value) {
  value.UpdateVelocity(NodeId{0},Velocity3d{0,0,0},SimTime::Zero());
};
static_assert(!HasVelocityUpdate<CycleWorkingState>);
constexpr auto T(std::int64_t seconds) { return SimTime::FromNanoseconds(seconds*1'000'000'000); }
void Check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }
template<class V> auto Take(Result<V> value) -> V {
  if(!value) throw std::runtime_error(value.error().message);
  return std::move(*value);
}
struct RecordingChannel : IChannelFieldProvider {
  const IChannelFieldProvider& inner;
  mutable std::vector<ChannelQuery> queries;
  explicit RecordingChannel(const IChannelFieldProvider& provider) : inner(provider) {}
  auto Query(const ChannelQuery& q) const -> Result<ChannelFieldOutcome> override {
    queries.push_back(q); return inner.Query(q);
  }
};
int main() {
  try {
    auto config = Take(AcceptanceScenarioConfig::Create(Take(Acceptance4NodeParameters())));
    auto original = Take(config.InitialWorldSnapshot());
    auto generated = Take(BindGenerated(original,T(144)));
    for(const auto& node : original.nodes()) for(auto second : {0,4,12,24,120,144}) {
      auto p = Take(generated->Query(node.node_id,T(second))).position;
      Check(std::abs(p.x_meters-(node.motion.position.x_meters+node.motion.velocity.x_meters_per_second*second))<1e-9 &&
            std::abs(p.y_meters-(node.motion.position.y_meters+node.motion.velocity.y_meters_per_second*second))<1e-9 &&
            p.z_meters == node.motion.position.z_meters,"Acceptance4Node migration position mismatch");
    }
    auto imported = Take(ImportCsv(
        "track_id,time_s,x_m,y_m,z_m\nN1,0,1000,0,-65\nN1,10,1300,0,-65\nN1,20,1000,0,-65\n"
        "N2,0,0,0,-8\nN2,20,0,0,-8\n",
        "turning-track",1,CoordinateSystem::kLocalCartesianMetersZUp,"external-CSV-test"));
    Repository repository;
    Check(repository.Register(std::move(imported)).has_value(),"register imported resource");
    auto bound = Take(BoundTrajectories::Create(repository,
        {{NodeId{1},{"turning-track",1,"N1"}},{NodeId{2},{"turning-track",1,"N2"}}}));
    auto snapshot = Take(WorldSnapshot::Create(SnapshotVersion{0},T(0),
        {acceptance_config_detail::Node(1,{1000,0,-65},{999,0,0},ProtocolRole::kMember).initial_state,
         acceptance_config_detail::Node(2,{0,0,-8},{},ProtocolRole::kSink).initial_state}));
    // Deliberately misleading old velocity: runtime must ignore it completely.
    auto working = Take(CycleWorkingState::Create(snapshot,PlanningCycleId{0},T(0),bound));
    Check(!CycleWorkingState::Create(snapshot,PlanningCycleId{0},T(0),nullptr),"missing bindings rejected");
    auto asset = Take(BuildReferenceShallowWaterV1(PLATFORM_REFERENCE_ENVIRONMENT_ASSET_ROOT));
    auto provider = Take(AcousticFieldChannelProvider::Create(std::make_shared<const AcousticFieldAsset>(std::move(asset)),
        Take(DiscreteFrequencySelectionPolicy::Create(0))));
    RecordingChannel channel{provider};
    auto tx = Take(phy::internal::RateBasedTxPhy::Create({60,25000,4000,110}));
    CommunicationIdAllocator ids{TransmissionId{1}};
    TransmissionExecutor executor{ids,tx,channel};
    auto send = [&](int second) {
      const DigitalPacket packet{PacketId{1},NodeId{1},UnicastDestination{NodeId{2}},{std::byte{0xa5}}};
      auto session = Take(executor.ExecuteTransmission(working,
          {{NodeId{1},T(second)},packet,UnicastTransmissionTarget{NodeId{2}},T(second),{NodeId{2}}}));
      Check(!session.received_signals().empty(),"formal Bellhop path must exist");
      Check(channel.queries.back().tx_position() == Take(bound->Query(NodeId{1},T(second))).position &&
            channel.queries.back().rx_position() == Position3d{0,0,-8},"Bellhop must use trajectory geometry");
    };
#ifdef TRAJECTORY_NS3_TEST
    kernel::internal::Ns3KernelGateway gateway;
    struct Cleanup { kernel::internal::Ns3KernelGateway& gateway; ~Cleanup() { gateway.Destroy(); } } cleanup{gateway};
    kernel::internal::EventDispatcher dispatcher{gateway};
    for(auto second : {0,5,10,15}) {
      Take(dispatcher.Schedule(kernel::internal::TxStartEvent{T(second),[&,second]() -> Status {
        Check(Take(gateway.PlatformNow()) == T(second),"ns-3 is the sole event time authority");
        send(second);
        return {};
      }}));
    }
    Check(dispatcher.Run().has_value(),"ns-3 trajectory execution");
#else
    for(auto second : {0,5,10,15}) send(second);
#endif
    Check(channel.queries[1].tx_position().x_meters == 1150 &&
          channel.queries[2].tx_position().x_meters == 1300 &&
          channel.queries[3].tx_position().x_meters == 1150,"nonlinear waypoint course used");
    WorldStateStore world{snapshot};
    CommitService commit{world};
    Check(commit.CommitCycle(SnapshotVersion{0},Take(working.FinalizeDeltaSet(T(10)))).has_value(),"commit");
    Check(world.current_snapshot().FindNode(NodeId{1})->get().motion.position.x_meters == 1300,
          "next topology snapshot must contain trajectory position");
    Check(!working.ProjectNodeState(NodeId{1},T(21)),"runtime out-of-coverage rejection");
    std::cout << "Acceptance4Node key times preserved; imported turning track drives Bellhop and committed geometry PASS\n";
    return 0;
  } catch(const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
