#pragma once

#include "../trajectory/include/ns3_factory/trajectory/generator.hpp"
#include "../runtime/internal/cycle_working_state.hpp"

namespace ns3_factory::test_support {
using namespace contracts;

// Migration helper for historical tests ONLY. UpdateVelocity edits a test's
// offline piecewise-linear resource, never a runtime motion model/event.
class TrajectoryWorkingState {
 public:
  static auto Create(const WorldSnapshot& snapshot, PlanningCycleId cycle, SimTime start)
      -> Result<TrajectoryWorkingState> {
    const auto end = CheckedAdd(start, SimDuration::FromNanoseconds(3'600'000'000'000));
    if(!end) return std::unexpected(Error{ErrorCode::kOverflow,"Test trajectory coverage overflow"});
    auto generated = trajectory::GenerateConstantVelocity(snapshot,*end,"test-tracks",1);
    if(!generated) return std::unexpected(generated.error());
    TrajectoryWorkingState result{snapshot,cycle,start,*end};
    for(const auto& track : generated->tracks()) result.tracks_.push_back(track);
    auto rebuilt = result.Rebuild();
    if(!rebuilt) return std::unexpected(rebuilt.error());
    return result;
  }
  operator const runtime::internal::CycleWorkingState&() const { return *working_; }
  operator runtime::internal::CycleWorkingState&() { return *working_; }
  auto base_snapshot() const -> const WorldSnapshot& { return working_->base_snapshot(); }
  auto cycle_id() const -> PlanningCycleId { return cycle_; }
  auto cycle_started_at() const -> SimTime { return start_; }
  auto ProjectNodeState(NodeId node,SimTime time) const -> Result<NodeCommittedState> {
    return working_->ProjectNodeState(node,time);
  }
  auto FinalizeDeltaSet(SimTime time) const -> Result<DeltaSet> { return working_->FinalizeDeltaSet(time); }
  auto UpdateVelocity(NodeId node,Velocity3d velocity,SimTime time) -> Status {
    auto state = working_->ProjectNodeState(node,time);
    if(!state) return std::unexpected(state.error());
    auto it = std::find_if(tracks_.begin(),tracks_.end(),[&](const trajectory::Track& t) {
      return t.id == "N"+std::to_string(node.value()); });
    const auto seconds = static_cast<double>(end_.nanoseconds()-time.nanoseconds())/1e9;
    auto& points = it->points;
    while(!points.empty() && points.back().time >= time) points.pop_back();
    points.push_back({time,state->motion.position});
    const auto p = state->motion.position;
    points.push_back({end_,{p.x_meters+velocity.x_meters_per_second*seconds,
                            p.y_meters+velocity.y_meters_per_second*seconds,
                            p.z_meters+velocity.z_meters_per_second*seconds}});
    return Rebuild();
  }
 private:
  TrajectoryWorkingState(WorldSnapshot snapshot,PlanningCycleId cycle,SimTime start,SimTime end)
      : snapshot_(std::move(snapshot)),cycle_(cycle),start_(start),end_(end) {}
  auto Rebuild() -> Status {
    auto asset = trajectory::Asset::Create("test-tracks",1,
        trajectory::CoordinateSystem::kLocalCartesianMetersZUp,snapshot_.committed_at(),end_,"test-fixture",tracks_);
    if(!asset) return std::unexpected(asset.error());
    trajectory::Repository repository;
    auto registered = repository.Register(std::move(*asset));
    if(!registered) return registered;
    std::vector<trajectory::NodeBinding> bindings;
    for(const auto& node : snapshot_.nodes()) bindings.push_back({node.node_id,{"test-tracks",1,"N"+std::to_string(node.node_id.value())}});
    auto bound = trajectory::BoundTrajectories::Create(repository,std::move(bindings));
    if(!bound) return std::unexpected(bound.error());
    auto created = runtime::internal::CycleWorkingState::Create(snapshot_,cycle_,start_,*bound);
    if(!created) return std::unexpected(created.error());
    working_.emplace(std::move(*created));
    return {};
  }
  WorldSnapshot snapshot_;
  PlanningCycleId cycle_;
  SimTime start_,end_;
  std::vector<trajectory::Track> tracks_;
  std::optional<runtime::internal::CycleWorkingState> working_;
};
}  // namespace ns3_factory::test_support
