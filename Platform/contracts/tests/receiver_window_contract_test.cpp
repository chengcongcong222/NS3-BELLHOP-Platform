#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::HasP0SignalOverlap;
using ns3_factory::contracts::HasSpectralOverlap;
using ns3_factory::contracts::HasTemporalOverlap;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::PropagationPath;
using ns3_factory::contracts::ReceivedSignal;
using ns3_factory::contracts::ReceiverWindow;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TxEmission;

template <typename T>
concept HasReceptionIdentity =
    requires(T value) { value.reception_id; } ||
    requires(const T value) { value.reception_id(); };

template <typename T>
concept HasDecodeApi = requires(T value) { value.Decode(); };

template <typename T>
concept HasNoiseData =
    requires(T value) { value.noise; } ||
    requires(const T value) { value.noise(); };

template <typename T>
concept HasSinrData =
    requires(T value) { value.sinr; } ||
    requires(const T value) { value.sinr(); };

static_assert(std::copy_constructible<ReceivedSignal>);
static_assert(std::copy_constructible<ReceiverWindow>);
static_assert(!HasReceptionIdentity<ReceivedSignal>);
static_assert(!HasReceptionIdentity<ReceiverWindow>);
static_assert(!HasDecodeApi<ReceivedSignal>);
static_assert(!HasDecodeApi<ReceiverWindow>);
static_assert(!HasNoiseData<ReceivedSignal>);
static_assert(!HasNoiseData<ReceiverWindow>);
static_assert(!HasSinrData<ReceivedSignal>);
static_assert(!HasSinrData<ReceiverWindow>);
static_assert(std::same_as<
              decltype(std::declval<const ReceivedSignal&>().emission()),
              const TxEmission&>);
static_assert(std::same_as<
              decltype(std::declval<const ReceivedSignal&>()
                           .channel_response()),
              const ChannelFieldResponse&>);

[[nodiscard]] auto MakeScalarSignal(TransmissionId transmission_id,
                                    NodeId receiver_node_id,
                                    SimTime started_at,
                                    SimDuration duration,
                                    SimDuration first_arrival_delay,
                                    double center_frequency_hz,
                                    double bandwidth_hz)
    -> Result<ReceivedSignal> {
  auto emission = TxEmission::Create(transmission_id,
                                     PacketId{transmission_id.value()},
                                     NodeId{0},
                                     started_at,
                                     duration,
                                     center_frequency_hz,
                                     bandwidth_hz,
                                     180.0);
  if(!emission) {
    return std::unexpected(emission.error());
  }
  auto response = ChannelFieldResponse::Create(transmission_id,
                                               receiver_node_id,
                                               70.0,
                                               first_arrival_delay,
                                               {});
  if(!response) {
    return std::unexpected(response.error());
  }
  return ReceivedSignal::Create(*emission, *response);
}

auto main() -> int {
  auto identity_emission = TxEmission::Create(
      TransmissionId{1},
      PacketId{1},
      NodeId{0},
      SimTime::Zero(),
      SimDuration::FromNanoseconds(10),
      100.0,
      20.0,
      180.0);
  auto mismatched_response = ChannelFieldResponse::Create(
      TransmissionId{2},
      NodeId{1},
      70.0,
      SimDuration::Zero(),
      {});
  if(!identity_emission || !mismatched_response) {
    return EXIT_FAILURE;
  }
  const auto identity_mismatch =
      ReceivedSignal::Create(*identity_emission, *mismatched_response);
  if(identity_mismatch ||
     identity_mismatch.error().code != ErrorCode::kFailedPrecondition) {
    return EXIT_FAILURE;
  }

  auto scalar_signal = MakeScalarSignal(
      TransmissionId{3},
      NodeId{1},
      SimTime::FromNanoseconds(100),
      SimDuration::FromNanoseconds(30),
      SimDuration::FromNanoseconds(20),
      100.0,
      20.0);
  if(!scalar_signal ||
     scalar_signal->first_arrival_at() !=
         SimTime::FromNanoseconds(120) ||
     scalar_signal->latest_excess_delay() != SimDuration::Zero() ||
     scalar_signal->last_effect_end_at() !=
         SimTime::FromNanoseconds(150) ||
     scalar_signal->lower_frequency_hz() != 90.0 ||
     scalar_signal->upper_frequency_hz() != 110.0 ||
     !scalar_signal->channel_response().paths().empty()) {
    return EXIT_FAILURE;
  }

  auto path_zero =
      PropagationPath::Create(SimDuration::Zero(), 1.0, 0.0);
  auto path_thirty_ms = PropagationPath::Create(
      SimDuration::FromNanoseconds(30'000'000), 0.5, 0.25);
  auto path_one_hundred_ten_ms = PropagationPath::Create(
      SimDuration::FromNanoseconds(110'000'000), 0.25, 0.5);
  auto example_emission = TxEmission::Create(
      TransmissionId{4},
      PacketId{4},
      NodeId{0},
      SimTime::FromNanoseconds(10'000'000'000),
      SimDuration::FromNanoseconds(100'000'000),
      25'000.0,
      4'000.0,
      180.0);
  if(!path_zero || !path_thirty_ms || !path_one_hundred_ten_ms ||
     !example_emission) {
    return EXIT_FAILURE;
  }
  auto example_response = ChannelFieldResponse::Create(
      TransmissionId{4},
      NodeId{1},
      72.5,
      SimDuration::FromNanoseconds(800'000'000),
      std::vector<PropagationPath>{*path_one_hundred_ten_ms,
                                   *path_zero,
                                   *path_thirty_ms});
  if(!example_response) {
    return EXIT_FAILURE;
  }
  auto multipath_signal =
      ReceivedSignal::Create(*example_emission, *example_response);
  if(!multipath_signal ||
     multipath_signal->first_arrival_at() !=
         SimTime::FromNanoseconds(10'800'000'000) ||
     multipath_signal->latest_excess_delay() !=
         SimDuration::FromNanoseconds(110'000'000) ||
     multipath_signal->last_effect_end_at() !=
         SimTime::FromNanoseconds(11'010'000'000) ||
     multipath_signal->channel_response().paths().size() != 3) {
    return EXIT_FAILURE;
  }

  constexpr auto kMaxNanoseconds =
      std::numeric_limits<SimTime::representation_type>::max();
  auto overflow_emission = TxEmission::Create(
      TransmissionId{5},
      PacketId{5},
      NodeId{0},
      SimTime::FromNanoseconds(kMaxNanoseconds),
      SimDuration::FromNanoseconds(1),
      100.0,
      20.0,
      180.0);
  auto overflow_response = ChannelFieldResponse::Create(
      TransmissionId{5},
      NodeId{1},
      70.0,
      SimDuration::FromNanoseconds(1),
      {});
  if(!overflow_emission || !overflow_response) {
    return EXIT_FAILURE;
  }
  const auto time_overflow =
      ReceivedSignal::Create(*overflow_emission, *overflow_response);
  if(time_overflow || time_overflow.error().code != ErrorCode::kOverflow) {
    return EXIT_FAILURE;
  }

  constexpr auto kMaximumDouble = std::numeric_limits<double>::max();
  auto frequency_overflow_emission = TxEmission::Create(
      TransmissionId{6},
      PacketId{6},
      NodeId{0},
      SimTime::Zero(),
      SimDuration::FromNanoseconds(1),
      kMaximumDouble,
      kMaximumDouble,
      180.0);
  auto frequency_overflow_response = ChannelFieldResponse::Create(
      TransmissionId{6},
      NodeId{1},
      70.0,
      SimDuration::Zero(),
      {});
  if(!frequency_overflow_emission || !frequency_overflow_response) {
    return EXIT_FAILURE;
  }
  const auto frequency_overflow = ReceivedSignal::Create(
      *frequency_overflow_emission, *frequency_overflow_response);
  if(frequency_overflow ||
     frequency_overflow.error().code != ErrorCode::kOverflow) {
    return EXIT_FAILURE;
  }

  auto desired = MakeScalarSignal(TransmissionId{100},
                                  NodeId{1},
                                  SimTime::FromNanoseconds(0),
                                  SimDuration::FromNanoseconds(10),
                                  SimDuration::Zero(),
                                  100.0,
                                  20.0);
  auto overlapping_b = MakeScalarSignal(TransmissionId{101},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(5),
                                        SimDuration::FromNanoseconds(10),
                                        SimDuration::Zero(),
                                        105.0,
                                        20.0);
  auto overlapping_g = MakeScalarSignal(TransmissionId{102},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(2),
                                        SimDuration::FromNanoseconds(5),
                                        SimDuration::Zero(),
                                        98.0,
                                        20.0);
  auto overlapping_h = MakeScalarSignal(TransmissionId{99},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(5),
                                        SimDuration::FromNanoseconds(4),
                                        SimDuration::Zero(),
                                        102.0,
                                        20.0);
  auto temporal_touch = MakeScalarSignal(TransmissionId{103},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(10),
                                        SimDuration::FromNanoseconds(10),
                                        SimDuration::Zero(),
                                        100.0,
                                        20.0);
  auto zero_duration = MakeScalarSignal(TransmissionId{107},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(5),
                                        SimDuration::Zero(),
                                        SimDuration::Zero(),
                                        100.0,
                                        20.0);
  auto spectral_touch = MakeScalarSignal(TransmissionId{104},
                                        NodeId{1},
                                        SimTime::FromNanoseconds(5),
                                        SimDuration::FromNanoseconds(10),
                                        SimDuration::Zero(),
                                        120.0,
                                        20.0);
  auto temporally_separate = MakeScalarSignal(
      TransmissionId{105},
      NodeId{1},
      SimTime::FromNanoseconds(20),
      SimDuration::FromNanoseconds(10),
      SimDuration::Zero(),
      100.0,
      20.0);
  auto wrong_receiver = MakeScalarSignal(TransmissionId{106},
                                         NodeId{2},
                                         SimTime::FromNanoseconds(5),
                                         SimDuration::FromNanoseconds(10),
                                         SimDuration::Zero(),
                                         105.0,
                                         20.0);
  if(!desired || !overlapping_b || !overlapping_g || !overlapping_h ||
     !temporal_touch || !zero_duration || !spectral_touch ||
     !temporally_separate || !wrong_receiver) {
    return EXIT_FAILURE;
  }

  if(!HasTemporalOverlap(*desired, *overlapping_b) ||
     HasTemporalOverlap(*desired, *temporal_touch) ||
     HasTemporalOverlap(*desired, *zero_duration) ||
     !HasSpectralOverlap(*desired, *overlapping_b) ||
     HasSpectralOverlap(*desired, *spectral_touch) ||
     HasP0SignalOverlap(*desired, *spectral_touch) ||
     HasP0SignalOverlap(*desired, *temporally_separate) ||
     !HasP0SignalOverlap(*desired, *overlapping_b) ||
     HasP0SignalOverlap(*desired, *wrong_receiver)) {
    return EXIT_FAILURE;
  }

  auto empty_window = ReceiverWindow::Create(NodeId{1}, *desired, {});
  if(!empty_window || !empty_window->overlapping_signals().empty()) {
    return EXIT_FAILURE;
  }

  auto window_a = ReceiverWindow::Create(
      NodeId{1},
      *desired,
      std::vector<ReceivedSignal>{*overlapping_b,
                                  *overlapping_h,
                                  *overlapping_g});
  auto window_b = ReceiverWindow::Create(
      NodeId{1},
      *desired,
      std::vector<ReceivedSignal>{*overlapping_g,
                                  *overlapping_b,
                                  *overlapping_h});
  if(!window_a || !window_b ||
     window_a->overlapping_signals().size() != 3 ||
     window_a->overlapping_signals().size() !=
         window_b->overlapping_signals().size()) {
    return EXIT_FAILURE;
  }
  for(std::size_t index = 0;
      index < window_a->overlapping_signals().size();
      ++index) {
    if(window_a->overlapping_signals()[index] !=
       window_b->overlapping_signals()[index]) {
      return EXIT_FAILURE;
    }
  }
  if(window_a->overlapping_signals()[0].transmission_id() !=
         TransmissionId{102} ||
     window_a->overlapping_signals()[1].transmission_id() !=
         TransmissionId{99} ||
     window_a->overlapping_signals()[2].transmission_id() !=
         TransmissionId{101}) {
    return EXIT_FAILURE;
  }

  if(ReceiverWindow::Create(
         NodeId{1},
         *desired,
         std::vector<ReceivedSignal>{*spectral_touch}) ||
     ReceiverWindow::Create(
         NodeId{1},
         *desired,
         std::vector<ReceivedSignal>{*temporal_touch}) ||
     ReceiverWindow::Create(
         NodeId{1},
         *desired,
         std::vector<ReceivedSignal>{*wrong_receiver}) ||
     ReceiverWindow::Create(NodeId{2}, *desired, {}) ||
     ReceiverWindow::Create(
         NodeId{1},
         *desired,
         std::vector<ReceivedSignal>{*desired}) ||
     ReceiverWindow::Create(
         NodeId{1},
         *desired,
         std::vector<ReceivedSignal>{*overlapping_b, *overlapping_b})) {
    return EXIT_FAILURE;
  }

  auto shared_emission = TxEmission::Create(
      TransmissionId{500},
      PacketId{500},
      NodeId{0},
      SimTime::FromNanoseconds(100),
      SimDuration::FromNanoseconds(10),
      100.0,
      20.0,
      180.0);
  if(!shared_emission) {
    return EXIT_FAILURE;
  }
  std::vector<ReceivedSignal> receiver_signals;
  for(const auto receiver : {NodeId{1}, NodeId{3}, NodeId{4}}) {
    auto response = ChannelFieldResponse::Create(
        shared_emission->transmission_id(),
        receiver,
        70.0,
        SimDuration::FromNanoseconds(20),
        {});
    if(!response) {
      return EXIT_FAILURE;
    }
    auto signal = ReceivedSignal::Create(*shared_emission, *response);
    if(!signal) {
      return EXIT_FAILURE;
    }
    receiver_signals.push_back(std::move(*signal));
  }
  if(receiver_signals.size() != 3 ||
     receiver_signals[0].transmission_id() != TransmissionId{500} ||
     receiver_signals[1].transmission_id() != TransmissionId{500} ||
     receiver_signals[2].transmission_id() != TransmissionId{500} ||
     receiver_signals[0].receiver_node_id() != NodeId{1} ||
     receiver_signals[1].receiver_node_id() != NodeId{3} ||
     receiver_signals[2].receiver_node_id() != NodeId{4}) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
