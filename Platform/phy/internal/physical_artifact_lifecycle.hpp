#pragma once

namespace ns3_factory::phy::internal {

// Assembly uses this narrow boundary to bound transient physical artifacts
// without learning their representation. Implementations must not own or
// advance simulation time.
class IPhysicalArtifactLifecycle {
 public:
  virtual ~IPhysicalArtifactLifecycle() = default;

  virtual void ReleaseCycleArtifacts() noexcept = 0;
};

}  // namespace ns3_factory::phy::internal
