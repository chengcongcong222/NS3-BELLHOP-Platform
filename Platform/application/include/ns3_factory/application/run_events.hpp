#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

#include <ns3_factory/application/ids.hpp>
#include <ns3_factory/contracts/trace.hpp>

namespace ns3_factory::application {

class RunService;

class RunEventSequence final {
 public:
  using value_type = std::uint64_t;

  constexpr explicit RunEventSequence(value_type value) noexcept
      : value_(value) {}

  [[nodiscard]] static constexpr auto BeforeFirst() noexcept
      -> RunEventSequence {
    return RunEventSequence{0};
  }

  [[nodiscard]] static auto TryNextAfter(RunEventSequence latest)
      -> contracts::Result<RunEventSequence> {
    if(latest.value() == std::numeric_limits<value_type>::max()) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOverflow,
                           "Run event sequence is exhausted"});
    }
    return RunEventSequence{latest.value() + 1U};
  }

  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return value_;
  }

  auto operator<=>(const RunEventSequence&) const = default;

 private:
  value_type value_;
};

inline constexpr std::size_t kMaximumRunEventReadLimit = 256U;

struct RunEventRecord final {
  RunId run_id;
  RunEventSequence sequence;
  contracts::TraceEvent trace_event;

  auto operator==(const RunEventRecord&) const -> bool = default;
};

class RunEventSink;

class IRunEventJournal {
 public:
  virtual ~IRunEventJournal() = default;

  [[nodiscard]] virtual auto ReadAfter(
      const RunId& run_id,
      RunEventSequence cursor,
      std::size_t limit) const
      -> contracts::Result<std::vector<RunEventRecord>> = 0;

  [[nodiscard]] virtual auto GetLatestSequence(
      const RunId& run_id) const
      -> contracts::Result<RunEventSequence> = 0;

 private:
  friend class RunEventSink;

  [[nodiscard]] virtual auto Append(
      const RunId& run_id,
      const contracts::TraceEvent& event) noexcept
      -> contracts::Result<RunEventRecord> = 0;
};

class RunEventSink final : public contracts::ITraceSink {
 public:
  RunEventSink(const RunEventSink&) = delete;
  auto operator=(const RunEventSink&) -> RunEventSink& = delete;

  [[nodiscard]] auto Emit(const contracts::TraceEvent& event) noexcept
      -> contracts::Status override {
    const auto appended = journal_.get().Append(run_id_.get(), event);
    if(!appended) event_stream_complete_ = false;
    return {};
  }

  [[nodiscard]] constexpr auto event_stream_complete() const noexcept
      -> bool {
    return event_stream_complete_;
  }

 private:
  friend class RunService;

  RunEventSink(const RunId& run_id, IRunEventJournal& journal) noexcept
      : run_id_(run_id), journal_(journal) {}

  std::reference_wrapper<const RunId> run_id_;
  std::reference_wrapper<IRunEventJournal> journal_;
  bool event_stream_complete_{true};
};

class InMemoryRunEventJournal final : public IRunEventJournal {
 public:
  [[nodiscard]] auto ReadAfter(const RunId& run_id,
                               RunEventSequence cursor,
                               std::size_t limit) const
      -> contracts::Result<std::vector<RunEventRecord>> override {
    if(limit == 0U || limit > kMaximumRunEventReadLimit) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kInvalidArgument,
                           "Run event read limit is outside [1, 256]"});
    }
    const auto entry = FindEntry(run_id);
    const auto latest = entry == entries_.end() || entry->records.empty()
                            ? RunEventSequence::BeforeFirst()
                            : entry->records.back().sequence;
    if(cursor > latest) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kOutOfRange,
                           "Run event cursor is beyond the latest sequence"});
    }
    if(entry == entries_.end() || cursor == latest) {
      return std::vector<RunEventRecord>{};
    }
    const auto first = std::upper_bound(
        entry->records.begin(),
        entry->records.end(),
        cursor,
        [](RunEventSequence lhs, const RunEventRecord& rhs) {
          return lhs < rhs.sequence;
        });
    const auto count = std::min<std::size_t>(
        limit, static_cast<std::size_t>(entry->records.end() - first));
    return std::vector<RunEventRecord>{first, first + count};
  }

  [[nodiscard]] auto GetLatestSequence(const RunId& run_id) const
      -> contracts::Result<RunEventSequence> override {
    const auto entry = FindEntry(run_id);
    if(entry == entries_.end() || entry->records.empty()) {
      return RunEventSequence::BeforeFirst();
    }
    return entry->records.back().sequence;
  }

 private:
  struct Entry final {
    RunId run_id;
    std::vector<RunEventRecord> records;
  };

  [[nodiscard]] auto Append(const RunId& run_id,
                            const contracts::TraceEvent& event) noexcept
      -> contracts::Result<RunEventRecord> override {
    try {
      auto entry = FindEntry(run_id);
      if(entry == entries_.end() || entry->run_id != run_id) {
        entry = entries_.insert(entry, Entry{run_id, {}});
      }
      const auto latest = entry->records.empty()
                              ? RunEventSequence::BeforeFirst()
                              : entry->records.back().sequence;
      const auto next = RunEventSequence::TryNextAfter(latest);
      if(!next) return std::unexpected(next.error());
      RunEventRecord record{run_id, *next, event};
      entry->records.push_back(record);
      return record;
    } catch(...) {
      return std::unexpected(
          contracts::Error{contracts::ErrorCode::kUnavailable,
                           "Run event journal append failed"});
    }
  }

  [[nodiscard]] auto FindEntry(const RunId& run_id)
      -> std::vector<Entry>::iterator {
    return std::lower_bound(
        entries_.begin(),
        entries_.end(),
        run_id,
        [](const Entry& lhs, const RunId& rhs) {
          return lhs.run_id < rhs;
        });
  }

  [[nodiscard]] auto FindEntry(const RunId& run_id) const
      -> std::vector<Entry>::const_iterator {
    return std::lower_bound(
        entries_.begin(),
        entries_.end(),
        run_id,
        [](const Entry& lhs, const RunId& rhs) {
          return lhs.run_id < rhs;
        });
  }

  std::vector<Entry> entries_;
};

}  // namespace ns3_factory::application
