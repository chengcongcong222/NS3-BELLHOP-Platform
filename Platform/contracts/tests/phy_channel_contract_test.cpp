#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/channel.hpp>
#include <ns3_factory/contracts/destination.hpp>
#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/transmission_target.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

using ns3_factory::contracts::BroadcastDestination;
using ns3_factory::contracts::BroadcastTransmissionTarget;
using ns3_factory::contracts::ChannelFieldResponse;
using ns3_factory::contracts::ChannelQuery;
using ns3_factory::contracts::CheckedAdd;
using ns3_factory::contracts::DigitalPacket;
using ns3_factory::contracts::ErrorCode;
using ns3_factory::contracts::IChannelFieldProvider;
using ns3_factory::contracts::ITxPhy;
using ns3_factory::contracts::NodeId;
using ns3_factory::contracts::PacketId;
using ns3_factory::contracts::Position3d;
using ns3_factory::contracts::PropagationPath;
using ns3_factory::contracts::Result;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;
using ns3_factory::contracts::Status;
using ns3_factory::contracts::Transmission;
using ns3_factory::contracts::TransmissionId;
using ns3_factory::contracts::TxEmission;
using ns3_factory::contracts::TxEncodeRequest;
using ns3_factory::contracts::ValidateChannelFieldResponseIdentity;

template <typename T>
concept HasReceiverIdentity =
    requires(T value) { value.receiver_node_id; } ||
    requires(const T value) { value.receiver_node_id(); } ||
    requires(T value) { value.receiver_id; } ||
    requires(const T value) { value.receiver_id(); };

template <typename T>
concept HasReceiverCollection =
    requires(T value) { value.receivers; } ||
    requires(const T value) { value.receivers(); } ||
    requires(T value) { value.receiver_ids; } ||
    requires(const T value) { value.receiver_ids(); } ||
    requires(T value) { value.receiver_node_ids; } ||
    requires(const T value) { value.receiver_node_ids(); };

template <typename T>
concept HasCommitMethod = requires(T value) { value.Commit(); };

template <typename T>
concept HasApplyMethod = requires(T value) { value.Apply(); };

template <typename T>
concept HasScheduleMethod = requires(T value) { value.Schedule(); };

template <typename T>
concept HasLegacyPropagationDelay =
    requires(const T value) { value.propagation_delay(); };

template <typename T>
concept HasLegacyRelativeDelay =
    requires(const T value) { value.relative_delay(); };

template <typename T>
concept HasLegacyAmplitudeLinear =
    requires(const T value) { value.amplitude_linear(); };

static_assert(!HasReceiverIdentity<TxEncodeRequest>);
static_assert(!HasReceiverCollection<TxEncodeRequest>);
static_assert(!HasReceiverIdentity<TxEmission>);
static_assert(!HasReceiverCollection<TxEmission>);
static_assert(HasReceiverIdentity<ChannelQuery>);
static_assert(HasReceiverIdentity<ChannelFieldResponse>);
static_assert(!HasReceiverCollection<ChannelQuery>);
static_assert(!HasReceiverCollection<ChannelFieldResponse>);
static_assert(!HasCommitMethod<ITxPhy> && !HasApplyMethod<ITxPhy> &&
              !HasScheduleMethod<ITxPhy>);
static_assert(!HasCommitMethod<IChannelFieldProvider> &&
              !HasApplyMethod<IChannelFieldProvider> &&
              !HasScheduleMethod<IChannelFieldProvider>);
static_assert(std::has_virtual_destructor_v<ITxPhy>);
static_assert(std::has_virtual_destructor_v<IChannelFieldProvider>);
static_assert(requires(const ITxPhy& phy,
                       const DigitalPacket& packet,
                       const TxEncodeRequest& request) {
  { phy.Encode(packet, request) } -> std::same_as<Result<TxEmission>>;
});
static_assert(requires(const IChannelFieldProvider& provider,
                       const ChannelQuery& query) {
  { provider.Query(query) } ->
      std::same_as<Result<ChannelFieldResponse>>;
});
static_assert(std::same_as<
              decltype(std::declval<const ChannelQuery&>().tx_position()),
              const Position3d&>);
static_assert(std::same_as<
              decltype(std::declval<const ChannelQuery&>().rx_position()),
              const Position3d&>);
static_assert(!HasLegacyPropagationDelay<ChannelFieldResponse>);
static_assert(!HasLegacyRelativeDelay<PropagationPath>);
static_assert(!HasLegacyAmplitudeLinear<PropagationPath>);
static_assert(requires(const ChannelQuery& query,
                       const ChannelFieldResponse& response) {
  { ValidateChannelFieldResponseIdentity(query, response) } ->
      std::same_as<Status>;
});

class TestTxPhy final : public ITxPhy {
 public:
  [[nodiscard]] auto Encode(const DigitalPacket& packet,
                            const TxEncodeRequest& request) const
      -> Result<TxEmission> override {
    ++encode_count_;
    return TxEmission::Create(request.transmission_id,
                              packet.packet_id,
                              request.sender_node_id,
                              request.started_at,
                              SimDuration::FromNanoseconds(50),
                              25'000.0,
                              4'000.0,
                              180.0);
  }

  [[nodiscard]] auto encode_count() const noexcept -> std::size_t {
    return encode_count_;
  }

 private:
  mutable std::size_t encode_count_{0};
};

class TestChannelFieldProvider final : public IChannelFieldProvider {
 public:
  [[nodiscard]] auto Query(const ChannelQuery& query) const
      -> Result<ChannelFieldResponse> override {
    std::vector<PropagationPath> paths;
    paths.reserve(3);
    for(const auto& specification :
        std::array{std::pair{11LL, 0.25},
                   std::pair{0LL, 1.25},
                   std::pair{5LL, 0.5}}) {
      auto path = PropagationPath::Create(
          SimDuration::FromNanoseconds(specification.first),
          specification.second,
          0.125 * static_cast<double>(paths.size()));
      if(!path) {
        return std::unexpected(path.error());
      }
      paths.push_back(std::move(*path));
    }

    return ChannelFieldResponse::Create(
        query.transmission_id(),
        query.receiver_node_id(),
        72.5,
        SimDuration::FromNanoseconds(1'500),
        std::move(paths));
  }
};

auto main() -> int {
  const DigitalPacket packet{PacketId{1},
                             NodeId{0},
                             BroadcastDestination{},
                             std::vector<std::byte>{std::byte{0x5A}}};
  const TxEncodeRequest encode_request{TransmissionId{10},
                                       NodeId{0},
                                       BroadcastTransmissionTarget{},
                                       SimTime::FromNanoseconds(100)};

  const TestTxPhy tx_phy;
  auto emission_result = tx_phy.Encode(packet, encode_request);
  if(!emission_result || tx_phy.encode_count() != 1) {
    return EXIT_FAILURE;
  }
  const auto& emission = *emission_result;
  if(emission.transmission_id() != encode_request.transmission_id ||
     emission.packet_id() != packet.packet_id ||
     emission.sender_node_id() != encode_request.sender_node_id ||
     emission.started_at() != encode_request.started_at ||
     emission.center_frequency_hz() != 25'000.0 ||
     emission.bandwidth_hz() != 4'000.0 ||
     emission.source_level_db_re_1upa_at_1m() != 180.0) {
    return EXIT_FAILURE;
  }

  const auto ended_at = CheckedAdd(emission.started_at(),
                                   emission.duration());
  if(!ended_at || *ended_at != SimTime::FromNanoseconds(150)) {
    return EXIT_FAILURE;
  }
  const Transmission transmission{encode_request.transmission_id,
                                  packet.packet_id,
                                  encode_request.sender_node_id,
                                  encode_request.target,
                                  encode_request.started_at,
                                  *ended_at};
  if(transmission.ended_at != *ended_at) {
    return EXIT_FAILURE;
  }

  constexpr auto kMaxNanoseconds =
      std::numeric_limits<SimTime::representation_type>::max();
  if(CheckedAdd(SimTime::FromNanoseconds(kMaxNanoseconds),
                SimDuration::FromNanoseconds(1))) {
    return EXIT_FAILURE;
  }

  const auto nan = std::numeric_limits<double>::quiet_NaN();
  const auto infinity = std::numeric_limits<double>::infinity();
  const auto make_emission = [](SimDuration duration,
                                double frequency,
                                double bandwidth,
                                double source_level) {
    return TxEmission::Create(TransmissionId{20},
                              PacketId{2},
                              NodeId{0},
                              SimTime::Zero(),
                              duration,
                              frequency,
                              bandwidth,
                              source_level);
  };
  if(make_emission(SimDuration::FromNanoseconds(-1), 1.0, 1.0, 1.0) ||
     make_emission(SimDuration::Zero(), 0.0, 1.0, 1.0) ||
     make_emission(SimDuration::Zero(), 1.0, 0.0, 1.0) ||
     make_emission(SimDuration::Zero(), nan, 1.0, 1.0) ||
     make_emission(SimDuration::Zero(), 1.0, infinity, 1.0) ||
     make_emission(SimDuration::Zero(), 1.0, 1.0, -infinity)) {
    return EXIT_FAILURE;
  }

  std::vector<ChannelQuery> queries;
  queries.reserve(3);
  constexpr std::array receiver_ids{NodeId{1}, NodeId{3}, NodeId{4}};
  constexpr std::array receiver_positions{
      Position3d{10.0, 20.0, -30.0},
      Position3d{40.0, 50.0, -60.0},
      Position3d{70.0, 80.0, -90.0}};
  for(std::size_t index = 0; index < receiver_ids.size(); ++index) {
    auto query = ChannelQuery::Create(emission.transmission_id(),
                                      emission.sender_node_id(),
                                      receiver_ids[index],
                                      Position3d{1.0, 2.0, -3.0},
                                      receiver_positions[index],
                                      emission.started_at(),
                                      emission.center_frequency_hz(),
                                      emission.bandwidth_hz());
    if(!query) {
      return EXIT_FAILURE;
    }
    queries.push_back(std::move(*query));
  }

  const TestChannelFieldProvider channel_provider;
  for(std::size_t index = 0; index < queries.size(); ++index) {
    const auto& query = queries[index];
    if(query.transmission_id() != emission.transmission_id() ||
       query.receiver_node_id() != receiver_ids[index] ||
       query.tx_position() != Position3d{1.0, 2.0, -3.0} ||
       query.rx_position() != receiver_positions[index]) {
      return EXIT_FAILURE;
    }

    auto response = channel_provider.Query(query);
    if(!response ||
       response->transmission_id() != emission.transmission_id() ||
       response->receiver_node_id() != receiver_ids[index] ||
       response->aggregate_transmission_loss_db() != 72.5 ||
       response->first_arrival_delay() !=
           SimDuration::FromNanoseconds(1'500) ||
       response->paths().size() != 3 ||
       response->paths()[0].excess_delay() != SimDuration::Zero() ||
       response->paths()[1].excess_delay() !=
           SimDuration::FromNanoseconds(5) ||
       response->paths()[2].excess_delay() !=
           SimDuration::FromNanoseconds(11) ||
       !ValidateChannelFieldResponseIdentity(query, *response)) {
      return EXIT_FAILURE;
    }

    const auto absolute_path_delay =
        CheckedAdd(response->first_arrival_delay(),
                   response->paths()[2].excess_delay());
    if(!absolute_path_delay ||
       *absolute_path_delay != SimDuration::FromNanoseconds(1'511)) {
      return EXIT_FAILURE;
    }

    // Scalar and path-aware processing are alternative representations.
    const auto scalar_received_level_db =
        emission.source_level_db_re_1upa_at_1m() -
        response->aggregate_transmission_loss_db();
    const auto path_aware_pressure_gain =
        response->paths()[0].pressure_gain_linear();
    if(scalar_received_level_db != 107.5 ||
       path_aware_pressure_gain != 1.25) {
      return EXIT_FAILURE;
    }
  }

  auto scalar_only = ChannelFieldResponse::Create(
      TransmissionId{10},
      NodeId{1},
      -3.0,
      SimDuration::FromNanoseconds(100),
      {});
  if(!scalar_only || !scalar_only->paths().empty() ||
     scalar_only->aggregate_transmission_loss_db() != -3.0) {
    return EXIT_FAILURE;
  }

  auto ordering_path_a = PropagationPath::Create(
      SimDuration::FromNanoseconds(5), 2.0, 0.0);
  auto ordering_path_b = PropagationPath::Create(
      SimDuration::FromNanoseconds(5), 1.0, 1.0);
  auto ordering_path_c = PropagationPath::Create(
      SimDuration::FromNanoseconds(5), 1.0, -1.0);
  auto ordering_path_d =
      PropagationPath::Create(SimDuration::Zero(), 3.0, 5.0);
  if(!ordering_path_a || !ordering_path_b || !ordering_path_c ||
     !ordering_path_d) {
    return EXIT_FAILURE;
  }

  auto ordered_response_a = ChannelFieldResponse::Create(
      TransmissionId{10},
      NodeId{1},
      1.0,
      SimDuration::Zero(),
      std::vector<PropagationPath>{*ordering_path_a,
                                   *ordering_path_d,
                                   *ordering_path_c,
                                   *ordering_path_b});
  auto ordered_response_b = ChannelFieldResponse::Create(
      TransmissionId{10},
      NodeId{1},
      1.0,
      SimDuration::Zero(),
      std::vector<PropagationPath>{*ordering_path_b,
                                   *ordering_path_c,
                                   *ordering_path_d,
                                   *ordering_path_a});
  if(!ordered_response_a || !ordered_response_b ||
     ordered_response_a->paths().size() !=
         ordered_response_b->paths().size()) {
    return EXIT_FAILURE;
  }
  for(std::size_t index = 0;
      index < ordered_response_a->paths().size();
      ++index) {
    if(ordered_response_a->paths()[index] !=
       ordered_response_b->paths()[index]) {
      return EXIT_FAILURE;
    }
  }
  const auto deterministic_paths = ordered_response_a->paths();
  if(deterministic_paths[0] != *ordering_path_d ||
     deterministic_paths[1] != *ordering_path_c ||
     deterministic_paths[2] != *ordering_path_b ||
     deterministic_paths[3] != *ordering_path_a) {
    return EXIT_FAILURE;
  }

  auto nonzero_excess_path = PropagationPath::Create(
      SimDuration::FromNanoseconds(1), 1.0, 0.0);
  auto greater_than_one_gain_path =
      PropagationPath::Create(SimDuration::Zero(), 1.5, 0.0);
  if(!nonzero_excess_path || !greater_than_one_gain_path ||
     greater_than_one_gain_path->pressure_gain_linear() != 1.5 ||
     ChannelFieldResponse::Create(
         TransmissionId{10},
         NodeId{1},
         1.0,
         SimDuration::Zero(),
         std::vector<PropagationPath>{*nonzero_excess_path})) {
    return EXIT_FAILURE;
  }

  auto mismatched_transmission = ChannelFieldResponse::Create(
      TransmissionId{999},
      queries[0].receiver_node_id(),
      1.0,
      SimDuration::Zero(),
      {});
  auto mismatched_receiver = ChannelFieldResponse::Create(
      queries[0].transmission_id(),
      NodeId{999},
      1.0,
      SimDuration::Zero(),
      {});
  if(!mismatched_transmission || !mismatched_receiver) {
    return EXIT_FAILURE;
  }
  const auto transmission_identity_status =
      ValidateChannelFieldResponseIdentity(queries[0],
                                           *mismatched_transmission);
  const auto receiver_identity_status =
      ValidateChannelFieldResponseIdentity(queries[0], *mismatched_receiver);
  if(transmission_identity_status || receiver_identity_status ||
     transmission_identity_status.error().code !=
         ErrorCode::kFailedPrecondition ||
     receiver_identity_status.error().code !=
         ErrorCode::kFailedPrecondition) {
    return EXIT_FAILURE;
  }

  if(PropagationPath::Create(SimDuration::FromNanoseconds(-1), 1.0, 0.0) ||
     PropagationPath::Create(SimDuration::Zero(), -1.0, 0.0) ||
     PropagationPath::Create(SimDuration::Zero(), nan, 0.0) ||
     PropagationPath::Create(SimDuration::Zero(), infinity, 0.0) ||
     PropagationPath::Create(SimDuration::Zero(), -infinity, 0.0) ||
     PropagationPath::Create(SimDuration::Zero(), 1.0, nan) ||
     PropagationPath::Create(SimDuration::Zero(), 1.0, infinity) ||
     PropagationPath::Create(SimDuration::Zero(), 1.0, -infinity) ||
     ChannelQuery::Create(TransmissionId{10},
                          NodeId{0},
                          NodeId{1},
                          Position3d{nan, 0.0, 0.0},
                          Position3d{0.0, 0.0, 0.0},
                          SimTime::Zero(),
                          1.0,
                          1.0) ||
     ChannelQuery::Create(TransmissionId{10},
                          NodeId{0},
                          NodeId{1},
                          Position3d{0.0, 0.0, 0.0},
                          Position3d{0.0, 0.0, infinity},
                          SimTime::Zero(),
                          1.0,
                          1.0) ||
     ChannelQuery::Create(TransmissionId{10},
                          NodeId{0},
                          NodeId{1},
                          Position3d{0.0, 0.0, 0.0},
                          Position3d{0.0, 0.0, 0.0},
                          SimTime::Zero(),
                          0.0,
                          1.0) ||
     ChannelFieldResponse::Create(TransmissionId{10},
                                  NodeId{1},
                                  nan,
                                  SimDuration::Zero(),
                                  {}) ||
     ChannelFieldResponse::Create(TransmissionId{10},
                                  NodeId{1},
                                  infinity,
                                  SimDuration::Zero(),
                                  {}) ||
     ChannelFieldResponse::Create(TransmissionId{10},
                                  NodeId{1},
                                  -infinity,
                                  SimDuration::Zero(),
                                  {}) ||
     ChannelFieldResponse::Create(
         TransmissionId{10},
         NodeId{1},
         1.0,
         SimDuration::FromNanoseconds(-1),
         {})) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
