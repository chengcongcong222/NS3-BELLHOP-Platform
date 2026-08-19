#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ComputeP0ScalarReceivedLevelDbRe1upa;
using ns3_factory::contracts::CreateNoiseQueryForDesiredSignal;
using ns3_factory::contracts::DecodeOutcome;
using ns3_factory::contracts::INoiseFieldProvider;
using ns3_factory::contracts::IRxPhy;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::NoiseObservation;
using ns3_factory::contracts::NoiseQuery;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::PropagationPath;
using ns3_factory::contracts::ReceivedSignal;
using ns3_factory::contracts::ReceiverWindow;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::RxDecodeRequest;
using ns3_factory::contracts::RxDecodeResult;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TxEmission;
using ns3_factory::contracts::ValidateNoiseObservationIdentity;
using ns3_factory::contracts::ValidateRxDecodeResultIdentity;

template <typename T>
concept HasTransmissionIdentity =
    requires(T value) { value.transmission_id; } ||
    requires(const T value) { value.transmission_id(); };

template <typename T>
concept HasReceptionIdentity =
    requires(T value) { value.reception_id; } ||
    requires(const T value) { value.reception_id(); };

template <typename T>
concept HasPacketErrorRate =
    requires(T value) { value.packet_error_rate; } ||
    requires(const T value) { value.packet_error_rate(); };

template <typename T>
concept HasSnr = requires(T value) { value.snr_db; } ||
                 requires(const T value) { value.snr_db(); };

template <typename T>
concept HasSinr = requires(T value) { value.sinr_db; } ||
                  requires(const T value) { value.sinr_db(); };

template <typename T>
concept HasSchedulerApi = requires { &T::Schedule; } ||
                          requires { &T::ScheduleAt; } ||
                          requires { &T::ScheduleAfter; };

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

static_assert(!HasTransmissionIdentity<NoiseQuery>);
static_assert(!HasTransmissionIdentity<NoiseObservation>);
static_assert(std::is_enum_v<DecodeOutcome>);
static_assert(!std::same_as<DecodeOutcome, bool>);
static_assert(std::same_as<std::underlying_type_t<DecodeOutcome>,
                           std::uint8_t>);
static_assert(static_cast<std::uint8_t>(DecodeOutcome::kDecoded) == 1);
static_assert(static_cast<std::uint8_t>(DecodeOutcome::kNotDecoded) == 2);
static_assert(!HasReceptionIdentity<RxDecodeResult>);
// Optional packet diagnostics are deliberately not frozen in P0-S0-04B2B;
// unavailable values therefore have no NaN sentinel representation.
static_assert(!HasPacketErrorRate<RxDecodeResult>);
static_assert(!HasSnr<RxDecodeResult>);
static_assert(!HasSinr<RxDecodeResult>);
static_assert(!HasSchedulerApi<IRxPhy> && !HasCommitMethod<IRxPhy> &&
              !HasApplyMethod<IRxPhy>);
static_assert(!HasSchedulerApi<INoiseFieldProvider> &&
              !HasCommitMethod<INoiseFieldProvider> &&
              !HasApplyMethod<INoiseFieldProvider>);
static_assert(std::has_virtual_destructor_v<IRxPhy>);
static_assert(std::has_virtual_destructor_v<INoiseFieldProvider>);
static_assert(requires(const INoiseFieldProvider& provider,
                       const NoiseQuery& query) {
  { provider.Query(query) } -> std::same_as<Result<NoiseObservation>>;
});
static_assert(requires(const IRxPhy& phy,
                       const RxDecodeRequest& request) {
  { phy.Decode(request) } -> std::same_as<Result<RxDecodeResult>>;
});

[[nodiscard]] auto MakeSignal(
    TransmissionId transmission_id,
    PacketId packet_id,
    NodeId receiver_node_id,
    SimTime started_at,
    SimDuration duration,
    double center_frequency_hz,
    double bandwidth_hz,
    double aggregate_transmission_loss_db,
    std::vector<PropagationPath> paths = {}) -> Result<ReceivedSignal> {
  auto emission = TxEmission::Create(transmission_id,
                                     packet_id,
                                     NodeId{0},
                                     started_at,
                                     duration,
                                     center_frequency_hz,
                                     bandwidth_hz,
                                     180.0);
  if(!emission) {
    return std::unexpected(emission.error());
  }
  auto response = ChannelFieldResponse::Create(
      transmission_id,
      receiver_node_id,
      aggregate_transmission_loss_db,
      SimDuration::Zero(),
      std::move(paths));
  if(!response) {
    return std::unexpected(response.error());
  }
  return ReceivedSignal::Create(*emission, *response);
}

class FixtureNoiseProvider final : public INoiseFieldProvider {
 public:
  [[nodiscard]] auto Query(const NoiseQuery& query) const
      -> Result<NoiseObservation> override {
    return NoiseObservation::Create(
        query.receiver_node_id(),
        query.observed_from(),
        query.observed_until(),
        query.lower_frequency_hz(),
        query.upper_frequency_hz(),
        -5.0);
  }
};

class DeterministicMockRxPhy final : public IRxPhy {
 public:
  [[nodiscard]] auto Decode(const RxDecodeRequest& request) const
      -> Result<RxDecodeResult> override {
    const auto& window = request.receiver_window();
    auto desired_level =
        ComputeP0ScalarReceivedLevelDbRe1upa(window.desired_signal());
    if(!desired_level) {
      return std::unexpected(desired_level.error());
    }
    for(const auto& overlap : window.overlapping_signals()) {
      auto overlap_level = ComputeP0ScalarReceivedLevelDbRe1upa(overlap);
      if(!overlap_level) {
        return std::unexpected(overlap_level.error());
      }
    }

    const auto outcome = window.overlapping_signals().empty()
                             ? DecodeOutcome::kDecoded
                             : DecodeOutcome::kNotDecoded;
    const auto& desired = window.desired_signal();
    return RxDecodeResult::Create(desired.transmission_id(),
                                  desired.emission().packet_id(),
                                  window.receiver_node_id(),
                                  outcome);
  }
};

auto main() -> int {
  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  constexpr auto kReceiverPosition = Position3d{1.0, 2.0, -3.0};

  auto query = NoiseQuery::Create(NodeId{1},
                                  kReceiverPosition,
                                  SimTime::FromNanoseconds(100),
                                  SimTime::FromNanoseconds(200),
                                  90.0,
                                  110.0);
  if(!query || query->receiver_node_id() != NodeId{1} ||
     query->receiver_position() != kReceiverPosition ||
     query->observed_from() != SimTime::FromNanoseconds(100) ||
     query->observed_until() != SimTime::FromNanoseconds(200) ||
     query->lower_frequency_hz() != 90.0 ||
     query->upper_frequency_hz() != 110.0) {
    return EXIT_FAILURE;
  }

  if(NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(100),
                        90.0,
                        110.0) ||
     NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(101),
                        SimTime::FromNanoseconds(100),
                        90.0,
                        110.0) ||
     NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(200),
                        -1.0,
                        110.0) ||
     NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(200),
                        110.0,
                        110.0) ||
     NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(200),
                        nan,
                        110.0) ||
     NoiseQuery::Create(NodeId{1},
                        kReceiverPosition,
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(200),
                        90.0,
                        infinity) ||
     NoiseQuery::Create(NodeId{1},
                        Position3d{nan, 0.0, 0.0},
                        SimTime::FromNanoseconds(100),
                        SimTime::FromNanoseconds(200),
                        90.0,
                        110.0)) {
    return EXIT_FAILURE;
  }

  const FixtureNoiseProvider noise_provider;
  auto observation = noise_provider.Query(*query);
  if(!observation ||
     observation->equivalent_noise_power_db_re_1upa2() != -5.0 ||
     !ValidateNoiseObservationIdentity(*query, *observation)) {
    return EXIT_FAILURE;
  }
  if(NoiseObservation::Create(NodeId{1},
                              SimTime::FromNanoseconds(100),
                              SimTime::FromNanoseconds(200),
                              90.0,
                              110.0,
                              nan) ||
     NoiseObservation::Create(NodeId{1},
                              SimTime::FromNanoseconds(100),
                              SimTime::FromNanoseconds(200),
                              90.0,
                              110.0,
                              infinity) ||
     NoiseObservation::Create(NodeId{1},
                              SimTime::FromNanoseconds(100),
                              SimTime::FromNanoseconds(200),
                              90.0,
                              110.0,
                              -infinity)) {
    return EXIT_FAILURE;
  }

  auto receiver_mismatch = NoiseObservation::Create(
      NodeId{2},
      query->observed_from(),
      query->observed_until(),
      query->lower_frequency_hz(),
      query->upper_frequency_hz(),
      -5.0);
  auto time_mismatch = NoiseObservation::Create(
      query->receiver_node_id(),
      SimTime::FromNanoseconds(101),
      query->observed_until(),
      query->lower_frequency_hz(),
      query->upper_frequency_hz(),
      -5.0);
  auto frequency_mismatch = NoiseObservation::Create(
      query->receiver_node_id(),
      query->observed_from(),
      query->observed_until(),
      91.0,
      query->upper_frequency_hz(),
      -5.0);
  if(!receiver_mismatch || !time_mismatch || !frequency_mismatch ||
     ValidateNoiseObservationIdentity(*query, *receiver_mismatch) ||
     ValidateNoiseObservationIdentity(*query, *time_mismatch) ||
     ValidateNoiseObservationIdentity(*query, *frequency_mismatch)) {
    return EXIT_FAILURE;
  }

  auto first_path =
      PropagationPath::Create(SimDuration::Zero(), 100.0, 1.0);
  auto delayed_path = PropagationPath::Create(
      SimDuration::FromNanoseconds(5), 0.01, -1.0);
  if(!first_path || !delayed_path) {
    return EXIT_FAILURE;
  }
  auto desired = MakeSignal(
      TransmissionId{10},
      PacketId{20},
      NodeId{1},
      SimTime::Zero(),
      SimDuration::FromNanoseconds(10),
      100.0,
      20.0,
      70.0,
      std::vector<PropagationPath>{*delayed_path, *first_path});
  auto overlap = MakeSignal(TransmissionId{11},
                            PacketId{21},
                            NodeId{1},
                            SimTime::FromNanoseconds(5),
                            SimDuration::FromNanoseconds(10),
                            105.0,
                            20.0,
                            80.0);
  if(!desired || !overlap) {
    return EXIT_FAILURE;
  }

  const auto desired_scalar_level =
      ComputeP0ScalarReceivedLevelDbRe1upa(*desired);
  const auto overlap_scalar_level =
      ComputeP0ScalarReceivedLevelDbRe1upa(*overlap);
  if(!desired_scalar_level || *desired_scalar_level != 110.0 ||
     !overlap_scalar_level || *overlap_scalar_level != 100.0 ||
     desired->channel_response().paths().size() != 2 ||
     desired->channel_response().paths()[0].pressure_gain_linear() !=
         100.0) {
    return EXIT_FAILURE;
  }

  auto empty_window = ReceiverWindow::Create(NodeId{1}, *desired, {});
  auto overlap_window = ReceiverWindow::Create(
      NodeId{1}, *desired, std::vector<ReceivedSignal>{*overlap});
  if(!empty_window || !overlap_window ||
     !empty_window->overlapping_signals().empty() ||
     overlap_window->overlapping_signals().size() != 1) {
    return EXIT_FAILURE;
  }

  auto desired_noise_query =
      CreateNoiseQueryForDesiredSignal(*overlap_window, kReceiverPosition);
  if(!desired_noise_query ||
     desired_noise_query->receiver_node_id() != NodeId{1} ||
     desired_noise_query->observed_from() !=
         desired->first_arrival_at() ||
     desired_noise_query->observed_until() !=
         desired->last_effect_end_at() ||
     desired_noise_query->lower_frequency_hz() !=
         desired->lower_frequency_hz() ||
     desired_noise_query->upper_frequency_hz() !=
         desired->upper_frequency_hz()) {
    return EXIT_FAILURE;
  }
  auto desired_noise = noise_provider.Query(*desired_noise_query);
  if(!desired_noise ||
     !ValidateNoiseObservationIdentity(*desired_noise_query,
                                       *desired_noise)) {
    return EXIT_FAILURE;
  }

  auto decode_request =
      RxDecodeRequest::Create(*overlap_window, *desired_noise);
  auto empty_noise_query =
      CreateNoiseQueryForDesiredSignal(*empty_window, kReceiverPosition);
  if(!decode_request || !empty_noise_query) {
    return EXIT_FAILURE;
  }
  auto empty_noise = noise_provider.Query(*empty_noise_query);
  if(!empty_noise) {
    return EXIT_FAILURE;
  }
  auto empty_decode_request =
      RxDecodeRequest::Create(*empty_window, *empty_noise);
  if(!empty_decode_request) {
    return EXIT_FAILURE;
  }

  auto request_receiver_mismatch = NoiseObservation::Create(
      NodeId{2},
      desired->first_arrival_at(),
      desired->last_effect_end_at(),
      desired->lower_frequency_hz(),
      desired->upper_frequency_hz(),
      -5.0);
  auto request_time_mismatch = NoiseObservation::Create(
      NodeId{1},
      desired->first_arrival_at(),
      SimTime::FromNanoseconds(
          desired->last_effect_end_at().nanoseconds() - 1),
      desired->lower_frequency_hz(),
      desired->upper_frequency_hz(),
      -5.0);
  auto request_frequency_mismatch = NoiseObservation::Create(
      NodeId{1},
      desired->first_arrival_at(),
      desired->last_effect_end_at(),
      desired->lower_frequency_hz() + 1.0,
      desired->upper_frequency_hz(),
      -5.0);
  if(!request_receiver_mismatch || !request_time_mismatch ||
     !request_frequency_mismatch ||
     RxDecodeRequest::Create(*overlap_window,
                             *request_receiver_mismatch) ||
     RxDecodeRequest::Create(*overlap_window, *request_time_mismatch) ||
     RxDecodeRequest::Create(*overlap_window,
                             *request_frequency_mismatch)) {
    return EXIT_FAILURE;
  }

  auto zero_width_signal = MakeSignal(TransmissionId{12},
                                      PacketId{22},
                                      NodeId{1},
                                      SimTime::Zero(),
                                      SimDuration::Zero(),
                                      100.0,
                                      20.0,
                                      70.0);
  if(!zero_width_signal) {
    return EXIT_FAILURE;
  }
  auto zero_width_window =
      ReceiverWindow::Create(NodeId{1}, *zero_width_signal, {});
  if(!zero_width_window ||
     CreateNoiseQueryForDesiredSignal(*zero_width_window,
                                      kReceiverPosition)) {
    return EXIT_FAILURE;
  }
  auto arbitrary_noise = NoiseObservation::Create(
      NodeId{1},
      SimTime::Zero(),
      SimTime::FromNanoseconds(1),
      90.0,
      110.0,
      -5.0);
  if(!arbitrary_noise ||
     RxDecodeRequest::Create(*zero_width_window, *arbitrary_noise)) {
    return EXIT_FAILURE;
  }

  const DeterministicMockRxPhy rx_phy;
  auto decode_result_a = rx_phy.Decode(*decode_request);
  auto decode_result_b = rx_phy.Decode(*decode_request);
  auto empty_decode_result = rx_phy.Decode(*empty_decode_request);
  if(!decode_result_a || !decode_result_b || !empty_decode_result ||
     *decode_result_a != *decode_result_b ||
     decode_result_a->outcome() != DecodeOutcome::kNotDecoded ||
     empty_decode_result->outcome() != DecodeOutcome::kDecoded ||
     !ValidateRxDecodeResultIdentity(*decode_request, *decode_result_a)) {
    return EXIT_FAILURE;
  }

  if(RxDecodeResult::Create(
         desired->transmission_id(),
         desired->emission().packet_id(),
         desired->receiver_node_id(),
         static_cast<DecodeOutcome>(0))) {
    return EXIT_FAILURE;
  }
  auto wrong_transmission_result = RxDecodeResult::Create(
      TransmissionId{999},
      desired->emission().packet_id(),
      desired->receiver_node_id(),
      DecodeOutcome::kDecoded);
  auto wrong_packet_result = RxDecodeResult::Create(
      desired->transmission_id(),
      PacketId{999},
      desired->receiver_node_id(),
      DecodeOutcome::kDecoded);
  auto wrong_receiver_result = RxDecodeResult::Create(
      desired->transmission_id(),
      desired->emission().packet_id(),
      NodeId{999},
      DecodeOutcome::kDecoded);
  if(!wrong_transmission_result || !wrong_packet_result ||
     !wrong_receiver_result ||
     ValidateRxDecodeResultIdentity(*decode_request,
                                    *wrong_transmission_result) ||
     ValidateRxDecodeResultIdentity(*decode_request,
                                    *wrong_packet_result) ||
     ValidateRxDecodeResultIdentity(*decode_request,
                                    *wrong_receiver_result)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
