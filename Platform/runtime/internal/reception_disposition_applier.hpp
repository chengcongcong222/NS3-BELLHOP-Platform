#pragma once

#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

#include <ns3_factory/contracts/errors.hpp>

#include "internal/application_delivery_store.hpp"
#include "internal/packet_queue_store.hpp"
#include "internal/reception_disposition.hpp"

namespace ns3_factory::runtime::internal {

class ReceptionDispositionApplier final {
 public:
  ReceptionDispositionApplier(
      PacketQueueStore& queue_store,
      ApplicationDeliveryStore& delivery_store) noexcept
      : queue_store_(queue_store), delivery_store_(delivery_store) {}

  [[nodiscard]] auto Apply(ReceptionDisposition disposition)
      -> contracts::Status;

 private:
  std::reference_wrapper<PacketQueueStore> queue_store_;
  std::reference_wrapper<ApplicationDeliveryStore> delivery_store_;
};

inline auto ReceptionDispositionApplier::Apply(
    ReceptionDisposition disposition) -> contracts::Status {
  return std::visit(
      [this](auto reception) -> contracts::Status {
        using Reception = decltype(reception);
        if constexpr(std::is_same_v<Reception, NotDecodedReception> ||
                     std::is_same_v<Reception, OverheardReception>) {
          return {};
        } else if constexpr(std::is_same_v<Reception,
                                                LocalDeliveryReception>) {
          return delivery_store_.get().Append(std::move(reception));
        } else {
          return queue_store_.get().Enqueue(reception.receiver_node_id,
                                            std::move(reception.packet));
        }
      },
      std::move(disposition));
}

}  // namespace ns3_factory::runtime::internal
