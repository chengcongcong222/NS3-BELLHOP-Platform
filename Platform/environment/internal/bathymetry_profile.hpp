#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/errors.hpp>
#include "environment_profile.hpp"

namespace ns3_factory::environment {

// Validated range/depth profile used only by the offline Environment path.
class BathymetryProfile final {
 public:
  [[nodiscard]] static auto Create(std::vector<double> ranges_meters,
                                   std::vector<double> depths_meters)
      -> contracts::Result<BathymetryProfile> {
    if(ranges_meters.size() < 2 ||
       ranges_meters.size() != depths_meters.size()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bathymetry requires at least two matching range/depth values"});
    }
    for(std::size_t index = 0; index < ranges_meters.size(); ++index) {
      if(!std::isfinite(ranges_meters[index]) ||
         !std::isfinite(depths_meters[index]) || ranges_meters[index] < 0.0 ||
         depths_meters[index] <= 0.0 ||
         (index > 0 && ranges_meters[index - 1] >= ranges_meters[index])) {
        return std::unexpected(contracts::Error{
            contracts::ErrorCode::kInvalidArgument,
            "Bathymetry ranges must increase and all values must be finite with positive depths"});
      }
    }
    return BathymetryProfile{std::move(ranges_meters),
                             std::move(depths_meters)};
  }

  [[nodiscard]] auto ranges_meters() const noexcept -> std::span<const double> {
    return ranges_meters_;
  }

  [[nodiscard]] auto depths_meters() const noexcept -> std::span<const double> {
    return depths_meters_;
  }

  [[nodiscard]] auto DepthAt(double range_meters) const
      -> contracts::Result<double> {
    if(!std::isfinite(range_meters)) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bathymetry query range must be finite"});
    }
    if(range_meters < ranges_meters_.front() ||
       range_meters > ranges_meters_.back()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Bathymetry query is outside profile coverage"});
    }
    const auto upper =
        std::lower_bound(ranges_meters_.begin(), ranges_meters_.end(),
                         range_meters);
    if(upper == ranges_meters_.begin()) {
      return depths_meters_.front();
    }
    if(upper == ranges_meters_.end()) {
      return depths_meters_.back();
    }
    const auto upper_index =
        static_cast<std::size_t>(upper - ranges_meters_.begin());
    if(*upper == range_meters) {
      return depths_meters_[upper_index];
    }
    const auto lower_index = upper_index - 1;
    const auto weight =
        (range_meters - ranges_meters_[lower_index]) /
        (ranges_meters_[upper_index] - ranges_meters_[lower_index]);
    return depths_meters_[lower_index] * (1.0 - weight) +
           depths_meters_[upper_index] * weight;
  }

  [[nodiscard]] auto IsGeometricallyBlocked(double range_meters,
                                             double source_depth_meters,
                                             double receiver_depth_meters,
                                             std::size_t sample_count) const
      -> contracts::Result<bool> {
    if(!std::isfinite(source_depth_meters) ||
       !std::isfinite(receiver_depth_meters) || source_depth_meters < 0.0 ||
       receiver_depth_meters < 0.0 || sample_count < 2) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Bathymetry blockage query requires finite non-negative depths and at least two samples"});
    }
    if(range_meters == 0.0) {
      return false;
    }
    for(std::size_t index = 1; index < sample_count; ++index) {
      const auto weight = static_cast<double>(index) /
                          static_cast<double>(sample_count);
      const auto sample_range = range_meters * weight;
      const auto bottom_depth = DepthAt(sample_range);
      if(!bottom_depth) {
        return std::unexpected(bottom_depth.error());
      }
      const auto line_depth = source_depth_meters * (1.0 - weight) +
                              receiver_depth_meters * weight;
      if(line_depth > *bottom_depth) {
        return true;
      }
    }
    return false;
  }

 private:
  BathymetryProfile(std::vector<double> ranges_meters,
                    std::vector<double> depths_meters)
      : ranges_meters_(std::move(ranges_meters)),
        depths_meters_(std::move(depths_meters)) {}

  std::vector<double> ranges_meters_;
  std::vector<double> depths_meters_;
};

class IBathymetrySource {
 public:
  virtual ~IBathymetrySource() = default;

  [[nodiscard]] virtual auto LoadBathymetryProfile(
      const EnvironmentSourceQuery& query) const
      -> contracts::Result<BathymetryProfile> = 0;
};

// GEBCO adapters use this same offline interface. This concrete source keeps
// hand-entered and test-fixture bathymetry fully deterministic.
class ManualBathymetrySource final : public IBathymetrySource {
 public:
  [[nodiscard]] static auto Create(GeographicRegion coverage,
                                   std::string time_descriptor,
                                   BathymetryProfile profile)
      -> contracts::Result<ManualBathymetrySource> {
    if(time_descriptor.empty()) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kInvalidArgument,
          "Manual bathymetry source requires a time descriptor"});
    }
    return ManualBathymetrySource{coverage,
                                 std::move(time_descriptor),
                                 std::move(profile)};
  }

  [[nodiscard]] auto LoadBathymetryProfile(
      const EnvironmentSourceQuery& query) const
      -> contracts::Result<BathymetryProfile> override {
    if(!coverage_.Contains(query.latitude_degrees(),
                           query.longitude_degrees())) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kOutOfRange,
          "Bathymetry source query is outside manual profile coverage"});
    }
    if(query.time_descriptor() != time_descriptor_) {
      return std::unexpected(contracts::Error{
          contracts::ErrorCode::kNotFound,
          "Bathymetry source time descriptor was not found"});
    }
    return profile_;
  }

 private:
  ManualBathymetrySource(GeographicRegion coverage,
                         std::string time_descriptor,
                         BathymetryProfile profile)
      : coverage_(coverage),
        time_descriptor_(std::move(time_descriptor)),
        profile_(std::move(profile)) {}

  GeographicRegion coverage_;
  std::string time_descriptor_;
  BathymetryProfile profile_;
};

}  // namespace ns3_factory::environment
