#pragma once

#include <array>
#include <charconv>
#include <limits>
#include "asset.hpp"

namespace ns3_factory::trajectory {
// Narrow unquoted CSV v1. Decimal seconds have at most nine fractional digits;
// exponent notation, negative time and sub-nanosecond precision are rejected.
inline auto ParseSeconds(std::string_view text) -> Result<SimTime> {
  const auto dot = text.find('.');
  const auto whole = text.substr(0, dot);
  const auto fraction = dot == text.npos ? std::string_view{} : text.substr(dot+1);
  auto digits = [](std::string_view s) { return std::all_of(s.begin(), s.end(), [](char c) { return c >= '0' && c <= '9'; }); };
  if(whole.empty() || !digits(whole) || !digits(fraction) || fraction.size() > 9 ||
     (dot != text.npos && fraction.empty()))
    return std::unexpected(Error{ErrorCode::kInvalidArgument, "CSV time must be nonnegative decimal seconds with nanosecond precision"});
  std::uint64_t seconds = 0, nanos = 0;
  if(std::from_chars(whole.data(), whole.data()+whole.size(), seconds).ec != std::errc{})
    return std::unexpected(Error{ErrorCode::kOverflow, "CSV seconds overflow"});
  for(char c : fraction) nanos = nanos*10 + static_cast<unsigned>(c-'0');
  for(std::size_t i=fraction.size(); i<9; ++i) nanos *= 10;
  const auto maximum = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
  if(seconds > (maximum-nanos)/1'000'000'000)
    return std::unexpected(Error{ErrorCode::kOverflow, "CSV nanoseconds overflow"});
  return SimTime::FromNanoseconds(static_cast<std::int64_t>(seconds*1'000'000'000+nanos));
}

inline auto ImportCsv(std::string_view csv, std::string id, std::uint64_t version,
                      CoordinateSystem coordinates, std::string source)
    -> Result<Asset> {
  if(csv.size() > 16*1024*1024)
    return std::unexpected(Error{ErrorCode::kOutOfRange, "CSV exceeds 16 MiB import limit"});
  auto line = [&]() {
    const auto end = csv.find('\n');
    auto result = csv.substr(0,end);
    csv = end == csv.npos ? std::string_view{} : csv.substr(end+1);
    if(!result.empty() && result.back() == '\r') result.remove_suffix(1);
    return result;
  };
  if(line() != "track_id,time_s,x_m,y_m,z_m")
    return std::unexpected(Error{ErrorCode::kInvalidArgument, "Unsupported CSV header/units"});
  std::map<std::string,std::vector<Point>> tracks;
  while(!csv.empty()) {
    auto row = line();
    std::array<std::string_view,5> fields;
    for(std::size_t i=0;i<fields.size();++i) {
      const auto comma = row.find(',');
      if((i == 4) != (comma == row.npos))
        return std::unexpected(Error{ErrorCode::kInvalidArgument, "CSV requires exactly five columns"});
      fields[i] = row.substr(0,comma);
      if(comma != row.npos) row.remove_prefix(comma+1);
    }
    if(!ValidId(fields[0])) return std::unexpected(Error{ErrorCode::kInvalidArgument, "Invalid CSV track id"});
    auto time = ParseSeconds(fields[1]);
    if(!time) return std::unexpected(time.error());
    std::array<double,3> xyz;
    for(std::size_t i=0;i<3;++i) {
      const auto value = fields[i+2];
      const auto parsed = std::from_chars(value.data(),value.data()+value.size(),xyz[i]);
      if(parsed.ec != std::errc{} || parsed.ptr != value.data()+value.size() || !std::isfinite(xyz[i]))
        return std::unexpected(Error{ErrorCode::kInvalidArgument, "CSV coordinates must be finite metres"});
    }
    tracks[std::string(fields[0])].push_back({*time,{xyz[0],xyz[1],xyz[2]}});
  }
  if(tracks.empty()) return std::unexpected(Error{ErrorCode::kInvalidArgument, "CSV has no tracks"});
  std::vector<Track> canonical;
  for(auto& [name,points] : tracks) {
    std::sort(points.begin(),points.end(),[](const Point& a,const Point& b) { return a.time < b.time; });
    canonical.push_back({name,std::move(points)});
  }
  const auto start = canonical.front().points.front().time;
  const auto end = canonical.front().points.back().time;
  // CSV tracks must share their explicit coverage. Fixed tracks can repeat their
  // position at the common end; single-row imports have point-only coverage.
  for(const auto& track : canonical)
    if(track.points.front().time != start || track.points.back().time != end)
      return std::unexpected(Error{ErrorCode::kInvalidArgument, "CSV tracks must share start/end coverage"});
  return Asset::Create(std::move(id),version,coordinates,start,end,std::move(source),std::move(canonical));
}
}  // namespace ns3_factory::trajectory
