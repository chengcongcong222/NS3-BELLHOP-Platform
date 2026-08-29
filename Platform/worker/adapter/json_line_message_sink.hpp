#pragma once

#include <functional>
#include <ostream>

#include <ns3_factory/worker/codec/json_codec.hpp>

namespace ns3_factory::worker::adapter {

class JsonLineWorkerMessageSink final : public IWorkerMessageSink {
 public:
  explicit JsonLineWorkerMessageSink(std::ostream& output) noexcept
      : output_(output) {}

  [[nodiscard]] auto Emit(const WorkerMessage& message) noexcept
      -> contracts::Status override {
    try {
      const auto encoded = codec::EncodeWorkerMessage(message);
      if(!encoded) return std::unexpected(encoded.error());
      output_.get() << *encoded << '\n';
      output_.get().flush();
      if(!output_.get()) {
        return std::unexpected(
            contracts::Error{contracts::ErrorCode::kUnavailable,
                             "Worker stdout protocol pipe failed"});
      }
      return {};
    } catch(...) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kUnavailable,
                           "Worker stdout protocol emission failed"});
    }
  }

 private:
  std::reference_wrapper<std::ostream> output_;
};

}  // namespace ns3_factory::worker::adapter
