#include <iostream>

#include <ns3_factory/worker/codec/json_codec.hpp>

auto main() -> int {
  auto run = ns3_factory::application::RunId::Create("wire-process-run");
  if(!run) return 127;
  ns3_factory::worker::WorkerMessage message =
      ns3_factory::worker::WorkerFailed{
          *run,
          ns3_factory::worker::WorkerFailureCategory::kSimulation,
          {ns3_factory::contracts::ErrorCode::kInternal,
           "intentional fixture failure"},
          std::nullopt};
  auto encoded = ns3_factory::worker::codec::EncodeWorkerMessage(message);
  if(!encoded) return 127;
  std::cout << *encoded << '\n';
  return 0;
}
