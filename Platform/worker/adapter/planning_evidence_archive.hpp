#pragma once
#include <filesystem>
#include <fstream>
#include <ns3_factory/contracts/planning_evidence.hpp>
#include <ns3_factory/planning/registered_algorithms.hpp>
#include "planning_context_json.hpp"

namespace ns3_factory::worker::adapter {
class PlanningEvidenceArchive final : public contracts::IPlanningEvidenceSink {
 public:
  explicit PlanningEvidenceArchive(std::size_t maximum_bytes=64U*1024U*1024U):maximum_bytes_(maximum_bytes) {}
  void OnPlan(const contracts::PlanningContext& context,const contracts::ProtocolCyclePlan& plan,
              std::string_view package_sha256) noexcept override {
    Capture([&] {
      auto document=nlohmann::json{{"schema","platform.planning-replay.v2"},{"package_sha256",package_sha256},
          {"context",PlanningContextJson(context)},{"plan",FlowPlanJson(plan)},{"executions",nlohmann::json::array()}};
      const auto size=document.dump().size();
      if(size>maximum_cycle_bytes_) throw std::invalid_argument("Planning cycle export exceeds 8 MiB replay budget");
      Account(size);
      cycle_bytes_[context.data().structure.cycle_id().value()]=size;
      if(!cycles_.emplace(context.data().structure.cycle_id().value(),std::move(document)).second)
        throw std::invalid_argument("Duplicate cycle evidence");
    });
  }
  void OnGrant(const contracts::GrantExecutionEvidence& evidence) noexcept override {
    Capture([&] {
      using namespace planning_json;
      const auto& flow=*evidence.opportunity.flow_selection;
      auto& document=cycles_.at(flow.cycle_id.value());
      auto row=Json{{"grant_id",Id(flow.grant_id.value())},{"flow_id",Id(flow.flow_id.value())},
          {"owner",Id(evidence.opportunity.sender_node_id.value())},{"destination",Id(flow.destination_node_id.value())},
          {"next_hop",Id(flow.next_hop_node_id.value())},{"planned_at_ns",Time(evidence.opportunity.eligible_at)},
          {"actual_at_ns",Time(evidence.actual_at)},{"snapshot_version",Id(flow.snapshot_version.value())},
          {"context_fingerprint",Id(flow.context_fingerprint)},
          {"packet_id",evidence.packet_id ? Id(evidence.packet_id->value()):Json{}},
          {"transmission_id",evidence.transmission_id ? Id(evidence.transmission_id->value()):Json{}},
          {"status",evidence.status==GrantExecutionStatus::kExecuted ? "Executed":
           evidence.status==GrantExecutionStatus::kUnusedSelectedFlowEmpty ? "UnusedSelectedFlowEmpty":"Rejected"}};
      const auto size=row.dump().size()+1;
      auto& cycle_bytes=cycle_bytes_.at(flow.cycle_id.value());
      if(size>maximum_cycle_bytes_-cycle_bytes) throw std::invalid_argument("Planning cycle export exceeds 8 MiB replay budget");
      Account(size);cycle_bytes+=size;document["executions"].push_back(std::move(row));
    });
  }
  auto Write(const std::filesystem::path& directory) const -> contracts::Status {
    try {
      if(cycles_.empty() && error_.empty()) return {};
      std::filesystem::create_directories(directory.parent_path());
      if(!std::filesystem::create_directory(directory)) throw std::invalid_argument("Evidence directory already exists; refusing overwrite");
      nlohmann::json index{{"schema","platform.planning-evidence-index.v2"},{"maximum_bytes",std::to_string(maximum_bytes_)},
          {"status",error_.empty() ? "Available":"Unavailable"},{"error",error_},{"cycles",nlohmann::json::array()}};
      if(error_.empty()) for(const auto& [cycle,document]:cycles_) {
        auto name="cycle-"+std::to_string(cycle)+".json";
        std::ofstream file{directory/name};file.exceptions(std::ios::badbit|std::ios::failbit);file << document.dump() << '\n';
        index["cycles"].push_back(name);
      }
      std::ofstream file{directory/"index.json"};file.exceptions(std::ios::badbit|std::ios::failbit);file << index.dump() << '\n';
      if(!error_.empty()) throw std::invalid_argument(error_);
      return {};
    } catch(const std::exception& error) {
      return std::unexpected(contracts::Error{contracts::ErrorCode::kUnavailable,
          std::string{"Planning evidence unavailable: "}+error.what()});
    }
  }
 private:
  template<class Function> void Capture(Function&& function) noexcept {
    if(!error_.empty()) return;
    try { function(); } catch(const std::exception& error) { error_=error.what(); }
    catch(...) { error_="Planning evidence capture failed"; }
  }
  void Account(std::size_t bytes) {
    if(bytes>maximum_bytes_-bytes_) throw std::invalid_argument("Planning evidence deployment budget exceeded; no truncated export is presented as complete");
    bytes_+=bytes;
  }
  std::map<std::uint64_t,nlohmann::json> cycles_;
  static constexpr std::size_t maximum_cycle_bytes_=8U*1024U*1024U-1; // newline included by reader
  std::map<std::uint64_t,std::size_t> cycle_bytes_;
  std::size_t maximum_bytes_,bytes_{};
  std::string error_;
};

inline auto ReplayPlanningEvidence(const nlohmann::json& document) -> contracts::Result<nlohmann::json> {
  try {
    if(document.at("schema")!="platform.planning-replay.v2") throw std::invalid_argument("Unsupported replay document");
    for(const auto& item:document.items()) if(item.key()!="schema" && item.key()!="package_sha256" && item.key()!="context" &&
        item.key()!="plan" && item.key()!="executions") throw std::invalid_argument("Unknown replay field");
    auto context=ReadPlanningContextJson(document.at("context"));
    if(!context) return std::unexpected(context.error());
    const auto& data=context->data();
    const planning::MacAlgorithmConfiguration selection{data.algorithm.id,data.algorithm.version,
        document.at("package_sha256"),data.effective_parameters,"planning-mac-v2"};
    const auto* algorithm=planning::FindFlowAlgorithm(selection);
    if(!algorithm) throw std::invalid_argument("Exact replay algorithm package is not installed");
    auto valid=planning::ValidateParameters(*algorithm,selection.parameters);
    if(!valid) return std::unexpected(valid.error());
    auto plan=algorithm->build(*context,selection.parameters);
    if(!plan) return std::unexpected(plan.error());
    valid=plan->Validate(*context);if(!valid) return std::unexpected(valid.error());
    auto actual=FlowPlanJson(*plan);
    if(actual!=document.at("plan")) throw std::invalid_argument("Replay output differs from captured plan");
    return nlohmann::json{{"status","Matched"},{"plan",actual},{"context_fingerprint",std::to_string(planning::ContextFingerprint(*context))}};
  } catch(const std::exception& error) {
    return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,std::string{"Replay failed: "}+error.what()});
  }
}
}  // namespace ns3_factory::worker::adapter
