#pragma once
#include <nlohmann/json.hpp>
#include <ns3_factory/planning/registered_algorithms.hpp>

namespace ns3_factory::worker::adapter {
inline auto AlgorithmCatalogJson() -> nlohmann::json {
  using Json = nlohmann::json;
  auto algorithms = Json::array();
  for(const auto& algorithm : planning::RegisteredAlgorithms()) {
    auto fields = Json::array();
    for(const auto& p : algorithm.parameters) fields.push_back({
        {"id", p.id}, {"label", p.label}, {"type", p.type}, {"unit", p.unit},
        {"default", p.default_value}, {"min", p.minimum}, {"max", p.maximum},
        {"help", p.help}, {"advanced", p.advanced}, {"choices", p.choices}});
    algorithms.push_back({{"id", algorithm.id}, {"version", algorithm.version},
        {"package_sha256", algorithm.package_sha256}, {"label", algorithm.label},
        {"description", algorithm.description}, {"interface", "planning-mac-v1"},
        {"parameters", fields}});
  }
  return {{"schema", "platform.algorithm-catalog.v1"}, {"algorithms", algorithms}};
}
}  // namespace ns3_factory::worker::adapter
