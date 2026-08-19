#include <concepts>
#include <cstdlib>
#include <limits>
#include <type_traits>

#include <ns3_factory/contracts/time.hpp>

using ns3_factory::contracts::CheckedAdd;
using ns3_factory::contracts::CheckedSubtract;
using ns3_factory::contracts::NanosecondCount;
using ns3_factory::contracts::SimDuration;
using ns3_factory::contracts::SimTime;

template <typename T>
concept CanMakeSimTimeFromNanoseconds = requires(T value) {
  { SimTime::FromNanoseconds(value) } -> std::same_as<SimTime>;
};

template <typename T>
concept CanMakeSimDurationFromNanoseconds = requires(T value) {
  { SimDuration::FromNanoseconds(value) } -> std::same_as<SimDuration>;
};

template <typename T>
concept CanMakeSimDurationFromMicroseconds = requires(T value) {
  { SimDuration::TryFromMicroseconds(value) }
      -> std::same_as<std::optional<SimDuration>>;
};

template <typename T>
concept CanMakeSimDurationFromMilliseconds = requires(T value) {
  { SimDuration::TryFromMilliseconds(value) }
      -> std::same_as<std::optional<SimDuration>>;
};

template <typename T>
concept CanMakeSimDurationFromSeconds = requires(T value) {
  { SimDuration::TryFromSeconds(value) }
      -> std::same_as<std::optional<SimDuration>>;
};

template <typename T>
concept CanCallAllTimeFactories =
    CanMakeSimTimeFromNanoseconds<T> &&
    CanMakeSimDurationFromNanoseconds<T> &&
    CanMakeSimDurationFromMicroseconds<T> &&
    CanMakeSimDurationFromMilliseconds<T> &&
    CanMakeSimDurationFromSeconds<T>;

template <typename T>
concept CannotCallAnyTimeFactory =
    !CanMakeSimTimeFromNanoseconds<T> &&
    !CanMakeSimDurationFromNanoseconds<T> &&
    !CanMakeSimDurationFromMicroseconds<T> &&
    !CanMakeSimDurationFromMilliseconds<T> &&
    !CanMakeSimDurationFromSeconds<T>;

static_assert(CanCallAllTimeFactories<NanosecondCount>);
static_assert(CanCallAllTimeFactories<int>);
static_assert(CannotCallAnyTimeFactory<float>);
static_assert(CannotCallAnyTimeFactory<double>);
static_assert(CannotCallAnyTimeFactory<long double>);

static_assert(
    std::is_same_v<SimTime::representation_type, std::int64_t>);
static_assert(
    std::is_same_v<SimDuration::representation_type, std::int64_t>);

static_assert(SimTime::Zero().nanoseconds() == 0);
static_assert(SimDuration::Zero().nanoseconds() == 0);
static_assert(SimTime::FromNanoseconds(25).nanoseconds() == 25);
static_assert(SimDuration::FromNanoseconds(-25).nanoseconds() == -25);
static_assert(SimTime::FromNanoseconds(-1).nanoseconds() == -1);

static_assert(SimTime::FromNanoseconds(2) > SimTime::FromNanoseconds(1));
static_assert(SimDuration::FromNanoseconds(-1) < SimDuration::Zero());

constexpr auto kOneMillisecond = SimDuration::TryFromMilliseconds(1);
static_assert(kOneMillisecond.has_value());
static_assert(kOneMillisecond->nanoseconds() == 1'000'000);

constexpr auto kNegativeMillisecond = SimDuration::TryFromMilliseconds(-1);
static_assert(kNegativeMillisecond.has_value());
static_assert(kNegativeMillisecond->nanoseconds() == -1'000'000);

constexpr auto kDurationSum =
    CheckedAdd(SimDuration::FromNanoseconds(3),
               SimDuration::FromNanoseconds(4));
static_assert(kDurationSum.has_value());
static_assert(kDurationSum->nanoseconds() == 7);

constexpr auto kAdvancedTime =
    CheckedAdd(SimTime::FromNanoseconds(10),
               SimDuration::FromNanoseconds(5));
static_assert(kAdvancedTime.has_value());
static_assert(kAdvancedTime->nanoseconds() == 15);

constexpr auto kDifference =
    CheckedSubtract(SimTime::FromNanoseconds(10),
                    SimTime::FromNanoseconds(15));
static_assert(kDifference.has_value());
static_assert(kDifference->nanoseconds() == -5);

constexpr auto kMax = std::numeric_limits<NanosecondCount>::max();
constexpr auto kMin = std::numeric_limits<NanosecondCount>::min();

static_assert(!CheckedAdd(SimDuration::FromNanoseconds(kMax),
                          SimDuration::FromNanoseconds(1))
                   .has_value());
static_assert(!CheckedAdd(SimTime::FromNanoseconds(kMax),
                          SimDuration::FromNanoseconds(1))
                   .has_value());
static_assert(!CheckedSubtract(SimDuration::FromNanoseconds(kMin),
                               SimDuration::FromNanoseconds(1))
                   .has_value());
static_assert(!CheckedSubtract(SimTime::FromNanoseconds(kMax),
                               SimTime::FromNanoseconds(kMin))
                   .has_value());
static_assert(!SimDuration::TryFromSeconds(kMax).has_value());

int main() {
  const auto result =
      CheckedSubtract(SimTime::FromNanoseconds(9),
                      SimDuration::FromNanoseconds(4));
  return result && result->nanoseconds() == 5 ? EXIT_SUCCESS : EXIT_FAILURE;
}
