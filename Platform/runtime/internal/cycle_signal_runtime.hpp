#pragma once

#include <utility>

#include <ns3_factory/contracts/errors.hpp>
#include <ns3_factory/contracts/identity.hpp>
#include <ns3_factory/contracts/time.hpp>

#include "internal/commit_service.hpp"
#include "internal/cycle_working_state.hpp"
#include "internal/in_flight_signal_ledger.hpp"
#include "internal/reception_disposition_applier.hpp"
#include "internal/reception_disposition_service.hpp"
#include "internal/receiver_processor.hpp"
#include "internal/reception_result_accumulator.hpp"
#include "internal/transmission_executor.hpp"
#include "internal/transmission_record.hpp"
#include "internal/transmission_record_store.hpp"
#include "internal/transmission_session.hpp"

namespace ns3_factory::runtime::internal {

// Runtime-side event handlers. This owner contains no scheduler and receives
// simulation time only through explicit Platform SimTime arguments.
class CycleSignalRuntime final {
 public:
  // Explicit PHY-only path for focused signal-lifecycle tests.
  CycleSignalRuntime(TransmissionExecutor& transmission_executor,
                     ReceiverProcessor& receiver_processor,
                     CycleWorkingState& working_state,
                     CommitService& commit_service,
                     InFlightSignalLedger& ledger,
                     ReceptionResultAccumulator& results,
                     contracts::SnapshotVersion expected_version,
                     contracts::SimTime cycle_close_time) noexcept
      : transmission_executor_(transmission_executor),
        receiver_processor_(receiver_processor),
        working_state_(working_state),
        commit_service_(commit_service),
        ledger_(ledger),
        results_(results),
        expected_version_(expected_version),
        cycle_close_time_(cycle_close_time) {}

  CycleSignalRuntime(
      TransmissionExecutor& transmission_executor,
      ReceiverProcessor& receiver_processor,
      CycleWorkingState& working_state,
      CommitService& commit_service,
      InFlightSignalLedger& ledger,
      ReceptionResultAccumulator& results,
      TransmissionRecordStore& record_store,
      const ReceptionDispositionService& disposition_service,
      ReceptionDispositionApplier& disposition_applier,
      contracts::SnapshotVersion expected_version,
      contracts::SimTime cycle_close_time) noexcept
      : transmission_executor_(transmission_executor),
        receiver_processor_(receiver_processor),
        working_state_(working_state),
        commit_service_(commit_service),
        ledger_(ledger),
        results_(results),
        record_store_(&record_store),
        disposition_service_(&disposition_service),
        disposition_applier_(&disposition_applier),
        expected_version_(expected_version),
        cycle_close_time_(cycle_close_time) {}

  [[nodiscard]] auto HandleTxStart(contracts::SimTime event_time,
                                   TransmissionExecutionRequest request)
      -> contracts::Result<TransmissionSession>;

  [[nodiscard]] auto HandleSignalArrival(
      contracts::SimTime event_time,
      contracts::ReceivedSignal signal) -> contracts::Status {
    if(event_time != signal.first_arrival_at()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "SignalArrival event time does not match signal"});
    }
    return ledger_.Insert(std::move(signal));
  }

  [[nodiscard]] auto HandleSessionFinalize(
      contracts::SimTime event_time,
      const contracts::ReceivedSignal& desired_signal) -> contracts::Status;

  [[nodiscard]] auto HandleCycleClose(contracts::SimTime close_time)
      -> contracts::Status;

 private:
  TransmissionExecutor& transmission_executor_;
  ReceiverProcessor& receiver_processor_;
  CycleWorkingState& working_state_;
  CommitService& commit_service_;
  InFlightSignalLedger& ledger_;
  ReceptionResultAccumulator& results_;
  TransmissionRecordStore* record_store_{nullptr};
  const ReceptionDispositionService* disposition_service_{nullptr};
  ReceptionDispositionApplier* disposition_applier_{nullptr};
  contracts::SnapshotVersion expected_version_;
  contracts::SimTime cycle_close_time_;
  bool committed_{false};
};

inline auto CycleSignalRuntime::HandleTxStart(
    contracts::SimTime event_time,
    TransmissionExecutionRequest request)
    -> contracts::Result<TransmissionSession> {
  if(event_time != request.started_at) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "TxStart event time does not match request"});
  }
  auto session = transmission_executor_.ExecuteTransmission(
      working_state_, std::move(request));
  if(!session) {
    return std::unexpected(session.error());
  }
  if(session->transmission().ended_at > cycle_close_time_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "Transmission extends beyond cycle close"});
  }
  for(const auto& signal : session->received_signals()) {
    if(signal.last_effect_end_at() > cycle_close_time_) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                           "Transmission signal extends beyond cycle close"});
    }
  }
  if(record_store_ != nullptr) {
    auto record = TransmissionRecord::Create(session->packet(),
                                             session->transmission());
    if(!record) {
      return std::unexpected(record.error());
    }
    const auto registered = record_store_->Register(std::move(*record));
    if(!registered) {
      return std::unexpected(registered.error());
    }
  }
  return session;
}

inline auto CycleSignalRuntime::HandleSessionFinalize(
    contracts::SimTime event_time,
    const contracts::ReceivedSignal& desired_signal) -> contracts::Status {
  if(event_time != desired_signal.last_effect_end_at()) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "SessionFinalize event time does not match signal"});
  }
  auto session = receiver_processor_.ProcessReceivedSignal(
      desired_signal, ledger_, working_state_);
  if(!session) {
    return std::unexpected(session.error());
  }
  if(record_store_ != nullptr) {
    auto disposition = disposition_service_->Decide(*session,
                                                    *record_store_);
    if(!disposition) {
      return std::unexpected(disposition.error());
    }
    results_.Append(std::move(*session));
    return disposition_applier_->Apply(std::move(*disposition));
  }
  results_.Append(std::move(*session));
  return {};
}

inline auto CycleSignalRuntime::HandleCycleClose(
    contracts::SimTime close_time) -> contracts::Status {
  if(committed_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kAlreadyExists,
                         "Planning cycle was already committed"});
  }
  if(close_time != cycle_close_time_) {
    return std::unexpected(
        contracts::Error{contracts::ErrorCode::kFailedPrecondition,
                         "CycleClose event time does not match cycle policy"});
  }
  const auto ledger_status = ledger_.ValidateReadyForCycleClose(close_time);
  if(!ledger_status) {
    return ledger_status;
  }
  const auto delta = working_state_.FinalizeDeltaSet(close_time);
  if(!delta) {
    return std::unexpected(delta.error());
  }
  const auto commit_status =
      commit_service_.CommitCycle(expected_version_, *delta);
  if(!commit_status) {
    return commit_status;
  }
  ledger_.ClearForCycleClose();
  if(record_store_ != nullptr) {
    record_store_->ClearForCycleClose();
  }
  committed_ = true;
  return {};
}

}  // namespace ns3_factory::runtime::internal
