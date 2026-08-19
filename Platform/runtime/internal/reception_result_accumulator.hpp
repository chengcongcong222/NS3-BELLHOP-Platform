#pragma once

#include <algorithm>
#include <span>
#include <utility>
#include <vector>

#include "internal/reception_session.hpp"

namespace ns3_factory::runtime::internal {

class ReceptionResultAccumulator final {
 public:
  auto Append(ReceptionSession session) -> void {
    sessions_.push_back(std::move(session));
    std::sort(sessions_.begin(), sessions_.end(), CanonicalLess);
  }

  [[nodiscard]] auto sessions() const noexcept
      -> std::span<const ReceptionSession> {
    return std::span<const ReceptionSession>{sessions_};
  }

 private:
  [[nodiscard]] static auto CanonicalLess(const ReceptionSession& lhs,
                                          const ReceptionSession& rhs)
      noexcept -> bool {
    if(lhs.reception().arrival_at != rhs.reception().arrival_at) {
      return lhs.reception().arrival_at < rhs.reception().arrival_at;
    }
    if(lhs.reception().receiver_node_id !=
       rhs.reception().receiver_node_id) {
      return lhs.reception().receiver_node_id <
             rhs.reception().receiver_node_id;
    }
    if(lhs.reception().transmission_id !=
       rhs.reception().transmission_id) {
      return lhs.reception().transmission_id <
             rhs.reception().transmission_id;
    }
    return lhs.reception().reception_id < rhs.reception().reception_id;
  }

  std::vector<ReceptionSession> sessions_;
};

}  // namespace ns3_factory::runtime::internal
