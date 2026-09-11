#pragma once

#include <ns3_factory/contracts/state.hpp>

namespace ns3_factory::contracts {

// Immutable run input. Equal node/time/input must produce equal finite results;
// missing nodes and out-of-coverage times are explicit errors. No hot-path I/O,
// time advancement or online model solving. Resource ownership stays outside WorldSnapshot.
// Time is supplied by the existing simulation authority.
// Velocity is a segment derivative summary, never an integration instruction.
class INodeTrajectory {
 public:
  virtual ~INodeTrajectory() = default;
  [[nodiscard]] virtual auto Query(NodeId node, SimTime time) const
      -> Result<MotionState> = 0;
};

}  // namespace ns3_factory::contracts
