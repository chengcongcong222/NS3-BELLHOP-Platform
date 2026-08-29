#include <iostream>

#include <ns3_factory/worker/codec/json_codec.hpp>

auto main() -> int {
  using namespace ns3_factory;
  auto run = application::RunId::Create("wire-process-run");
  auto scenario = application::ScenarioId::Create("fixture-scenario");
  auto experiment = application::ExperimentId::Create("fixture-experiment");
  if(!run || !scenario || !experiment) return 127;
  const auto zero = contracts::SimTime::Zero();
  const auto one = contracts::SimTime::FromNanoseconds(1);
  application::RunRecord record{
      *run,
      {*experiment, 1},
      {*scenario, 1},
      {"fixture-asset", 1},
      application::RunLifecycle::kCompleted,
      zero,
      one,
      contracts::SnapshotVersion{1},
      std::nullopt,
      true};
  application::RunResult result{
      *run,
      {zero, one, contracts::SimDuration::FromNanoseconds(1),
       contracts::SnapshotVersion{1}, 0, 0, 0, 0, 0, 0, 0},
      std::nullopt,
      {},
      {}};
  for(const worker::WorkerMessage& message : {
          worker::WorkerMessage{worker::WorkerStarted{*run}},
          worker::WorkerMessage{worker::WorkerCompleted{record, result}}}) {
    auto encoded = worker::codec::EncodeWorkerMessage(message);
    if(!encoded) return 127;
    std::cout << *encoded << '\n';
  }
  return 3;
}
