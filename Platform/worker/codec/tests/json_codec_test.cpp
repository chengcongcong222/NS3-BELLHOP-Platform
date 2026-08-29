#include <cstdlib>
#include <iostream>
#include <string>
#include <variant>

#include <ns3_factory/worker/codec/json_codec.hpp>

namespace {

using namespace ns3_factory;

auto MakeCommand() -> contracts::Result<worker::StartRunCommand> {
  auto run = application::RunId::Create("wire-codec-run");
  auto scenario = application::ScenarioId::Create("wire-scenario");
  auto experiment = application::ExperimentId::Create("wire-experiment");
  if(!run || !scenario || !experiment) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kInternal,
                         "test ID creation failed"});
  }
  return worker::StartRunCommand{
      std::move(*run), std::move(*scenario), std::move(*experiment),
      9'007'199'254'740'993ULL,
      {"asset-v1", 1},
      application::AcceptanceProfile::kAcceptance4Node,
      2,
      application::RxQualityMode::kModeledBpskAwgn,
      45.0,
      18'446'744'073'709'551'614ULL};
}

auto TestCommandRoundTripAndNumericIntegrity() -> bool {
  auto command = MakeCommand();
  if(!command) return false;
  auto encoded = worker::codec::EncodeStartRunCommand(*command);
  auto decoded = encoded
                     ? worker::codec::DecodeStartRunCommand(*encoded)
                     : contracts::Result<worker::StartRunCommand>{
                           std::unexpected(encoded.error())};
  return encoded && decoded && *decoded == *command &&
         encoded->find("\"9007199254740993\"") != std::string::npos &&
         encoded->find("\"18446744073709551614\"") !=
             std::string::npos;
}

auto TestMalformedInputs() -> bool {
  const auto invalid = worker::codec::DecodeStartRunCommand("{");
  const auto missing = worker::codec::DecodeStartRunCommand(
      R"({"schema_version":1,"type":"StartRunCommand"})");
  const auto schema = worker::codec::DecodeStartRunCommand(
      R"({"schema_version":2,"type":"StartRunCommand","run_id":"x","preset":{},"environment":{},"execution":{}})");
  const auto unknown = worker::codec::DecodeStartRunCommand(
      R"({"schema_version":1,"type":"Unknown","run_id":"x","preset":{},"environment":{},"execution":{}})");
  const auto bad_id = worker::codec::DecodeStartRunCommand(
      R"({"schema_version":1,"type":"StartRunCommand","run_id":"bad/id","preset":{"scenario_id":"s","experiment_id":"e","definition_version":"1","acceptance_profile":"Acceptance4Node"},"environment":{"asset_id":"asset","asset_format_version":"1"},"execution":{"simulation_cycle_count":"1","rx_quality_mode":"None","equivalent_noise_power_db_re_1upa2":45.0,"deterministic_seed":"1"}})");
  const auto bad_asset = worker::codec::DecodeStartRunCommand(
      R"({"schema_version":1,"type":"StartRunCommand","run_id":"run","preset":{"scenario_id":"s","experiment_id":"e","definition_version":"1","acceptance_profile":"Acceptance4Node"},"environment":{"asset_id":"bad/asset","asset_format_version":"1"},"execution":{"simulation_cycle_count":"1","rx_quality_mode":"None","equivalent_noise_power_db_re_1upa2":45.0,"deterministic_seed":"1"}})");
  std::string oversized(worker::codec::kMaximumInputLineBytes + 1U, 'x');
  const auto too_large = worker::codec::DecodeStartRunCommand(oversized);
  return !invalid && !missing && !schema && !unknown && !bad_id &&
         !bad_asset &&
         !too_large;
}

auto TestWorkerMessageRoundTrip() -> bool {
  auto run = application::RunId::Create("wire-message-run");
  if(!run) return false;
  worker::WorkerMessage started = worker::WorkerStarted{*run};
  auto encoded_started = worker::codec::EncodeWorkerMessage(started);
  auto decoded_started = encoded_started
                             ? worker::codec::DecodeWorkerMessage(
                                   *encoded_started)
                             : contracts::Result<worker::WorkerMessage>{
                                   std::unexpected(encoded_started.error())};
  worker::WorkerMessage failed = worker::WorkerFailed{
      *run,
      worker::WorkerFailureCategory::kSimulation,
      {contracts::ErrorCode::kUnavailable, "owned failure message"},
      std::nullopt};
  auto encoded_failed = worker::codec::EncodeWorkerMessage(failed);
  auto decoded_failed = encoded_failed
                            ? worker::codec::DecodeWorkerMessage(
                                  *encoded_failed)
                            : contracts::Result<worker::WorkerMessage>{
                                  std::unexpected(encoded_failed.error())};
  const auto unknown = worker::codec::DecodeWorkerMessage(
      R"({"schema_version":1,"type":"Unknown"})");
  if(!decoded_started) std::cerr << decoded_started.error().message << '\n';
  if(decoded_started && *decoded_started != started)
    std::cerr << "started mismatch: " << *encoded_started << '\n';
  if(!decoded_failed) std::cerr << decoded_failed.error().message << '\n';
  if(decoded_failed && *decoded_failed != failed)
    std::cerr << "failed mismatch: " << *encoded_failed << '\n';
  if(unknown) std::cerr << "unknown accepted\n";
  return decoded_started && *decoded_started == started &&
         decoded_failed && *decoded_failed == failed && !unknown;
}

auto TestEventIntegerExactness() -> bool {
  auto run = application::RunId::Create("wire-large-integers");
  if(!run) return false;
  constexpr std::uint64_t kLarge = 9'007'199'254'740'993ULL;
  const auto started = contracts::SimTime::FromNanoseconds(
      9'007'199'254'740'993LL);
  const auto ended = contracts::SimTime::FromNanoseconds(
      9'007'199'254'740'994LL);
  auto trace = contracts::TraceEvent::Create(
      started,
      contracts::TransmissionTrace{
          contracts::TransmissionId{kLarge},
          contracts::PacketId{kLarge + 1U},
          contracts::NodeId{kLarge + 2U},
          contracts::TraceBroadcastTransmissionTarget{},
          started,
          ended});
  if(!trace) return false;
  worker::WorkerMessage event = worker::WorkerRunEvent{
      {*run, application::RunEventSequence{kLarge}, *trace}};
  auto encoded = worker::codec::EncodeWorkerMessage(event);
  auto decoded = encoded
                     ? worker::codec::DecodeWorkerMessage(*encoded)
                     : contracts::Result<worker::WorkerMessage>{
                           std::unexpected(encoded.error())};
  return encoded && decoded && *decoded == event &&
         encoded->find("9007199254740993") != std::string::npos;
}

}  // namespace

auto main() -> int {
  const auto command = TestCommandRoundTripAndNumericIntegrity();
  const auto malformed = TestMalformedInputs();
  const auto messages = TestWorkerMessageRoundTrip();
  const auto integers = TestEventIntegerExactness();
  if(!command) std::cerr << "command round-trip failed\n";
  if(!malformed) std::cerr << "malformed gate failed\n";
  if(!messages) std::cerr << "message round-trip failed\n";
  if(!integers) std::cerr << "event integer exactness failed\n";
  return command && malformed && messages && integers
             ? EXIT_SUCCESS
             : EXIT_FAILURE;
}
