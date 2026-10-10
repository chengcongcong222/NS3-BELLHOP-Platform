#pragma once

#include <charconv>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <ns3_factory/contracts/planning_input.hpp>
#include <ns3_factory/contracts/protocol_cycle_plan.hpp>

namespace ns3_factory::planning {

// Version 1: stateless, read-only planning. Native packages are trusted code.
using Parameters = std::map<std::string, std::string>;
struct MacAlgorithmConfiguration final {
  std::string id;
  std::string version;
  std::string package_sha256;
  Parameters parameters;
  auto operator==(const MacAlgorithmConfiguration&) const -> bool = default;
};
struct Parameter final {
  std::string id, label, type, unit, default_value, minimum, maximum, help;
  bool advanced;
  std::vector<std::string> choices;
};
using BuildMac = contracts::Result<std::vector<contracts::TxOpportunity>> (*)(
    const contracts::WorldSnapshot&, const contracts::StructureSnapshot&,
    const contracts::PlanningInput&, contracts::SimDuration, const Parameters&);
struct MacAlgorithm final {
  std::string id, version, package_sha256, label, description;
  std::vector<Parameter> parameters;
  BuildMac build;
};

inline auto IntegerParameter(const Parameters& values, const std::string& name)
    -> contracts::Result<std::int64_t> {
  auto it = values.find(name);
  if(it != values.end()) {
    std::int64_t value{};
    const auto& text = it->second;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if(result.ec == std::errc{} && result.ptr == text.data() + text.size() &&
       std::to_string(value) == text) return value;
  }
  return std::unexpected(contracts::Error{contracts::ErrorCode::kInvalidArgument,
      "Algorithm parameter must be a canonical integer: " + name});
}

inline auto ValidateParameters(const MacAlgorithm& algorithm, const Parameters& values)
    -> contracts::Result<void> {
  using namespace contracts;
  if(values.size() != algorithm.parameters.size()) return std::unexpected(Error{
      ErrorCode::kInvalidArgument, "Algorithm parameters are missing or unknown"});
  for(const auto& field : algorithm.parameters) {
    const auto it = values.find(field.id);
    if(it == values.end() || it->second.size() > 128) return std::unexpected(Error{
        ErrorCode::kInvalidArgument, "Missing or oversized algorithm parameter: " + field.id});
    const auto& text = it->second;
    bool valid = false;
    if(field.type == "integer" || field.type == "duration_ns") {
      const auto value = IntegerParameter(values, field.id);
      if(value) {
        const auto low = IntegerParameter({{"bound", field.minimum}}, "bound");
        const auto high = IntegerParameter({{"bound", field.maximum}}, "bound");
        valid = low && high && *value >= *low && *value <= *high;
      }
    } else if(field.type == "number") {
      double value{}, low{}, high{};
      auto parse = [](const std::string& s, double& v) {
        auto r = std::from_chars(s.data(), s.data()+s.size(), v);
        return r.ec == std::errc{} && r.ptr == s.data()+s.size() && std::isfinite(v);
      };
      valid = parse(text, value) && parse(field.minimum, low) && parse(field.maximum, high)
          && value >= low && value <= high;
    } else if(field.type == "boolean") {
      valid = text == "true" || text == "false";
    } else if(field.type == "enum") {
      valid = std::find(field.choices.begin(), field.choices.end(), text) != field.choices.end();
    }
    if(!valid) return std::unexpected(Error{ErrorCode::kInvalidArgument,
        "Invalid algorithm parameter: " + field.id});
  }
  return {};
}
}  // namespace ns3_factory::planning
