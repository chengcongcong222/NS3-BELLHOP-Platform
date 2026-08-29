#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <ns3_factory/worker/protocol.hpp>

namespace ns3_factory::worker::codec {

inline constexpr std::size_t kMaximumInputLineBytes = 1U << 20U;
inline constexpr std::size_t kMaximumOutputMessageBytes = 4U << 20U;
inline constexpr std::uint32_t kWorkerWireSchemaVersion = 1U;

[[nodiscard]] auto EncodeStartRunCommand(const StartRunCommand& command)
    -> contracts::Result<std::string>;

[[nodiscard]] auto DecodeStartRunCommand(std::string_view line)
    -> contracts::Result<StartRunCommand>;

[[nodiscard]] auto EncodeWorkerMessage(const WorkerMessage& message)
    -> contracts::Result<std::string>;

[[nodiscard]] auto DecodeWorkerMessage(std::string_view line)
    -> contracts::Result<WorkerMessage>;

}  // namespace ns3_factory::worker::codec
