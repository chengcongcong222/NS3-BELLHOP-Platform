#include <charconv>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <nlohmann/json.hpp>

#include <ns3_factory/worker/codec/json_codec.hpp>

namespace ns3_factory::worker::codec {
namespace {

using Json = nlohmann::json;
using contracts::Error;
using contracts::ErrorCode;

[[nodiscard]] auto ProtocolError(std::string message) -> Error {
  return Error{ErrorCode::kInvalidArgument, std::move(message)};
}

auto RequireKeys(const Json& value,
                 std::initializer_list<std::string_view> keys,
                 std::string_view context) -> contracts::Status {
  if(!value.is_object() || value.size() != keys.size()) {
    return std::unexpected(ProtocolError(
        std::string{context} + " must contain exactly the schema fields"));
  }
  for(const auto key : keys) {
    if(!value.contains(std::string{key})) {
      return std::unexpected(ProtocolError(
          std::string{context} + " is missing field " + std::string{key}));
    }
  }
  return {};
}

template <typename Integer>
[[nodiscard]] auto Decimal(Integer value) -> std::string {
  return std::to_string(value);
}

template <typename Integer>
[[nodiscard]] auto ParseDecimal(const Json& value,
                                std::string_view field)
    -> contracts::Result<Integer> {
  if(!value.is_string()) {
    return std::unexpected(ProtocolError(
        std::string{field} + " must be a decimal string"));
  }
  const auto& text = value.get_ref<const std::string&>();
  if(text.empty() ||
     (text.size() > 1U && text.front() == '0') ||
     (text.size() > 2U && text[0] == '-' && text[1] == '0')) {
    return std::unexpected(ProtocolError(
        std::string{field} + " is not canonical decimal"));
  }
  Integer parsed{};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if(error != std::errc{} || end != text.data() + text.size()) {
    return std::unexpected(ProtocolError(
        std::string{field} + " is outside its integer range"));
  }
  return parsed;
}

template <typename StringId>
[[nodiscard]] auto ParseStringId(const Json& value,
                                 std::string_view field)
    -> contracts::Result<StringId> {
  if(!value.is_string()) {
    return std::unexpected(
        ProtocolError(std::string{field} + " must be a string"));
  }
  return StringId::Create(value.get<std::string>());
}

[[nodiscard]] auto ParseFiniteDouble(const Json& value,
                                     std::string_view field)
    -> contracts::Result<double> {
  if(!value.is_number()) {
    return std::unexpected(
        ProtocolError(std::string{field} + " must be a JSON number"));
  }
  const auto parsed = value.get<double>();
  if(!std::isfinite(parsed)) {
    return std::unexpected(
        ProtocolError(std::string{field} + " must be finite"));
  }
  return parsed;
}

[[nodiscard]] auto ValidateEnvironmentAssetId(std::string_view value)
    -> contracts::Status {
  if(value.empty() || value == "." || value == "..") {
    return std::unexpected(
        ProtocolError("environment.asset_id is invalid"));
  }
  const auto alphanumeric = [](char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9');
  };
  if(!alphanumeric(value.front())) {
    return std::unexpected(
        ProtocolError("environment.asset_id must begin alphanumeric"));
  }
  for(const auto character : value) {
    if(!alphanumeric(character) && character != '-' && character != '_' &&
       character != '.') {
      return std::unexpected(
          ProtocolError("environment.asset_id contains an invalid byte"));
    }
  }
  return {};
}

[[nodiscard]] auto ParseJson(std::string_view line,
                             std::size_t limit)
    -> contracts::Result<Json> {
  if(line.empty() || line.size() > limit) {
    return std::unexpected(ProtocolError(
        "JSON line is empty or exceeds the configured byte limit"));
  }
  try {
    return Json::parse(line.begin(), line.end(), nullptr, true, false);
  } catch(const Json::exception& error) {
    return std::unexpected(
        ProtocolError(std::string{"Invalid JSON: "} + error.what()));
  }
}

[[nodiscard]] auto Dump(Json value) -> contracts::Result<std::string> {
  try {
    auto text = value.dump();
    if(text.size() > kMaximumOutputMessageBytes) {
      return std::unexpected(
          ProtocolError("Encoded worker message exceeds output limit"));
    }
    return text;
  } catch(const Json::exception& error) {
    return std::unexpected(
        ProtocolError(std::string{"JSON encoding failed: "} + error.what()));
  }
}

[[nodiscard]] auto EncodeErrorCode(ErrorCode code) -> std::string_view {
  switch(code) {
    case ErrorCode::kInvalidArgument: return "InvalidArgument";
    case ErrorCode::kOutOfRange: return "OutOfRange";
    case ErrorCode::kOverflow: return "Overflow";
    case ErrorCode::kNotFound: return "NotFound";
    case ErrorCode::kAlreadyExists: return "AlreadyExists";
    case ErrorCode::kFailedPrecondition: return "FailedPrecondition";
    case ErrorCode::kUnsupported: return "Unsupported";
    case ErrorCode::kUnavailable: return "Unavailable";
    case ErrorCode::kInternal: return "Internal";
  }
  return "Internal";
}

[[nodiscard]] auto DecodeErrorCode(const Json& value)
    -> contracts::Result<ErrorCode> {
  if(!value.is_string()) return std::unexpected(ProtocolError("error.code must be a string"));
  const auto text = value.get<std::string_view>();
#define CODE(name, value) if(text == name) return ErrorCode::value
  CODE("InvalidArgument", kInvalidArgument);
  CODE("OutOfRange", kOutOfRange);
  CODE("Overflow", kOverflow);
  CODE("NotFound", kNotFound);
  CODE("AlreadyExists", kAlreadyExists);
  CODE("FailedPrecondition", kFailedPrecondition);
  CODE("Unsupported", kUnsupported);
  CODE("Unavailable", kUnavailable);
  CODE("Internal", kInternal);
#undef CODE
  return std::unexpected(ProtocolError("error.code is unknown"));
}

[[nodiscard]] auto EncodeLifecycle(application::RunLifecycle value)
    -> std::string_view {
  switch(value) {
    case application::RunLifecycle::kCreated: return "Created";
    case application::RunLifecycle::kRunning: return "Running";
    case application::RunLifecycle::kCompleted: return "Completed";
    case application::RunLifecycle::kFailed: return "Failed";
  }
  return "Failed";
}

[[nodiscard]] auto DecodeLifecycle(const Json& value)
    -> contracts::Result<application::RunLifecycle> {
  if(!value.is_string()) return std::unexpected(ProtocolError("run.lifecycle must be a string"));
  const auto text = value.get<std::string_view>();
  if(text == "Created") return application::RunLifecycle::kCreated;
  if(text == "Running") return application::RunLifecycle::kRunning;
  if(text == "Completed") return application::RunLifecycle::kCompleted;
  if(text == "Failed") return application::RunLifecycle::kFailed;
  return std::unexpected(ProtocolError("run.lifecycle is unknown"));
}

template <typename Enum>
[[nodiscard]] auto EncodeMetric(Enum value) -> std::string_view {
  if constexpr(std::is_same_v<Enum, application::MetricStatus>) {
    if(value == Enum::kPass) return "Pass";
    if(value == Enum::kFail) return "Fail";
    return "NotEvaluated";
  } else {
    if(value == Enum::kPass) return "Pass";
    if(value == Enum::kFail) return "Fail";
    return "NotFullyEvaluated";
  }
}

template <typename Enum>
[[nodiscard]] auto DecodeMetric(const Json& value,
                                std::string_view field)
    -> contracts::Result<Enum> {
  if(!value.is_string()) return std::unexpected(ProtocolError(std::string{field} + " must be a string"));
  const auto text = value.get<std::string_view>();
  if(text == "Pass") return Enum::kPass;
  if(text == "Fail") return Enum::kFail;
  if constexpr(std::is_same_v<Enum, application::MetricStatus>) {
    if(text == "NotEvaluated") return Enum::kNotEvaluated;
  } else if(text == "NotFullyEvaluated") {
    return Enum::kNotFullyEvaluated;
  }
  return std::unexpected(ProtocolError(std::string{field} + " is unknown"));
}

[[nodiscard]] auto TimeJson(contracts::SimTime value) -> Json {
  return Decimal(value.nanoseconds());
}
[[nodiscard]] auto DurationJson(contracts::SimDuration value) -> Json {
  return Decimal(value.nanoseconds());
}

[[nodiscard]] auto EncodeRunRecord(const application::RunRecord& run) -> Json {
  Json failure = nullptr;
  if(run.failure) {
    failure = {{"code", EncodeErrorCode(run.failure->code)},
               {"message", run.failure->message}};
  }
  return {{"run_id", run.run_id.value()},
          {"experiment_id", run.experiment.experiment_id.value()},
          {"experiment_version", Decimal(run.experiment.experiment_version)},
          {"scenario_id", run.scenario.scenario_id.value()},
          {"scenario_version", Decimal(run.scenario.scenario_version)},
          {"environment_asset_id", run.environment.asset_id},
          {"environment_format_version", Decimal(run.environment.asset_format_version)},
          {"lifecycle", EncodeLifecycle(run.lifecycle)},
          {"simulation_started_at_ns", run.simulation_started_at ? TimeJson(*run.simulation_started_at) : Json(nullptr)},
          {"simulation_ended_at_ns", run.simulation_ended_at ? TimeJson(*run.simulation_ended_at) : Json(nullptr)},
          {"final_snapshot_version", run.final_snapshot_version ? Json(Decimal(run.final_snapshot_version->value())) : Json(nullptr)},
          {"failure", std::move(failure)},
          {"event_stream_complete", run.event_stream_complete ? Json(*run.event_stream_complete) : Json(nullptr)}};
}

[[nodiscard]] auto DecodeRunRecord(const Json& value)
    -> contracts::Result<application::RunRecord> {
  const auto keys = RequireKeys(value, {"run_id","experiment_id","experiment_version","scenario_id","scenario_version","environment_asset_id","environment_format_version","lifecycle","simulation_started_at_ns","simulation_ended_at_ns","final_snapshot_version","failure","event_stream_complete"}, "run");
  if(!keys) return std::unexpected(keys.error());
  auto run_id = ParseStringId<application::RunId>(value.at("run_id"), "run_id");
  auto experiment_id = ParseStringId<application::ExperimentId>(value.at("experiment_id"), "experiment_id");
  auto experiment_version = ParseDecimal<std::uint64_t>(value.at("experiment_version"), "experiment_version");
  auto scenario_id = ParseStringId<application::ScenarioId>(value.at("scenario_id"), "scenario_id");
  auto scenario_version = ParseDecimal<std::uint64_t>(value.at("scenario_version"), "scenario_version");
  auto format_version = ParseDecimal<std::uint32_t>(value.at("environment_format_version"), "environment_format_version");
  auto lifecycle = DecodeLifecycle(value.at("lifecycle"));
  if(!run_id || !experiment_id || !experiment_version || !scenario_id || !scenario_version || !format_version || !lifecycle || !value.at("environment_asset_id").is_string()) return std::unexpected(ProtocolError("run identity is invalid"));
  std::optional<contracts::SimTime> started;
  std::optional<contracts::SimTime> ended;
  std::optional<contracts::SnapshotVersion> snapshot;
  if(!value.at("simulation_started_at_ns").is_null()) { auto v=ParseDecimal<std::int64_t>(value.at("simulation_started_at_ns"),"simulation_started_at_ns"); if(!v) return std::unexpected(v.error()); started=contracts::SimTime::FromNanoseconds(*v); }
  if(!value.at("simulation_ended_at_ns").is_null()) { auto v=ParseDecimal<std::int64_t>(value.at("simulation_ended_at_ns"),"simulation_ended_at_ns"); if(!v) return std::unexpected(v.error()); ended=contracts::SimTime::FromNanoseconds(*v); }
  if(!value.at("final_snapshot_version").is_null()) { auto v=ParseDecimal<std::uint64_t>(value.at("final_snapshot_version"),"final_snapshot_version"); if(!v) return std::unexpected(v.error()); snapshot=contracts::SnapshotVersion{*v}; }
  std::optional<application::RunFailureSummary> failure;
  if(!value.at("failure").is_null()) { const auto& f=value.at("failure"); auto k=RequireKeys(f,{"code","message"},"failure"); auto c=DecodeErrorCode(f.at("code")); if(!k || !c || !f.at("message").is_string()) return std::unexpected(ProtocolError("run failure is invalid")); failure=application::RunFailureSummary{*c,f.at("message").get<std::string>()}; }
  std::optional<bool> complete;
  if(!value.at("event_stream_complete").is_null()) { if(!value.at("event_stream_complete").is_boolean()) return std::unexpected(ProtocolError("event_stream_complete must be boolean or null")); complete=value.at("event_stream_complete").get<bool>(); }
  return application::RunRecord{std::move(*run_id),{std::move(*experiment_id),*experiment_version},{std::move(*scenario_id),*scenario_version},{value.at("environment_asset_id").get<std::string>(),*format_version},*lifecycle,started,ended,snapshot,std::move(failure),complete};
}

[[nodiscard]] auto EncodeResult(const application::RunResult& result) -> Json {
  const auto& p=result.projection;
  Json report=nullptr;
  if(result.acceptance_report) { const auto& a=*result.acceptance_report; report={{"network_node_count",EncodeMetric(a.network_node_count)},{"communication_rate",EncodeMetric(a.communication_rate)},{"bit_error_rate",EncodeMetric(a.bit_error_rate)},{"feature_level_fusion",EncodeMetric(a.feature_level_fusion)},{"bearing_point_count",EncodeMetric(a.bearing_point_count)},{"fusion_period",EncodeMetric(a.fusion_period)},{"overall",EncodeMetric(a.overall)},{"evaluated_target_receptions",Decimal(a.evaluated_target_receptions)},{"missing_ber_evidence_count",Decimal(a.missing_ber_evidence_count)},{"maximum_ber",a.maximum_ber?Json(*a.maximum_ber):Json(nullptr)},{"mean_ber",a.mean_ber?Json(*a.mean_ber):Json(nullptr)},{"required_maximum_ber",a.required_maximum_ber},{"minimum_bearing_points",a.minimum_bearing_points?Json(Decimal(*a.minimum_bearing_points)):Json(nullptr)},{"required_minimum_bearing_points",Decimal(a.required_minimum_bearing_points)},{"maximum_fusion_period_ns",a.maximum_fusion_period?DurationJson(*a.maximum_fusion_period):Json(nullptr)},{"required_maximum_fusion_period_ns",DurationJson(a.required_maximum_fusion_period)},{"ber_reason",a.ber_reason}}; }
  Json fusion=Json::array(); for(const auto& f:result.fusion_results) fusion.push_back({{"fusion_sequence",Decimal(f.fusion_sequence)},{"started_at_ns",TimeJson(f.started_at)},{"completed_at_ns",TimeJson(f.completed_at)},{"fusion_period_ns",DurationJson(f.fusion_period)},{"observation_count",Decimal(f.observation_count)},{"estimated_target_x_meters",f.estimated_target_x_meters},{"estimated_target_y_meters",f.estimated_target_y_meters}});
  Json nodes=Json::array(); for(const auto& n:result.nodes) nodes.push_back({{"node_id",Decimal(n.node_id.value())},{"x_meters",n.final_position.x_meters},{"y_meters",n.final_position.y_meters},{"z_meters",n.final_position.z_meters},{"is_fusion_center",n.is_fusion_center}});
  return {{"run_id",result.run_id.value()},{"projection",{{"simulation_started_at_ns",TimeJson(p.simulation_started_at)},{"simulation_ended_at_ns",TimeJson(p.simulation_ended_at)},{"simulation_duration_ns",DurationJson(p.simulation_duration)},{"final_snapshot_version",Decimal(p.final_snapshot_version.value())},{"cycle_count",Decimal(p.cycle_count)},{"node_count",Decimal(p.node_count)},{"transmission_count",Decimal(p.transmission_count)},{"channel_signal_count",Decimal(p.channel_signal_count)},{"channel_no_arrival_count",Decimal(p.channel_no_arrival_count)},{"reception_count",Decimal(p.reception_count)},{"local_delivery_count",Decimal(p.local_delivery_count)}}},{"acceptance_report",std::move(report)},{"fusion_results",std::move(fusion)},{"nodes",std::move(nodes)}};
}

template <typename Integer>
auto ReadInt(const Json& object, const char* key) -> contracts::Result<Integer> { return ParseDecimal<Integer>(object.at(key),key); }

[[nodiscard]] auto DecodeResult(const Json& value) -> contracts::Result<application::RunResult> {
  auto keys=RequireKeys(value,{"run_id","projection","acceptance_report","fusion_results","nodes"},"result"); if(!keys) return std::unexpected(keys.error());
  auto run=ParseStringId<application::RunId>(value.at("run_id"),"run_id"); const auto& p=value.at("projection"); auto pk=RequireKeys(p,{"simulation_started_at_ns","simulation_ended_at_ns","simulation_duration_ns","final_snapshot_version","cycle_count","node_count","transmission_count","channel_signal_count","channel_no_arrival_count","reception_count","local_delivery_count"},"projection"); if(!run||!pk) return std::unexpected(ProtocolError("result header is invalid"));
  auto ps=ReadInt<std::int64_t>(p,"simulation_started_at_ns"),pe=ReadInt<std::int64_t>(p,"simulation_ended_at_ns"),pd=ReadInt<std::int64_t>(p,"simulation_duration_ns"); auto pv=ReadInt<std::uint64_t>(p,"final_snapshot_version"),pc=ReadInt<std::size_t>(p,"cycle_count"),pn=ReadInt<std::size_t>(p,"node_count"),pt=ReadInt<std::size_t>(p,"transmission_count"),pcs=ReadInt<std::size_t>(p,"channel_signal_count"),pcn=ReadInt<std::size_t>(p,"channel_no_arrival_count"),pr=ReadInt<std::size_t>(p,"reception_count"),pl=ReadInt<std::size_t>(p,"local_delivery_count"); if(!ps||!pe||!pd||!pv||!pc||!pn||!pt||!pcs||!pcn||!pr||!pl) return std::unexpected(ProtocolError("projection integer is invalid"));
  application::RunProjectionSummary projection{contracts::SimTime::FromNanoseconds(*ps),contracts::SimTime::FromNanoseconds(*pe),contracts::SimDuration::FromNanoseconds(*pd),contracts::SnapshotVersion{*pv},*pc,*pn,*pt,*pcs,*pcn,*pr,*pl};
  std::optional<application::AcceptanceReportSummary> report; if(!value.at("acceptance_report").is_null()) { const auto&a=value.at("acceptance_report"); auto ak=RequireKeys(a,{"network_node_count","communication_rate","bit_error_rate","feature_level_fusion","bearing_point_count","fusion_period","overall","evaluated_target_receptions","missing_ber_evidence_count","maximum_ber","mean_ber","required_maximum_ber","minimum_bearing_points","required_minimum_bearing_points","maximum_fusion_period_ns","required_maximum_fusion_period_ns","ber_reason"},"acceptance_report"); auto m1=DecodeMetric<application::MetricStatus>(a.at("network_node_count"),"network_node_count"); auto m2=DecodeMetric<application::MetricStatus>(a.at("communication_rate"),"communication_rate"); auto m3=DecodeMetric<application::MetricStatus>(a.at("bit_error_rate"),"bit_error_rate"); auto m4=DecodeMetric<application::MetricStatus>(a.at("feature_level_fusion"),"feature_level_fusion"); auto m5=DecodeMetric<application::MetricStatus>(a.at("bearing_point_count"),"bearing_point_count"); auto m6=DecodeMetric<application::MetricStatus>(a.at("fusion_period"),"fusion_period"); auto ov=DecodeMetric<application::OverallStatus>(a.at("overall"),"overall"); auto er=ReadInt<std::size_t>(a,"evaluated_target_receptions"); auto mb=ReadInt<std::size_t>(a,"missing_ber_evidence_count"); auto rmb=ReadInt<std::size_t>(a,"required_minimum_bearing_points"); auto rfp=ReadInt<std::int64_t>(a,"required_maximum_fusion_period_ns"); if(!ak||!m1||!m2||!m3||!m4||!m5||!m6||!ov||!er||!mb||!rmb||!rfp||!a.at("ber_reason").is_string()) return std::unexpected(ProtocolError("acceptance report is invalid")); std::optional<double> maxber,meanber; if(!a.at("maximum_ber").is_null()){auto d=ParseFiniteDouble(a.at("maximum_ber"),"maximum_ber");if(!d)return std::unexpected(d.error());maxber=*d;} if(!a.at("mean_ber").is_null()){auto d=ParseFiniteDouble(a.at("mean_ber"),"mean_ber");if(!d)return std::unexpected(d.error());meanber=*d;} auto required=ParseFiniteDouble(a.at("required_maximum_ber"),"required_maximum_ber"); if(!required)return std::unexpected(required.error()); std::optional<std::size_t> minpoints; if(!a.at("minimum_bearing_points").is_null()){auto v=ParseDecimal<std::size_t>(a.at("minimum_bearing_points"),"minimum_bearing_points");if(!v)return std::unexpected(v.error());minpoints=*v;} std::optional<contracts::SimDuration> maxperiod; if(!a.at("maximum_fusion_period_ns").is_null()){auto v=ParseDecimal<std::int64_t>(a.at("maximum_fusion_period_ns"),"maximum_fusion_period_ns");if(!v)return std::unexpected(v.error());maxperiod=contracts::SimDuration::FromNanoseconds(*v);} report=application::AcceptanceReportSummary{*m1,*m2,*m3,*m4,*m5,*m6,*ov,*er,*mb,maxber,meanber,*required,minpoints,*rmb,maxperiod,contracts::SimDuration::FromNanoseconds(*rfp),a.at("ber_reason").get<std::string>()}; }
  if(!value.at("fusion_results").is_array()||!value.at("nodes").is_array()) return std::unexpected(ProtocolError("result arrays are invalid")); std::vector<application::FusionResultSummary> fusion; for(const auto&f:value.at("fusion_results")){auto fk=RequireKeys(f,{"fusion_sequence","started_at_ns","completed_at_ns","fusion_period_ns","observation_count","estimated_target_x_meters","estimated_target_y_meters"},"fusion");auto seq=ReadInt<std::uint64_t>(f,"fusion_sequence"); auto s=ReadInt<std::int64_t>(f,"started_at_ns"); auto e=ReadInt<std::int64_t>(f,"completed_at_ns"); auto d=ReadInt<std::int64_t>(f,"fusion_period_ns"); auto c=ReadInt<std::size_t>(f,"observation_count"); auto x=ParseFiniteDouble(f.at("estimated_target_x_meters"),"x"); auto y=ParseFiniteDouble(f.at("estimated_target_y_meters"),"y");if(!fk||!seq||!s||!e||!d||!c||!x||!y)return std::unexpected(ProtocolError("fusion result is invalid"));fusion.push_back({*seq,contracts::SimTime::FromNanoseconds(*s),contracts::SimTime::FromNanoseconds(*e),contracts::SimDuration::FromNanoseconds(*d),*c,*x,*y});} std::vector<application::NodeSummary> nodes; for(const auto&n:value.at("nodes")){auto nk=RequireKeys(n,{"node_id","x_meters","y_meters","z_meters","is_fusion_center"},"node");auto id=ReadInt<std::uint64_t>(n,"node_id"); auto x=ParseFiniteDouble(n.at("x_meters"),"x"); auto y=ParseFiniteDouble(n.at("y_meters"),"y"); auto z=ParseFiniteDouble(n.at("z_meters"),"z");if(!nk||!id||!x||!y||!z||!n.at("is_fusion_center").is_boolean())return std::unexpected(ProtocolError("node result is invalid"));nodes.push_back({contracts::NodeId{*id},{*x,*y,*z},n.at("is_fusion_center").get<bool>()});} return application::RunResult{std::move(*run),projection,std::move(report),std::move(fusion),std::move(nodes)};
}

[[nodiscard]] auto EncodeTrace(const contracts::TraceEvent& event) -> Json {
  Json payload;
  std::string_view kind;
  std::visit([&](const auto& v){using T=std::decay_t<decltype(v)>; if constexpr(std::is_same_v<T,contracts::CycleCommitTrace>){kind="CycleCommit";payload={{"cycle_id",Decimal(v.cycle_id.value())},{"base_snapshot_version",Decimal(v.base_snapshot_version.value())},{"committed_snapshot_version",Decimal(v.committed_snapshot_version.value())},{"committed_at_ns",TimeJson(v.committed_at)}};}else if constexpr(std::is_same_v<T,contracts::TransmissionTrace>){kind="Transmission";Json target;std::visit([&](const auto&t){using U=std::decay_t<decltype(t)>;if constexpr(std::is_same_v<U,contracts::TraceUnicastTransmissionTarget>)target={{"type","Unicast"},{"node_id",Decimal(t.node_id.value())}};else target={{"type","Broadcast"}};},v.target);payload={{"transmission_id",Decimal(v.transmission_id.value())},{"packet_id",Decimal(v.packet_id.value())},{"sender_node_id",Decimal(v.sender_node_id.value())},{"target",target},{"started_at_ns",TimeJson(v.started_at)},{"ended_at_ns",TimeJson(v.ended_at)}};}else if constexpr(std::is_same_v<T,contracts::ChannelOutcomeTrace>){kind="ChannelOutcome";Json outcome;std::visit([&](const auto&o){using U=std::decay_t<decltype(o)>;if constexpr(std::is_same_v<U,contracts::TraceSignalChannelOutcome>)outcome={{"type","Signal"},{"first_arrival_delay_ns",DurationJson(o.first_arrival_delay)},{"aggregate_transmission_loss_db",o.aggregate_transmission_loss_db},{"path_count",Decimal(o.path_count)}};else outcome={{"type","NoArrival"}};},v.outcome);payload={{"transmission_id",Decimal(v.transmission_id.value())},{"receiver_node_id",Decimal(v.receiver_node_id.value())},{"outcome",outcome}};}else{kind="Reception";Json quality=nullptr;if(v.quality)quality={{"signal_to_noise_ratio_db",v.quality->signal_to_noise_ratio_db},{"eb_n0_db",v.quality->eb_n0_db},{"bit_error_rate",v.quality->bit_error_rate},{"source",v.quality->source==contracts::TraceRxQualityEvidenceSource::kModeled?"Modeled":v.quality->source==contracts::TraceRxQualityEvidenceSource::kMeasured?"Measured":"External"}};std::string_view d=v.disposition==contracts::TraceReceptionDisposition::kNotDecoded?"NotDecoded":v.disposition==contracts::TraceReceptionDisposition::kOverheard?"Overheard":v.disposition==contracts::TraceReceptionDisposition::kLocalDelivery?"LocalDelivery":"RelayEnqueue";payload={{"reception_id",Decimal(v.reception_id.value())},{"transmission_id",Decimal(v.transmission_id.value())},{"packet_id",Decimal(v.packet_id.value())},{"receiver_node_id",Decimal(v.receiver_node_id.value())},{"disposition",d},{"quality",quality}};}},event.payload());
  return {{"occurred_at_ns",TimeJson(event.occurred_at())},{"kind",kind},{"payload",std::move(payload)}};
}

[[nodiscard]] auto DecodeTrace(const Json& value) -> contracts::Result<contracts::TraceEvent> {
  auto k=RequireKeys(value,{"occurred_at_ns","kind","payload"},"trace");auto at=ReadInt<std::int64_t>(value,"occurred_at_ns");if(!k||!at||!value.at("kind").is_string())return std::unexpected(ProtocolError("trace header is invalid"));const auto kind=value.at("kind").get<std::string_view>();const auto&p=value.at("payload");std::optional<contracts::TracePayload> payload;
  if(kind=="CycleCommit"){auto pk=RequireKeys(p,{"cycle_id","base_snapshot_version","committed_snapshot_version","committed_at_ns"},"cycle trace");auto a=ReadInt<std::uint64_t>(p,"cycle_id"); auto b=ReadInt<std::uint64_t>(p,"base_snapshot_version"); auto c=ReadInt<std::uint64_t>(p,"committed_snapshot_version"); auto d=ReadInt<std::int64_t>(p,"committed_at_ns");if(!pk||!a||!b||!c||!d)return std::unexpected(ProtocolError("cycle trace is invalid"));payload=contracts::CycleCommitTrace{contracts::PlanningCycleId{*a},contracts::SnapshotVersion{*b},contracts::SnapshotVersion{*c},contracts::SimTime::FromNanoseconds(*d)};
  }else if(kind=="Transmission"){auto pk=RequireKeys(p,{"transmission_id","packet_id","sender_node_id","target","started_at_ns","ended_at_ns"},"transmission trace");auto a=ReadInt<std::uint64_t>(p,"transmission_id"); auto b=ReadInt<std::uint64_t>(p,"packet_id"); auto c=ReadInt<std::uint64_t>(p,"sender_node_id"); auto s=ReadInt<std::int64_t>(p,"started_at_ns"); auto e=ReadInt<std::int64_t>(p,"ended_at_ns");if(!pk||!a||!b||!c||!s||!e)return std::unexpected(ProtocolError("transmission trace is invalid"));const auto&t=p.at("target");std::optional<contracts::TraceTransmissionTarget> target;if(t.is_object()&&t.value("type","")=="Broadcast"&&t.size()==1)target=contracts::TraceBroadcastTransmissionTarget{};else{auto tk=RequireKeys(t,{"type","node_id"},"target");auto id=ReadInt<std::uint64_t>(t,"node_id");if(!tk||!id||t.at("type")!="Unicast")return std::unexpected(ProtocolError("target is invalid"));target=contracts::TraceUnicastTransmissionTarget{contracts::NodeId{*id}};}payload=contracts::TransmissionTrace{contracts::TransmissionId{*a},contracts::PacketId{*b},contracts::NodeId{*c},*target,contracts::SimTime::FromNanoseconds(*s),contracts::SimTime::FromNanoseconds(*e)};
  }else if(kind=="ChannelOutcome"){auto pk=RequireKeys(p,{"transmission_id","receiver_node_id","outcome"},"channel trace");auto a=ReadInt<std::uint64_t>(p,"transmission_id"); auto b=ReadInt<std::uint64_t>(p,"receiver_node_id");if(!pk||!a||!b)return std::unexpected(ProtocolError("channel trace is invalid"));const auto&o=p.at("outcome");std::optional<contracts::TraceChannelOutcome> outcome;if(o.is_object()&&o.value("type","")=="NoArrival"&&o.size()==1)outcome=contracts::TraceNoArrivalChannelOutcome{};else{auto ok=RequireKeys(o,{"type","first_arrival_delay_ns","aggregate_transmission_loss_db","path_count"},"channel outcome");auto d=ReadInt<std::int64_t>(o,"first_arrival_delay_ns"); auto n=ReadInt<std::uint64_t>(o,"path_count"); auto g=ParseFiniteDouble(o.at("aggregate_transmission_loss_db"),"aggregate_transmission_loss_db");if(!ok||!d||!n||!g||o.at("type")!="Signal")return std::unexpected(ProtocolError("channel outcome is invalid"));outcome=contracts::TraceSignalChannelOutcome{contracts::SimDuration::FromNanoseconds(*d),*g,*n};}payload=contracts::ChannelOutcomeTrace{contracts::TransmissionId{*a},contracts::NodeId{*b},*outcome};
  }else if(kind=="Reception"){auto pk=RequireKeys(p,{"reception_id","transmission_id","packet_id","receiver_node_id","disposition","quality"},"reception trace");auto a=ReadInt<std::uint64_t>(p,"reception_id"),b=ReadInt<std::uint64_t>(p,"transmission_id"),c=ReadInt<std::uint64_t>(p,"packet_id"),d=ReadInt<std::uint64_t>(p,"receiver_node_id");if(!pk||!a||!b||!c||!d||!p.at("disposition").is_string())return std::unexpected(ProtocolError("reception trace is invalid"));const auto ds=p.at("disposition").get<std::string_view>();contracts::TraceReceptionDisposition disposition;if(ds=="NotDecoded")disposition=contracts::TraceReceptionDisposition::kNotDecoded;else if(ds=="Overheard")disposition=contracts::TraceReceptionDisposition::kOverheard;else if(ds=="LocalDelivery")disposition=contracts::TraceReceptionDisposition::kLocalDelivery;else if(ds=="RelayEnqueue")disposition=contracts::TraceReceptionDisposition::kRelayEnqueue;else return std::unexpected(ProtocolError("reception disposition is unknown"));std::optional<contracts::TraceRxQualitySummary> quality;if(!p.at("quality").is_null()){const auto&q=p.at("quality");auto qk=RequireKeys(q,{"signal_to_noise_ratio_db","eb_n0_db","bit_error_rate","source"},"quality");auto x=ParseFiniteDouble(q.at("signal_to_noise_ratio_db"),"snr"),y=ParseFiniteDouble(q.at("eb_n0_db"),"eb_n0"),z=ParseFiniteDouble(q.at("bit_error_rate"),"ber");if(!qk||!x||!y||!z||!q.at("source").is_string())return std::unexpected(ProtocolError("quality is invalid"));const auto qs=q.at("source").get<std::string_view>();auto source=qs=="Modeled"?contracts::TraceRxQualityEvidenceSource::kModeled:qs=="Measured"?contracts::TraceRxQualityEvidenceSource::kMeasured:contracts::TraceRxQualityEvidenceSource::kExternal;if(qs!="Modeled"&&qs!="Measured"&&qs!="External")return std::unexpected(ProtocolError("quality source is unknown"));quality=contracts::TraceRxQualitySummary{*x,*y,*z,source};}payload=contracts::ReceptionTrace{contracts::ReceptionId{*a},contracts::TransmissionId{*b},contracts::PacketId{*c},contracts::NodeId{*d},disposition,quality};
  }else return std::unexpected(ProtocolError("trace kind is unknown"));return contracts::TraceEvent::Create(contracts::SimTime::FromNanoseconds(*at),std::move(*payload));
}

[[nodiscard]] auto DecodeCategory(const Json& value)->contracts::Result<WorkerFailureCategory>{if(!value.is_string())return std::unexpected(ProtocolError("failure category must be a string"));const auto s=value.get<std::string_view>();if(s=="Protocol")return WorkerFailureCategory::kProtocol;if(s=="Composition")return WorkerFailureCategory::kComposition;if(s=="Simulation")return WorkerFailureCategory::kSimulation;return std::unexpected(ProtocolError("failure category is unknown"));}
[[nodiscard]] auto EncodeCategory(WorkerFailureCategory value)->std::string_view{return value==WorkerFailureCategory::kProtocol?"Protocol":value==WorkerFailureCategory::kComposition?"Composition":"Simulation";}

}  // namespace

auto EncodeStartRunCommand(const StartRunCommand& c)->contracts::Result<std::string>{return Dump({{"schema_version",kWorkerWireSchemaVersion},{"type","StartRunCommand"},{"run_id",c.run_id.value()},{"preset",{{"scenario_id",c.scenario_id.value()},{"experiment_id",c.experiment_id.value()},{"definition_version",Decimal(c.definition_version)},{"acceptance_profile",c.profile==application::AcceptanceProfile::kAcceptance4Node?"Acceptance4Node":"Extended6Node"}}},{"environment",{{"asset_id",c.environment.asset_id},{"asset_format_version",Decimal(c.environment.asset_format_version)}}},{"execution",{{"simulation_cycle_count",Decimal(c.simulation_cycle_count)},{"rx_quality_mode",c.quality_mode==application::RxQualityMode::kNone?"None":"ModeledBpskAwgn"},{"equivalent_noise_power_db_re_1upa2",c.equivalent_noise_power_db_re_1upa2},{"deterministic_seed",Decimal(c.deterministic_seed)}}}});}

auto DecodeStartRunCommand(std::string_view line)->contracts::Result<StartRunCommand>{auto parsed=ParseJson(line,kMaximumInputLineBytes);if(!parsed)return std::unexpected(parsed.error());try{const auto&j=*parsed;auto k=RequireKeys(j,{"schema_version","type","run_id","preset","environment","execution"},"StartRunCommand");if(!k||!j.at("schema_version").is_number_unsigned()||j.at("schema_version").get<std::uint32_t>()!=kWorkerWireSchemaVersion)return std::unexpected(ProtocolError("unknown or missing schema_version"));if(!j.at("type").is_string()||j.at("type")!="StartRunCommand")return std::unexpected(ProtocolError("unknown command type"));const auto&p=j.at("preset");const auto&e=j.at("environment");const auto&x=j.at("execution");auto pk=RequireKeys(p,{"scenario_id","experiment_id","definition_version","acceptance_profile"},"preset"); auto ek=RequireKeys(e,{"asset_id","asset_format_version"},"environment"); auto xk=RequireKeys(x,{"simulation_cycle_count","rx_quality_mode","equivalent_noise_power_db_re_1upa2","deterministic_seed"},"execution");auto run=ParseStringId<application::RunId>(j.at("run_id"),"run_id"); auto scenario=ParseStringId<application::ScenarioId>(p.at("scenario_id"),"scenario_id"); auto experiment=ParseStringId<application::ExperimentId>(p.at("experiment_id"),"experiment_id"); auto version=ParseDecimal<std::uint64_t>(p.at("definition_version"),"definition_version"); auto format=ParseDecimal<std::uint32_t>(e.at("asset_format_version"),"asset_format_version"); auto cycles=ParseDecimal<std::size_t>(x.at("simulation_cycle_count"),"simulation_cycle_count"); auto seed=ParseDecimal<std::uint64_t>(x.at("deterministic_seed"),"deterministic_seed"); auto noise=ParseFiniteDouble(x.at("equivalent_noise_power_db_re_1upa2"),"noise");if(!pk||!ek||!xk||!run||!scenario||!experiment||!version||!format||!cycles||!seed||!noise||!e.at("asset_id").is_string()||!p.at("acceptance_profile").is_string()||!x.at("rx_quality_mode").is_string())return std::unexpected(ProtocolError("StartRunCommand field is invalid"));const auto asset=e.at("asset_id").get<std::string>();auto asset_valid=ValidateEnvironmentAssetId(asset);if(!asset_valid)return std::unexpected(asset_valid.error());const auto profile=p.at("acceptance_profile").get<std::string_view>();const auto quality=x.at("rx_quality_mode").get<std::string_view>();if(profile!="Acceptance4Node"&&profile!="Extended6Node")return std::unexpected(ProtocolError("acceptance_profile is unknown"));if(quality!="None"&&quality!="ModeledBpskAwgn")return std::unexpected(ProtocolError("rx_quality_mode is unknown"));if(*version==0||*format==0||*cycles==0)return std::unexpected(ProtocolError("versions and cycle count must be positive"));return StartRunCommand{std::move(*run),std::move(*scenario),std::move(*experiment),*version,{asset,*format},profile=="Acceptance4Node"?application::AcceptanceProfile::kAcceptance4Node:application::AcceptanceProfile::kExtended6Node,*cycles,quality=="None"?application::RxQualityMode::kNone:application::RxQualityMode::kModeledBpskAwgn,*noise,*seed};}catch(const Json::exception&error){return std::unexpected(ProtocolError(std::string{"Invalid command shape: "}+error.what()));}}

auto EncodeWorkerMessage(const WorkerMessage& message)->contracts::Result<std::string>{Json j={{"schema_version",kWorkerWireSchemaVersion}};std::visit([&](const auto&v){using T=std::decay_t<decltype(v)>;if constexpr(std::is_same_v<T,WorkerStarted>){j["type"]="WorkerStarted";j["run_id"]=v.run_id.value();}else if constexpr(std::is_same_v<T,WorkerRunEvent>){j["type"]="WorkerRunEvent";j["run_id"]=v.record.run_id.value();j["sequence"]=Decimal(v.record.sequence.value());j["trace"]=EncodeTrace(v.record.trace_event);}else if constexpr(std::is_same_v<T,WorkerCompleted>){j["type"]="WorkerCompleted";j["run"]=EncodeRunRecord(v.run);j["result"]=EncodeResult(v.result);}else{j["type"]="WorkerFailed";j["run_id"]=v.run_id?Json(v.run_id->value()):Json(nullptr);j["category"]=EncodeCategory(v.category);j["error"]={{"code",EncodeErrorCode(v.error.code)},{"message",v.error.message}};j["run"]=v.run?EncodeRunRecord(*v.run):Json(nullptr);}},message);return Dump(std::move(j));}

auto DecodeWorkerMessage(std::string_view line)->contracts::Result<WorkerMessage>{auto parsed=ParseJson(line,kMaximumOutputMessageBytes);if(!parsed)return std::unexpected(parsed.error());try{const auto&j=*parsed;if(!j.is_object()||!j.contains("schema_version")||!j.at("schema_version").is_number_unsigned()||j.at("schema_version").get<std::uint32_t>()!=kWorkerWireSchemaVersion)return std::unexpected(ProtocolError("unknown or missing schema_version"));if(!j.contains("type")||!j.at("type").is_string())return std::unexpected(ProtocolError("missing worker message type"));const auto type=j.at("type").get<std::string_view>();if(type=="WorkerStarted"){auto k=RequireKeys(j,{"schema_version","type","run_id"},"WorkerStarted");auto id=ParseStringId<application::RunId>(j.at("run_id"),"run_id");if(!k||!id)return std::unexpected(ProtocolError("WorkerStarted is invalid"));return WorkerStarted{std::move(*id)};}if(type=="WorkerRunEvent"){auto k=RequireKeys(j,{"schema_version","type","run_id","sequence","trace"},"WorkerRunEvent");auto id=ParseStringId<application::RunId>(j.at("run_id"),"run_id");auto sequence=ParseDecimal<std::uint64_t>(j.at("sequence"),"sequence");auto trace=DecodeTrace(j.at("trace"));if(!k||!id||!sequence||*sequence==0||!trace)return std::unexpected(ProtocolError("WorkerRunEvent is invalid"));return WorkerRunEvent{{std::move(*id),application::RunEventSequence{*sequence},std::move(*trace)}};}if(type=="WorkerCompleted"){auto k=RequireKeys(j,{"schema_version","type","run","result"},"WorkerCompleted");auto run=DecodeRunRecord(j.at("run"));auto result=DecodeResult(j.at("result"));if(!k||!run||!result||run->lifecycle!=application::RunLifecycle::kCompleted||run->run_id!=result->run_id)return std::unexpected(ProtocolError("WorkerCompleted is inconsistent"));return WorkerCompleted{std::move(*run),std::move(*result)};}if(type=="WorkerFailed"){auto k=RequireKeys(j,{"schema_version","type","run_id","category","error","run"},"WorkerFailed");auto category=DecodeCategory(j.at("category"));const auto&e=j.at("error");auto ek=RequireKeys(e,{"code","message"},"error");auto code=DecodeErrorCode(e.at("code"));if(!k||!category||!ek||!code||!e.at("message").is_string())return std::unexpected(ProtocolError("WorkerFailed is invalid"));std::optional<application::RunId> id;if(!j.at("run_id").is_null()){auto parsed_id=ParseStringId<application::RunId>(j.at("run_id"),"run_id");if(!parsed_id)return std::unexpected(parsed_id.error());id=std::move(*parsed_id);}std::optional<application::RunRecord> run;if(!j.at("run").is_null()){auto parsed_run=DecodeRunRecord(j.at("run"));if(!parsed_run)return std::unexpected(parsed_run.error());run=std::move(*parsed_run);}return WorkerFailed{std::move(id),*category,{*code,e.at("message").get<std::string>()},std::move(run)};}return std::unexpected(ProtocolError("unknown worker message type"));}catch(const Json::exception&error){return std::unexpected(ProtocolError(std::string{"Invalid worker message shape: "}+error.what()));}}

}  // namespace ns3_factory::worker::codec
