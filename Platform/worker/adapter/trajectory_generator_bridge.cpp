#include <cmath>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>
#include <ns3_factory/trajectory/uuv_5dof.hpp>

namespace {
using json = nlohmann::json;
using namespace ns3_factory::trajectory;

auto ReadFive(const json& input, const char* key) -> std::array<double, 5> {
  const auto values = input.at(key).get<std::vector<double>>();
  if (values.size() != 5) throw std::runtime_error(std::string(key) + " must contain five values");
  std::array<double, 5> result{};
  std::copy(values.begin(), values.end(), result.begin());
  return result;
}

auto Run(const json& input) -> json {
  if (input.at("schema") != "ns3-factory-uuv-generation-v1")
    throw std::runtime_error("unsupported generation schema");

  UuvParameters parameters;
  parameters.effective_mass = ReadFive(input, "effective_mass");
  parameters.linear_damping = ReadFive(input, "linear_damping");
  parameters.quadratic_damping = ReadFive(input, "quadratic_damping");
  Uuv5DofPlant plant(parameters);

  const auto initial_json = input.at("initial_ned");
  UuvState initial{
      initial_json.at("north_m").get<double>(), initial_json.at("east_m").get<double>(),
      initial_json.at("down_m").get<double>(), initial_json.value("pitch_rad", 0.0),
      initial_json.value("yaw_rad", 0.0), initial_json.value("u_mps", 0.0),
      initial_json.value("v_mps", 0.0), initial_json.value("w_mps", 0.0),
      initial_json.value("q_radps", 0.0), initial_json.value("r_radps", 0.0)};

  const auto controller_json = input.at("controller");
  std::unique_ptr<IUuvController> controller;
  if (controller_json.at("type") == "Direct") {
    const auto values = controller_json.at("tau").get<std::vector<double>>();
    if (values.size() != 5) throw std::runtime_error("direct tau must contain five values");
    controller = std::make_unique<DirectUuvController>(
        UuvControl{values[0], values[1], values[2], values[3], values[4]});
  } else if (controller_json.at("type") == "WaypointPid") {
    std::vector<WaypointNed> waypoints;
    for (const auto& point : controller_json.at("waypoints")) {
      const auto values = point.get<std::vector<double>>();
      if (values.size() != 3) throw std::runtime_error("waypoint must contain north/east/down");
      waypoints.push_back({values[0], values[1], values[2]});
    }
    controller = std::make_unique<WaypointUuvController>(
        controller_json.at("target_speed_mps").get<double>(), std::move(waypoints));
  } else {
    throw std::runtime_error("unsupported controller");
  }

  auto generated = GenerateUuvTrajectory(
      input.at("asset_id").get<std::string>(), input.at("version").get<std::uint64_t>(),
      input.at("track_id").get<std::string>(), input.at("duration_s").get<double>(),
      input.at("dt_s").get<double>(), initial, plant, *controller);
  if (!generated) throw std::runtime_error(generated.error().message);

  json points = json::array();
  for (const auto& sample : generated->samples) {
    points.push_back({
        {"time_s", sample.time_s}, {"x_m", sample.platform_position.x_meters},
        {"y_m", sample.platform_position.y_meters}, {"z_m", sample.platform_position.z_meters},
        {"pitch_rad", sample.ned.pitch_rad}, {"yaw_rad", sample.ned.yaw_rad},
        {"u_mps", sample.ned.surge_mps}, {"v_mps", sample.ned.sway_mps},
        {"w_mps", sample.ned.heave_mps}, {"q_radps", sample.ned.pitch_rate_radps},
        {"r_radps", sample.ned.yaw_rate_radps}});
  }
  return {{"asset_id", generated->asset.id()}, {"version", generated->asset.version()},
          {"name", input.at("name")}, {"coordinate_system", "LocalCartesianMetersZUp"},
          {"source", "uuv-5dof-rk4-v1"},
          {"source_notice", "演示参数，不代表任意真实 UUV。"},
          {"tracks", json::array({{{"track_id", input.at("track_id")}, {"points", points}}})}};
}
}  // namespace

int main() {
  try {
    const std::string text{std::istreambuf_iterator<char>{std::cin}, {}};
    if (text.size() > 1024 * 1024) throw std::runtime_error("generation request exceeds 1 MiB");
    std::cout << Run(json::parse(text)).dump() << '\n';
    return EXIT_SUCCESS;
  } catch (const std::exception& error) {
    std::cerr << "TRAJECTORY_GENERATION_FAILED: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
