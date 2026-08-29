#include <cstdlib>
#include <iostream>
#include <string>

#include <ns3_factory/worker/codec/json_codec.hpp>

namespace {

using namespace ns3_factory;

auto Emit(const worker::WorkerMessage& message) -> bool {
  const auto encoded = worker::codec::EncodeWorkerMessage(message);
  if(!encoded) return false;
  std::cout << *encoded << '\n';
  return static_cast<bool>(std::cout);
}

}  // namespace

auto main() -> int {
  auto run = application::RunId::Create("golden-run");
  auto scenario = application::ScenarioId::Create("golden-scenario");
  auto experiment = application::ExperimentId::Create("golden-experiment");
  if(!run || !scenario || !experiment) return EXIT_FAILURE;

  const auto zero = contracts::SimTime::Zero();
  const auto trace = contracts::TraceEvent::Create(
      zero,
      contracts::CycleCommitTrace{contracts::PlanningCycleId{0},
                                  contracts::SnapshotVersion{0},
                                  contracts::SnapshotVersion{1},
                                  zero});
  if(!trace) return EXIT_FAILURE;

  const application::RunRecord record{
      *run,
      {*experiment, 1},
      {*scenario, 1},
      {"golden-asset", 1},
      application::RunLifecycle::kCompleted,
      zero,
      zero,
      contracts::SnapshotVersion{1},
      std::nullopt,
      true};
  const application::RunResult result{
      *run,
      {zero,
       zero,
       contracts::SimDuration::FromNanoseconds(0),
       contracts::SnapshotVersion{1},
       0,
       4,
       0,
       0,
       0,
       0,
       0},
      std::nullopt,
      {},
      {}};

  return Emit(worker::WorkerStarted{*run}) &&
                 Emit(worker::WorkerRunEvent{
                     {*run, application::RunEventSequence{1}, *trace}}) &&
                 Emit(worker::WorkerCompleted{record, result}) &&
                 Emit(worker::WorkerFailed{
                     *run,
                     worker::WorkerFailureCategory::kSimulation,
                     {contracts::ErrorCode::kUnavailable,
                      "golden owned failure"},
                     std::nullopt})
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
