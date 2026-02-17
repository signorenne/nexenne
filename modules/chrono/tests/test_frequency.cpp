/**
 * @file
 * @brief Tests for nexenne::chrono frequency helpers.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>

#include <nexenne/chrono/frequency.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::period_from yields the exact tick period") {
  constexpr auto p{ch::period_from<ch::hertz<1000>>()};  // 1 kHz -> 1 ms
  static_assert(p == std::chrono::milliseconds{1});
  CHECK(p == std::chrono::milliseconds{1});

  constexpr auto p2{ch::period_from<ch::hertz<1>>()};  // 1 Hz -> 1 s
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(p2) == 1s);
}

TEST_CASE("nexenne::chrono::hertz_from inverts a period, truncating") {
  static_assert(ch::hertz_from(1ms) == 1000);
  CHECK(ch::hertz_from(1ms) == 1000);
  CHECK(ch::hertz_from(1s) == 1);
  CHECK(ch::hertz_from(std::chrono::microseconds{1}) == 1'000'000);
  CHECK(ch::hertz_from(0ms) == 0);   // non-positive period -> 0
  CHECK(ch::hertz_from(1min) == 0);  // sub-1-Hz truncates to 0
}

TEST_CASE("nexenne::chrono runtime hz/period conversions saturate on zero") {
  CHECK(ch::period_ns_from(1'000'000'000ULL) == 1ns);
  CHECK(ch::period_us_from(1'000'000ULL) == 1us);
  CHECK(ch::period_ns_from(0) == std::chrono::nanoseconds::max());
  CHECK(ch::period_us_from(0) == std::chrono::microseconds::max());
  CHECK(ch::hz_from_ns(1ns) == 1'000'000'000ULL);
  CHECK(ch::hz_from_ns(0ns) == 0);
}

// Added coverage

TEST_CASE("nexenne::chrono::hertz wraps a compile-time value") {
  static_assert(ch::hertz<0>::value == 0);
  static_assert(ch::hertz<440>::value == 440);
  static_assert(
    ch::hertz<std::numeric_limits<std::uint64_t>::max()>::value
    == std::numeric_limits<std::uint64_t>::max()
  );
  // The wrapped value is an unsigned 64-bit integer.
  static_assert(std::is_same_v<decltype(ch::hertz<7>::value), std::uint64_t const>);
  CHECK(ch::hertz<440>::value == 440);
}

TEST_CASE("nexenne::chrono::period_from gives an exact, strictly positive duration") {
  // 2 Hz -> period 1/2 s, represented as duration<int64, ratio<1,2>> with count 1.
  constexpr auto p2{ch::period_from<ch::hertz<2>>()};
  static_assert(p2.count() == 1);
  static_assert(p2 == std::chrono::duration<std::int64_t, std::ratio<1, 2>>{1});
  // The period of 2 Hz cast to a finer unit is 500 ms.
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(p2) == 500ms);

  // 1 MHz -> 1 us exactly.
  constexpr auto pm{ch::period_from<ch::hertz<1'000'000>>()};
  CHECK(std::chrono::duration_cast<std::chrono::microseconds>(pm) == 1us);

  // 1 GHz -> 1 ns exactly.
  constexpr auto pg{ch::period_from<ch::hertz<1'000'000'000>>()};
  CHECK(std::chrono::duration_cast<std::chrono::nanoseconds>(pg) == 1ns);
}

TEST_CASE("nexenne::chrono::period_from then hertz_from round-trips for exact divisors") {
  // For frequencies whose period casts exactly to nanoseconds, Hz -> period -> Hz
  // returns the original. 1 kHz/1 MHz/1 GHz all have integer-ns periods.
  constexpr auto p_khz{ch::period_from<ch::hertz<1000>>()};
  CHECK(ch::hertz_from(p_khz) == 1000);
  constexpr auto p_mhz{ch::period_from<ch::hertz<1'000'000>>()};
  CHECK(ch::hertz_from(p_mhz) == 1'000'000);
  constexpr auto p_ghz{ch::period_from<ch::hertz<1'000'000'000>>()};
  CHECK(ch::hertz_from(p_ghz) == 1'000'000'000);
  constexpr auto p_1hz{ch::period_from<ch::hertz<1>>()};
  CHECK(ch::hertz_from(p_1hz) == 1);
}

TEST_CASE("nexenne::chrono::hertz_from rejects negative and zero periods") {
  CHECK(ch::hertz_from(-1ms) == 0);
  CHECK(ch::hertz_from(-1s) == 0);
  CHECK(ch::hertz_from(std::chrono::nanoseconds{-5}) == 0);
  CHECK(ch::hertz_from(0s) == 0);
  static_assert(ch::hertz_from(-1ms) == 0);
  static_assert(ch::hertz_from(0ms) == 0);
}

TEST_CASE("nexenne::chrono::hertz_from truncates sub-hertz multi-tick periods to zero") {
  CHECK(ch::hertz_from(2s) == 0);      // 0.5 Hz truncates to 0
  CHECK(ch::hertz_from(1500ms) == 0);  // 0.66 Hz truncates to 0
  CHECK(ch::hertz_from(3s) == 0);
  // Exactly 1 Hz survives, just above does not.
  CHECK(ch::hertz_from(1s) == 1);
  CHECK(ch::hertz_from(1001ms) == 0);
}

TEST_CASE("nexenne::chrono::hertz_from handles a multi-tick fine period") {
  // 250 ns -> 4 MHz, exact.
  CHECK(ch::hertz_from(std::chrono::nanoseconds{250}) == 4'000'000);
  // 3 us -> 333333 Hz (truncated from 333333.3).
  CHECK(ch::hertz_from(std::chrono::microseconds{3}) == 333'333);
  // 7 ms -> 142 Hz (truncated from 142.85).
  CHECK(ch::hertz_from(std::chrono::milliseconds{7}) == 142);
}

TEST_CASE("nexenne::chrono::period_ns_from inverts hertz to a nanosecond period") {
  CHECK(ch::period_ns_from(1) == 1s);
  CHECK(ch::period_ns_from(1000) == 1ms);
  CHECK(ch::period_ns_from(1'000'000) == 1us);
  CHECK(ch::period_ns_from(1'000'000'000) == 1ns);
  // A very high Hz beyond 1 GHz truncates the integer-ns period to zero.
  CHECK(ch::period_ns_from(2'000'000'000ULL) == 0ns);
  // 3 Hz -> 333333333 ns (truncated).
  CHECK(ch::period_ns_from(3) == std::chrono::nanoseconds{333'333'333});
  static_assert(ch::period_ns_from(0) == std::chrono::nanoseconds::max());
  static_assert(ch::period_ns_from(1'000'000'000) == 1ns);
}

TEST_CASE("nexenne::chrono::period_us_from inverts hertz to a microsecond period") {
  CHECK(ch::period_us_from(1) == 1s);
  CHECK(ch::period_us_from(1000) == 1ms);
  CHECK(ch::period_us_from(1'000'000) == 1us);
  // Beyond 1 MHz the integer-us period truncates to zero.
  CHECK(ch::period_us_from(2'000'000ULL) == 0us);
  // 3 Hz -> 333333 us (truncated).
  CHECK(ch::period_us_from(3) == std::chrono::microseconds{333'333});
  static_assert(ch::period_us_from(0) == std::chrono::microseconds::max());
  static_assert(ch::period_us_from(1'000'000) == 1us);
}

TEST_CASE("nexenne::chrono::hz_from_ns inverts a nanosecond period to hertz") {
  CHECK(ch::hz_from_ns(1s) == 1);
  CHECK(ch::hz_from_ns(1ms) == 1000);
  CHECK(ch::hz_from_ns(1us) == 1'000'000);
  CHECK(ch::hz_from_ns(1ns) == 1'000'000'000ULL);
  CHECK(ch::hz_from_ns(0ns) == 0);
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{-5}) == 0);
  // 250 ns -> 4 MHz, 3 ns -> 333333333 Hz (truncated).
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{250}) == 4'000'000);
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{3}) == 333'333'333ULL);
  static_assert(ch::hz_from_ns(1ns) == 1'000'000'000ULL);
  static_assert(ch::hz_from_ns(0ns) == 0);
}

TEST_CASE("nexenne::chrono hz -> ns-period -> hz round-trips for divisors of 1e9") {
  // Frequencies that divide 1e9 evenly survive the runtime round-trip exactly.
  for (std::uint64_t const hz : {1ULL, 2ULL, 4ULL, 5ULL, 8ULL, 1000ULL, 1'000'000ULL}) {
    auto const period{ch::period_ns_from(hz)};
    CHECK(ch::hz_from_ns(period) == hz);
  }
}

TEST_CASE("nexenne::chrono::period_ns_from handles the very low frequency of 1 Hz family") {
  // Low Hz keep a large but in-range ns period.
  CHECK(ch::period_ns_from(2) == std::chrono::nanoseconds{500'000'000});
  CHECK(ch::period_ns_from(4) == std::chrono::nanoseconds{250'000'000});
  // hz_from_ns recovers the exact divisor frequency.
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{500'000'000}) == 2);
}

}  // namespace
