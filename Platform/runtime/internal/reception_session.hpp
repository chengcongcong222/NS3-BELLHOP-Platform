#pragma once

#include <utility>

#include <ns3_factory/contracts/noise.hpp>
#include <ns3_factory/contracts/received_signal.hpp>
#include <ns3_factory/contracts/receiver_window.hpp>
#include <ns3_factory/contracts/reception.hpp>
#include <ns3_factory/contracts/rx_phy.hpp>

namespace ns3_factory::runtime::internal {

class ReceiverProcessor;

class ReceptionSession final {
 public:
  [[nodiscard]] constexpr auto reception() const noexcept
      -> const contracts::Reception& {
    return reception_;
  }

  [[nodiscard]] constexpr auto desired_signal() const noexcept
      -> const contracts::ReceivedSignal& {
    return desired_signal_;
  }

  [[nodiscard]] constexpr auto receiver_window() const noexcept
      -> const contracts::ReceiverWindow& {
    return receiver_window_;
  }

  [[nodiscard]] constexpr auto noise_observation() const noexcept
      -> const contracts::NoiseObservation& {
    return noise_observation_;
  }

  [[nodiscard]] constexpr auto decode_result() const noexcept
      -> const contracts::RxDecodeResult& {
    return decode_result_;
  }

 private:
  friend class ReceiverProcessor;

  ReceptionSession(contracts::Reception reception,
                   contracts::ReceivedSignal desired_signal,
                   contracts::ReceiverWindow receiver_window,
                   contracts::NoiseObservation noise_observation,
                   contracts::RxDecodeResult decode_result)
      : reception_(reception),
        desired_signal_(std::move(desired_signal)),
        receiver_window_(std::move(receiver_window)),
        noise_observation_(std::move(noise_observation)),
        decode_result_(std::move(decode_result)) {}

  contracts::Reception reception_;
  contracts::ReceivedSignal desired_signal_;
  contracts::ReceiverWindow receiver_window_;
  contracts::NoiseObservation noise_observation_;
  contracts::RxDecodeResult decode_result_;
};

}  // namespace ns3_factory::runtime::internal
