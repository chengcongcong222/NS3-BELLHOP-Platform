#pragma once

#include <cstdint>
#include <functional>
#include <utility>

#include <ns3/nstime.h>
#include <ns3/simulator.h>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/time.hpp>

namespace ns3_factory::kernel::internal {

// The only Platform boundary that converts time or operates ns-3's
// Simulator. All conversions use integer nanoseconds.
class Ns3KernelGateway final {
 public:
  [[nodiscard]] static auto ToNs3Time(contracts::SimTime time)
      -> contracts::Result<ns3::Time>;

  [[nodiscard]] static auto ToNs3Time(contracts::SimDuration duration)
      -> contracts::Result<ns3::Time>;

  [[nodiscard]] static auto FromNs3Time(const ns3::Time& time)
      -> contracts::Result<contracts::SimTime>;

  [[nodiscard]] static auto FromNs3Duration(const ns3::Time& duration)
      -> contracts::Result<contracts::SimDuration>;

  [[nodiscard]] static auto CheckedDelay(contracts::SimTime origin,
                                         contracts::SimTime target)
      -> contracts::Result<ns3::Time>;

  [[nodiscard]] auto PlatformNow() const
      -> contracts::Result<contracts::SimTime>;

  [[nodiscard]] auto ScheduleAt(contracts::SimTime target,
                                std::function<void()> callback) const
      -> contracts::Status;

  auto Run() const -> void {
    ns3::Simulator::Run();
  }

  auto Stop() const -> void {
    ns3::Simulator::Stop();
  }

  auto Destroy() const -> void {
    ns3::Simulator::Destroy();
  }

 private:
  [[nodiscard]] static auto ValidateResolution() -> contracts::Status;
};

inline auto Ns3KernelGateway::ValidateResolution() -> contracts::Status {
  if(ns3::Time::GetResolution() != ns3::Time::NS) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "ns-3 Time resolution must be nanoseconds"});
  }
  return {};
}

inline auto Ns3KernelGateway::ToNs3Time(contracts::SimTime time)
    -> contracts::Result<ns3::Time> {
  const auto resolution = ValidateResolution();
  if(!resolution) {
    return std::unexpected(resolution.error());
  }
  const ns3::Time converted{
      static_cast<long long>(time.nanoseconds())};
  if(converted.GetNanoSeconds() != time.nanoseconds()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "SimTime cannot be represented exactly by ns-3"});
  }
  return converted;
}

inline auto Ns3KernelGateway::ToNs3Time(contracts::SimDuration duration)
    -> contracts::Result<ns3::Time> {
  const auto resolution = ValidateResolution();
  if(!resolution) {
    return std::unexpected(resolution.error());
  }
  const ns3::Time converted{
      static_cast<long long>(duration.nanoseconds())};
  if(converted.GetNanoSeconds() != duration.nanoseconds()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "SimDuration cannot be represented exactly by ns-3"});
  }
  return converted;
}

inline auto Ns3KernelGateway::FromNs3Time(const ns3::Time& time)
    -> contracts::Result<contracts::SimTime> {
  const auto resolution = ValidateResolution();
  if(!resolution) {
    return std::unexpected(resolution.error());
  }
  const auto nanoseconds = time.GetNanoSeconds();
  const auto converted = contracts::SimTime::FromNanoseconds(nanoseconds);
  const auto round_trip = ToNs3Time(converted);
  if(!round_trip || *round_trip != time) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "ns-3 Time cannot round-trip through SimTime"});
  }
  return converted;
}

inline auto Ns3KernelGateway::FromNs3Duration(const ns3::Time& duration)
    -> contracts::Result<contracts::SimDuration> {
  const auto resolution = ValidateResolution();
  if(!resolution) {
    return std::unexpected(resolution.error());
  }
  const auto nanoseconds = duration.GetNanoSeconds();
  const auto converted =
      contracts::SimDuration::FromNanoseconds(nanoseconds);
  const auto round_trip = ToNs3Time(converted);
  if(!round_trip || *round_trip != duration) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "ns-3 Time cannot round-trip through SimDuration"});
  }
  return converted;
}

inline auto Ns3KernelGateway::CheckedDelay(contracts::SimTime origin,
                                           contracts::SimTime target)
    -> contracts::Result<ns3::Time> {
  if(target < origin) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOutOfRange,
                         "Cannot schedule an event before the origin time"});
  }
  const auto delay = contracts::CheckedSubtract(target, origin);
  if(!delay) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kOverflow,
                         "Event scheduling delay overflow"});
  }
  return ToNs3Time(*delay);
}

inline auto Ns3KernelGateway::PlatformNow() const
    -> contracts::Result<contracts::SimTime> {
  return FromNs3Time(ns3::Simulator::Now());
}

inline auto Ns3KernelGateway::ScheduleAt(
    contracts::SimTime target,
    std::function<void()> callback) const -> contracts::Status {
  const auto now = PlatformNow();
  if(!now) {
    return std::unexpected(now.error());
  }
  const auto delay = CheckedDelay(*now, target);
  if(!delay) {
    return std::unexpected(delay.error());
  }
  ns3::Simulator::Schedule(*delay, std::move(callback));
  return {};
}

}  // namespace ns3_factory::kernel::internal
