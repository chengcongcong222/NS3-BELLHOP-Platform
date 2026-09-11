#pragma once

#include <bit>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <ns3_factory/contracts/trajectory.hpp>

namespace ns3_factory::trajectory {
using namespace contracts;

enum class CoordinateSystem { kLocalCartesianMetersZUp = 1 };
struct Point { SimTime time; Position3d position; auto operator==(const Point&) const -> bool = default; };
struct Track { std::string id; std::vector<Point> points; };
struct Reference {
  std::string asset_id;
  std::uint64_t version;
  std::string track_id;
};

inline auto ValidId(std::string_view id) -> bool {
  return !id.empty() && id.size() <= 128 && id != "." && id != ".." &&
      std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
               (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.';
      });
}
inline auto Finite(Position3d p) -> bool {
  return std::isfinite(p.x_meters) && std::isfinite(p.y_meters) && std::isfinite(p.z_meters);
}

class Asset final {
 public:
  static auto Create(std::string id, std::uint64_t version, CoordinateSystem coordinates,
                     SimTime start, SimTime end, std::string source, std::vector<Track> tracks)
      -> Result<Asset> {
    if(!ValidId(id) || version == 0 || coordinates != CoordinateSystem::kLocalCartesianMetersZUp ||
       start < SimTime::Zero() || end < start || source.empty() || tracks.empty())
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "Invalid trajectory metadata"});
    std::sort(tracks.begin(), tracks.end(), [](const Track& a, const Track& b) { return a.id < b.id; });
    for(std::size_t i = 0; i < tracks.size(); ++i) {
      const auto& track = tracks[i];
      if(!ValidId(track.id) || (i && tracks[i-1].id == track.id) || track.points.empty() ||
         track.points.front().time != start ||
         (track.points.size() > 1 && track.points.back().time != end))
        return std::unexpected(Error{ErrorCode::kInvalidArgument, "Invalid trajectory track coverage/identity"});
      for(std::size_t j = 0; j < track.points.size(); ++j) {
        const auto& point = track.points[j];
        if(!Finite(point.position) || point.time < start || point.time > end ||
           (j && point.time <= track.points[j-1].time))
          return std::unexpected(Error{ErrorCode::kInvalidArgument, "Trajectory points must be finite and strictly increasing"});
      }
    }
    return Asset{std::move(id), version, coordinates, start, end, std::move(source), std::move(tracks)};
  }
  auto id() const -> const std::string& { return id_; }
  auto version() const -> std::uint64_t { return version_; }
  auto start() const -> SimTime { return start_; }
  auto end() const -> SimTime { return end_; }
  auto coordinates() const -> CoordinateSystem { return coordinates_; }
  auto source() const -> const std::string& { return source_; }
  auto tracks() const -> std::span<const Track> { return tracks_; }
  auto Find(std::string_view id) const -> const Track* {
    const auto it = std::lower_bound(tracks_.begin(), tracks_.end(), id,
        [](const Track& t, std::string_view key) { return t.id < key; });
    return it != tracks_.end() && it->id == id ? &*it : nullptr;
  }
  auto Query(std::string_view id, SimTime time) const -> Result<MotionState> {
    const auto* track = Find(id);
    if(!track) return std::unexpected(Error{ErrorCode::kNotFound, "Trajectory track not found"});
    if(time < start_ || time > end_)
      return std::unexpected(Error{ErrorCode::kOutOfRange, "Time outside trajectory coverage"});
    const auto& points = track->points;
    if(points.size() == 1) return MotionState{points.front().position, {0,0,0}};
    auto right = std::upper_bound(points.begin(), points.end(), time,
        [](SimTime t, const Point& p) { return t < p.time; });
    if(right == points.end()) --right;
    const auto& a = *(right-1);
    const auto& b = *right;
    const auto dt = b.time.nanoseconds() - a.time.nanoseconds();
    const auto alpha = static_cast<double>(time.nanoseconds()-a.time.nanoseconds()) / static_cast<double>(dt);
    const auto seconds = static_cast<long double>(dt) / 1'000'000'000.0L;
    MotionState result{{std::lerp(a.position.x_meters,b.position.x_meters,alpha),
                        std::lerp(a.position.y_meters,b.position.y_meters,alpha),
                        std::lerp(a.position.z_meters,b.position.z_meters,alpha)},
                       {static_cast<double>((static_cast<long double>(b.position.x_meters)-a.position.x_meters)/seconds),
                        static_cast<double>((static_cast<long double>(b.position.y_meters)-a.position.y_meters)/seconds),
                        static_cast<double>((static_cast<long double>(b.position.z_meters)-a.position.z_meters)/seconds)}};
    if(!Finite(result.position) || !std::isfinite(result.velocity.x_meters_per_second) ||
       !std::isfinite(result.velocity.y_meters_per_second) || !std::isfinite(result.velocity.z_meters_per_second))
      return std::unexpected(Error{ErrorCode::kOverflow, "Trajectory interpolation overflow"});
    return result;
  }
  // Canonical FNV-1a64 over explicit little-endian words and length-prefixed text.
  // Accidental corruption/content identity only, not authentication.
  auto checksum() const -> std::uint64_t {
    std::uint64_t hash = 14695981039346656037ULL;
    auto word = [&](std::uint64_t value) { for(int i=0;i<8;++i) { hash ^= value & 255; hash *= 1099511628211ULL; value >>= 8; } };
    auto text = [&](std::string_view value) { word(value.size()); for(unsigned char c : value) { hash ^= c; hash *= 1099511628211ULL; } };
    word(1); text(id_); word(version_); word(static_cast<std::uint64_t>(coordinates_));
    word(start_.nanoseconds()); word(end_.nanoseconds()); text(source_); word(tracks_.size());
    for(const auto& track : tracks_) {
      text(track.id); word(track.points.size());
      for(const auto& p : track.points) {
        word(p.time.nanoseconds());
        for(double v : {p.position.x_meters,p.position.y_meters,p.position.z_meters})
          word(std::bit_cast<std::uint64_t>(v == 0 ? 0.0 : v));
      }
    }
    return hash;
  }
 private:
  Asset(std::string id, std::uint64_t version, CoordinateSystem coordinates, SimTime start,
        SimTime end, std::string source, std::vector<Track> tracks)
      : id_(std::move(id)), version_(version), coordinates_(coordinates), start_(start), end_(end),
        source_(std::move(source)), tracks_(std::move(tracks)) {}
  std::string id_;
  std::uint64_t version_;
  CoordinateSystem coordinates_;
  SimTime start_, end_;
  std::string source_;
  std::vector<Track> tracks_;
};

class Repository final {
 public:
  auto Register(Asset asset) -> Status {
    const auto key = std::make_pair(asset.id(), asset.version());
    if(assets_.contains(key)) return std::unexpected(Error{ErrorCode::kAlreadyExists, "Trajectory version already registered"});
    assets_.emplace(key, std::make_shared<const Asset>(std::move(asset)));
    return {};
  }
  auto Load(std::string_view id, std::uint64_t version) const -> Result<std::shared_ptr<const Asset>> {
    const auto it = assets_.find({std::string(id), version});
    if(it == assets_.end()) return std::unexpected(Error{ErrorCode::kNotFound, "Trajectory asset/version not found"});
    return it->second;
  }
 private:
  std::map<std::pair<std::string,std::uint64_t>,std::shared_ptr<const Asset>> assets_;
};

struct NodeBinding { NodeId node_id; Reference trajectory; };
class BoundTrajectories final : public INodeTrajectory {
 public:
  static auto Create(const Repository& repository, std::vector<NodeBinding> bindings)
      -> Result<std::shared_ptr<const BoundTrajectories>> {
    auto result = std::shared_ptr<BoundTrajectories>(new BoundTrajectories);
    for(const auto& binding : bindings) {
      auto asset = repository.Load(binding.trajectory.asset_id, binding.trajectory.version);
      if(!asset) return std::unexpected(asset.error());
      if(!(*asset)->Find(binding.trajectory.track_id))
        return std::unexpected(Error{ErrorCode::kNotFound, "Bound track not found"});
      if(!result->bindings_.emplace(binding.node_id, Entry{*asset, binding.trajectory.track_id}).second)
        return std::unexpected(Error{ErrorCode::kInvalidArgument, "Duplicate node trajectory binding"});
    }
    return result;
  }
  auto Query(NodeId node, SimTime time) const -> Result<MotionState> override {
    const auto it = bindings_.find(node);
    if(it == bindings_.end()) return std::unexpected(Error{ErrorCode::kNotFound, "Node has no trajectory binding"});
    return it->second.asset->Query(it->second.track, time);
  }
 private:
  struct Entry { std::shared_ptr<const Asset> asset; std::string track; };
  std::map<NodeId,Entry> bindings_;
};
}  // namespace ns3_factory::trajectory
