#pragma once

#include <span>
#include <utility>
#include <vector>

#include <ns3_factory/contracts/packet.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/transmission.hpp>
#include <ns3_factory/contracts/tx_phy.hpp>

namespace ns3_factory::runtime::internal {

class TransmissionExecutor;

// Complete value-owned result of one physical send attempt. Receiver fan-out
// contributes only ReceivedSignal values; it never multiplies this session.
class TransmissionSession final {
 public:
  [[nodiscard]] auto packet() const noexcept
      -> const contracts::DigitalPacket& {
    return packet_;
  }

  [[nodiscard]] constexpr auto transmission() const noexcept
      -> const contracts::Transmission& {
    return transmission_;
  }

  [[nodiscard]] constexpr auto emission() const noexcept
      -> const contracts::TxEmission& {
    return emission_;
  }

  [[nodiscard]] auto received_signals() const noexcept
      -> std::span<const contracts::ReceivedSignal> {
    return std::span<const contracts::ReceivedSignal>{received_signals_};
  }

 private:
  friend class TransmissionExecutor;

  TransmissionSession(contracts::DigitalPacket packet,
                      contracts::Transmission transmission,
                      contracts::TxEmission emission,
                      std::vector<contracts::ReceivedSignal> received_signals)
      : packet_(std::move(packet)),
        transmission_(std::move(transmission)),
        emission_(std::move(emission)),
        received_signals_(std::move(received_signals)) {}

  contracts::DigitalPacket packet_;
  contracts::Transmission transmission_;
  contracts::TxEmission emission_;
  std::vector<contracts::ReceivedSignal> received_signals_;
};

}  // namespace ns3_factory::runtime::internal
