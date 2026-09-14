#pragma once

#include <array>
#include <cmath>
#include <memory>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

#include "asset.hpp"

namespace ns3_factory::trajectory {

struct UuvState {
  double north_m{}, east_m{}, down_m{}, pitch_rad{}, yaw_rad{};
  double surge_mps{}, sway_mps{}, heave_mps{}, pitch_rate_radps{}, yaw_rate_radps{};
  auto operator==(const UuvState&) const -> bool = default;
};

struct UuvControl {
  double surge_force_n{}, sway_force_n{}, heave_force_n{};
  double pitch_moment_nm{}, yaw_moment_nm{};
};

struct UuvParameters {
  std::array<double, 5> effective_mass{120.0, 150.0, 160.0, 80.0, 90.0};
  std::array<double, 5> linear_damping{18.0, 28.0, 32.0, 14.0, 16.0};
  std::array<double, 5> quadratic_damping{24.0, 36.0, 42.0, 18.0, 20.0};
  double weight_n{1177.2};
  double buoyancy_n{1177.2};
  double center_of_buoyancy_z_m{-0.04};
};

inline auto DemoUuvParameters() -> UuvParameters { return {}; }
inline constexpr std::string_view kDemoUuvParameterNotice =
    "Demo UUV preset for deterministic product demonstration; not a model of an arbitrary real UUV.";

class IUuvController {
 public:
  virtual ~IUuvController() = default;
  virtual auto Command(double time_s, const UuvState& state) const -> UuvControl = 0;
};

class DirectUuvController final : public IUuvController {
 public:
  explicit DirectUuvController(UuvControl command) : command_(command) {}
  auto Command(double, const UuvState&) const -> UuvControl override { return command_; }
 private:
  UuvControl command_;
};

struct WaypointNed { double north_m{}, east_m{}, down_m{}; };
struct TrackingGains {
  double surge_speed_kp{80.0}, yaw_kp{90.0}, yaw_kd{24.0};
  double depth_kp{100.0}, heave_kd{45.0}, pitch_kp{35.0};
  double waypoint_radius_m{2.0};
};

class WaypointUuvController final : public IUuvController {
 public:
  WaypointUuvController(double target_speed_mps, std::vector<WaypointNed> waypoints,
                        TrackingGains gains = {})
      : speed_(target_speed_mps), waypoints_(std::move(waypoints)), gains_(gains) {}
  auto Command(double, const UuvState& s) const -> UuvControl override {
    if (waypoints_.empty()) return {};
    std::size_t target = 0;
    while (target + 1 < waypoints_.size() &&
           std::hypot(waypoints_[target].north_m - s.north_m,
                      waypoints_[target].east_m - s.east_m) <= gains_.waypoint_radius_m) ++target;
    const auto& p = waypoints_[target];
    const double desired_yaw = std::atan2(p.east_m - s.east_m, p.north_m - s.north_m);
    const double yaw_error = std::remainder(desired_yaw - s.yaw_rad, 2.0 * std::numbers::pi);
    return {gains_.surge_speed_kp * (speed_ - s.surge_mps), 0.0,
            gains_.depth_kp * (p.down_m - s.down_m) - gains_.heave_kd * s.heave_mps,
            -gains_.pitch_kp * s.pitch_rad,
            gains_.yaw_kp * yaw_error - gains_.yaw_kd * s.yaw_rate_radps};
  }
 private:
  double speed_;
  std::vector<WaypointNed> waypoints_;
  TrackingGains gains_;
};

class Uuv5DofPlant final {
 public:
  explicit Uuv5DofPlant(UuvParameters parameters = DemoUuvParameters()) : p_(parameters) {}
  auto Derivative(const UuvState& s, const UuvControl& tau,
                  const UuvControl& disturbance = {}) const -> Result<UuvState> {
    const std::array<double, 10> values{s.north_m,s.east_m,s.down_m,s.pitch_rad,s.yaw_rad,
      s.surge_mps,s.sway_mps,s.heave_mps,s.pitch_rate_radps,s.yaw_rate_radps};
    if (!std::all_of(values.begin(), values.end(), [](double x){ return std::isfinite(x); }) ||
        !ValidParameters() || std::abs(std::cos(s.pitch_rad)) < 1e-6)
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "Invalid UUV state, parameters, or Euler singularity"});
    const double ct=std::cos(s.pitch_rad), st=std::sin(s.pitch_rad);
    const double cp=std::cos(s.yaw_rad), sp=std::sin(s.yaw_rad);
    UuvState d;
    d.north_m = cp*ct*s.surge_mps - sp*s.sway_mps + cp*st*s.heave_mps;
    d.east_m  = sp*ct*s.surge_mps + cp*s.sway_mps + sp*st*s.heave_mps;
    d.down_m  = -st*s.surge_mps + ct*s.heave_mps;
    d.pitch_rad = s.pitch_rate_radps;
    d.yaw_rad = s.yaw_rate_radps / ct;
    const std::array<double,5> velocity{s.surge_mps,s.sway_mps,s.heave_mps,s.pitch_rate_radps,s.yaw_rate_radps};
    const std::array<double,5> input{tau.surge_force_n+disturbance.surge_force_n,
      tau.sway_force_n+disturbance.sway_force_n,tau.heave_force_n+disturbance.heave_force_n,
      tau.pitch_moment_nm+disturbance.pitch_moment_nm,tau.yaw_moment_nm+disturbance.yaw_moment_nm};
    // Five-DOF diagonal M with rigid/added mass folded into effective_mass.
    // omega x Mv retains deterministic Coriolis/centripetal coupling (roll is constrained).
    const double cu = p_.effective_mass[2]*s.heave_mps*s.pitch_rate_radps - p_.effective_mass[1]*s.sway_mps*s.yaw_rate_radps;
    const double cv = p_.effective_mass[0]*s.surge_mps*s.yaw_rate_radps;
    const double cw = -p_.effective_mass[0]*s.surge_mps*s.pitch_rate_radps;
    const double restoring_heave = (p_.weight_n-p_.buoyancy_n)*ct;
    const double restoring_pitch = -p_.buoyancy_n*p_.center_of_buoyancy_z_m*std::sin(s.pitch_rad);
    const std::array<double,5> coriolis{cu,cv,cw,0.0,0.0};
    const std::array<double,5> restoring{0.0,0.0,restoring_heave,restoring_pitch,0.0};
    std::array<double,5> acceleration{};
    for(std::size_t i=0;i<5;++i)
      acceleration[i]=(input[i]-coriolis[i]-p_.linear_damping[i]*velocity[i]
        -p_.quadratic_damping[i]*std::abs(velocity[i])*velocity[i]-restoring[i])/p_.effective_mass[i];
    d.surge_mps=acceleration[0]; d.sway_mps=acceleration[1]; d.heave_mps=acceleration[2];
    d.pitch_rate_radps=acceleration[3]; d.yaw_rate_radps=acceleration[4];
    return d;
  }
 private:
  auto ValidParameters() const -> bool {
    return std::all_of(p_.effective_mass.begin(),p_.effective_mass.end(),[](double x){return std::isfinite(x)&&x>0;}) &&
      std::all_of(p_.linear_damping.begin(),p_.linear_damping.end(),[](double x){return std::isfinite(x)&&x>=0;}) &&
      std::all_of(p_.quadratic_damping.begin(),p_.quadratic_damping.end(),[](double x){return std::isfinite(x)&&x>=0;});
  }
  UuvParameters p_;
};

inline auto Add(const UuvState& a, const UuvState& b, double scale) -> UuvState {
  return {a.north_m+scale*b.north_m,a.east_m+scale*b.east_m,a.down_m+scale*b.down_m,
    a.pitch_rad+scale*b.pitch_rad,a.yaw_rad+scale*b.yaw_rad,
    a.surge_mps+scale*b.surge_mps,a.sway_mps+scale*b.sway_mps,a.heave_mps+scale*b.heave_mps,
    a.pitch_rate_radps+scale*b.pitch_rate_radps,a.yaw_rate_radps+scale*b.yaw_rate_radps};
}

inline auto Rk4Combine(const UuvState& state,const UuvState& a,const UuvState& b,
    const UuvState& c,const UuvState& d,double dt) -> UuvState {
  auto weighted=[&](double UuvState::* member){return state.*member+dt*(a.*member+2*(b.*member)+2*(c.*member)+d.*member)/6;};
  return {weighted(&UuvState::north_m),weighted(&UuvState::east_m),weighted(&UuvState::down_m),
    weighted(&UuvState::pitch_rad),weighted(&UuvState::yaw_rad),weighted(&UuvState::surge_mps),
    weighted(&UuvState::sway_mps),weighted(&UuvState::heave_mps),weighted(&UuvState::pitch_rate_radps),
    weighted(&UuvState::yaw_rate_radps)};
}

struct UuvSample { double time_s{}; UuvState ned; Position3d platform_position; auto operator==(const UuvSample&) const -> bool = default; };
struct UuvGenerationResult { Asset asset; std::vector<UuvSample> samples; };

inline auto GenerateUuvTrajectory(std::string asset_id, std::uint64_t version, std::string track_id,
    double duration_s, double dt_s, UuvState initial, const Uuv5DofPlant& plant,
    const IUuvController& controller, UuvControl disturbance = {}) -> Result<UuvGenerationResult> {
  if(!std::isfinite(duration_s)||!std::isfinite(dt_s)||duration_s<=0||dt_s<=0||
     std::abs(std::round(duration_s/dt_s)-duration_s/dt_s)>1e-9)
    return std::unexpected(Error{ErrorCode::kInvalidArgument,"Duration must be a positive exact multiple of fixed RK4 dt"});
  const auto steps=static_cast<std::size_t>(std::llround(duration_s/dt_s));
  std::vector<Point> points; std::vector<UuvSample> samples; points.reserve(steps+1); samples.reserve(steps+1);
  UuvState state=initial;
  for(std::size_t i=0;i<=steps;++i) {
    const double time=i*dt_s;
    const Position3d position{state.north_m,state.east_m,-state.down_m};
    samples.push_back({time,state,position});
    points.push_back({SimTime::FromNanoseconds(static_cast<std::int64_t>(std::llround(time*1e9))),position});
    if(i==steps) break;
    auto f=[&](double t,const UuvState& x){ return plant.Derivative(x,controller.Command(t,x),disturbance); };
    auto k1=f(time,state); if(!k1) return std::unexpected(k1.error());
    auto k2=f(time+dt_s/2,Add(state,*k1,dt_s/2)); if(!k2) return std::unexpected(k2.error());
    auto k3=f(time+dt_s/2,Add(state,*k2,dt_s/2)); if(!k3) return std::unexpected(k3.error());
    auto k4=f(time+dt_s,Add(state,*k3,dt_s)); if(!k4) return std::unexpected(k4.error());
    state=Rk4Combine(state,*k1,*k2,*k3,*k4,dt_s);
  }
  const auto start=points.front().time;
  const auto end=points.back().time;
  auto asset=Asset::Create(std::move(asset_id),version,CoordinateSystem::kLocalCartesianMetersZUp,
    start,end,
    std::string("uuv-5dof-rk4-v1; ")+std::string(kDemoUuvParameterNotice),{{std::move(track_id),std::move(points)}});
  if(!asset) return std::unexpected(asset.error());
  return UuvGenerationResult{std::move(*asset),std::move(samples)};
}

} // namespace ns3_factory::trajectory
