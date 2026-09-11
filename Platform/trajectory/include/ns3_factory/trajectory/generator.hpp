#pragma once

#include "asset.hpp"

namespace ns3_factory::trajectory {

// Offline compatibility generator, not called by runtime position queries.
inline auto GenerateConstantVelocity(const WorldSnapshot& initial, SimTime end,
                                    std::string id, std::uint64_t version)
    -> Result<Asset> {
  const auto duration = CheckedSubtract(end, initial.committed_at());
  if(!duration || duration->nanoseconds() <= 0)
    return std::unexpected(Error{ErrorCode::kInvalidArgument, "Generator requires a positive bounded interval"});
  const auto seconds = static_cast<double>(duration->nanoseconds()) / 1'000'000'000.0;
  std::vector<Track> tracks;
  for(const auto& node : initial.nodes()) {
    const auto& p = node.motion.position;
    const auto& v = node.motion.velocity;
    if(!std::isfinite(v.x_meters_per_second) || !std::isfinite(v.y_meters_per_second) ||
       !std::isfinite(v.z_meters_per_second))
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "Generator velocity must be finite"});
    Track track{"N" + std::to_string(node.node_id.value()), {{initial.committed_at(), p}}};
    if(v != Velocity3d{0,0,0}) {
      track.points.push_back({end, {p.x_meters + v.x_meters_per_second * seconds,
                                   p.y_meters + v.y_meters_per_second * seconds,
                                   p.z_meters + v.z_meters_per_second * seconds}});
    }
    tracks.push_back(std::move(track));
  }
  return Asset::Create(std::move(id), version, CoordinateSystem::kLocalCartesianMetersZUp,
      initial.committed_at(), end, "constant-velocity-generator-v1; input=initial-state; units=m,s", std::move(tracks));
}

inline auto BindGenerated(const WorldSnapshot& initial, SimTime end)
    -> Result<std::shared_ptr<const BoundTrajectories>> {
  auto asset = GenerateConstantVelocity(initial, end, "generated-initial-trajectories", 1);
  if(!asset) return std::unexpected(asset.error());
  std::vector<NodeBinding> bindings;
  for(const auto& node : initial.nodes())
    bindings.push_back({node.node_id, {asset->id(), asset->version(), "N" + std::to_string(node.node_id.value())}});
  Repository repository;
  auto registered = repository.Register(std::move(*asset));
  if(!registered) return std::unexpected(registered.error());
  return BoundTrajectories::Create(repository, std::move(bindings));
}
}  // namespace ns3_factory::trajectory
