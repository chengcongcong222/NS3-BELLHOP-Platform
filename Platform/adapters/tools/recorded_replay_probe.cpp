// Numerical verification only. Not a production asset loader or Run entry.
#include <iostream>
#include <nlohmann/json.hpp>
#include "internal/recorded_replay.hpp"

int main() {
  using namespace ns3_factory::phy::internal::recorded;
  try {
    nlohmann::json j; std::cin >> j;
    Channel c{j.at("fc"), j.at("fs_delay"), j.at("fs_time"),
              j.at("snapshots"),j.at("delay_taps"),{},j.value("phase",std::vector<double>{}),
              j.value("delay_tracking",false),j.value("resample_up",1U),j.value("resample_down",1U)};
    for(const auto& pair:j.at("h")) c.coefficients.emplace_back(pair[0],pair[1]);
    auto out=Replay(c,j.at("input").get<std::vector<double>>(),j.at("fs"),j.at("start"));
    if(!out) { std::cerr<<out.error().message<<'\n'; return 2; }
    std::cout<<nlohmann::json(*out).dump()<<'\n';
    return 0;
  } catch(const std::exception& e) {std::cerr<<e.what()<<'\n'; return 2;}
}
