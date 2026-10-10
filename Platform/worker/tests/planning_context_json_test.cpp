#include <iostream>
#include "adapter/planning_context_json.hpp"
#include "flow_context_fixture.hpp"
#include "algorithm.hpp"
using namespace ns3_factory;
using namespace contracts;
using namespace worker::adapter;
#define CHECK(x) do { if(!(x)) { std::cerr << "line " << __LINE__ << '\n'; return 1; } } while(false)
int main() {
  auto data=planning::test::Fixture();
  data.algorithm={"example.channel-aware","1.0.0","mac"};
  data.effective_parameters={{"delay_weight","0.5"},{"guard_margin_ns","0"}};
  data.deterministic_seed=UINT64_MAX;
  auto context=PlanningContext::Create(data);CHECK(context);
  auto json=PlanningContextJson(*context);
  CHECK(json["deterministic_seed"]=="18446744073709551615");
  auto loaded=ReadPlanningContextJson(nlohmann::json::parse(json.dump()));CHECK(loaded);
  CHECK(planning::ContextFingerprint(*context)==planning::ContextFingerprint(*loaded));
  auto first=example_channel_aware::Plan(*context,data.effective_parameters);
  auto second=example_channel_aware::Plan(*loaded,loaded->data().effective_parameters);CHECK(first && second);
  CHECK(FlowPlanJson(*first)==FlowPlanJson(*second));
  auto bad=json;bad["deterministic_seed"]=UINT64_MAX;CHECK(!ReadPlanningContextJson(bad));
  bad=json;bad["nodes"][0]["node_id"]="00";CHECK(!ReadPlanningContextJson(bad));
  bad=json;bad["duration_ns"]="9223372036854775808";CHECK(!ReadPlanningContextJson(bad));
  bad=json;bad["links"][0]["loss_db"]=99.0;CHECK(!ReadPlanningContextJson(bad));
  bad=json;bad["unknown"]=true;CHECK(!ReadPlanningContextJson(bad));
  bad=json;bad["nodes"][0]["unknown"]=true;CHECK(!ReadPlanningContextJson(bad));
  CHECK(json["links"][0]["path_count"].is_null());
  std::cout << "CONTEXT_JSON_AND_SAME_CPP_REPLAY_OK\n";
}
