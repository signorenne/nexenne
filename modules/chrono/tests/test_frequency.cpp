/**
 * @file
 * @brief Tests for nexenne::chrono frequency helpers.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>

#include <nexenne/chrono/frequency.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::period_from yields the exact tick period") {
  constexpr auto p{ch::period_from<ch::hertz<1000>>()};
  static_assert(p == std::chrono::milliseconds{1});
  CHECK(p == std::chrono::milliseconds{1});

  constexpr auto p2{ch::period_from<ch::hertz<1>>()};
  CHECK(std::chrono::duration_cast<std::chrono::seconds>(p2) == 1s);
}

TEST_CASE("nexenne::chrono::hertz_from inverts a period, truncating") {
  static_assert(ch::hertz_from(1ms) == 1000);
  CHECK(ch::hertz_from(1ms) == 1000);
  CHECK(ch::hertz_from(1s) == 1);
  CHECK(ch::hertz_from(std::chrono::microseconds{1}) == 1'000'000);
  CHECK(ch::hertz_from(0ms) == 0);
  CHECK(ch::hertz_from(1min) == 0);
}

TEST_CASE("nexenne::chrono runtime hz/period conversions saturate on zero") {
  CHECK(ch::period_ns_from(1'000'000'000ULL) == 1ns);
  CHECK(ch::period_us_from(1'000'000ULL) == 1us);
  CHECK(ch::period_ns_from(0) == std::chrono::nanoseconds::max());
  CHECK(ch::period_us_from(0) == std::chrono::microseconds::max());
  CHECK(ch::hz_from_ns(1ns) == 1'000'000'000ULL);
  CHECK(ch::hz_from_ns(0ns) == 0);
}

TEST_CASE("nexenne::chrono::hertz wraps a compile-time value") {
  static_assert(ch::hertz<0>::value == 0);
  static_assert(ch::hertz<440>::value == 440);
  static_assert(
    ch::hertz<std::numeric_limits<std::uint64_t>::max()>::value
    == std::numeric_limits<std::uint64_t>::max()
  );
  static_assert(std::is_same_v<decltype(ch::hertz<7>::value), std::uint64_t const>);
  CHECK(ch::hertz<440>::value == 440);
}

TEST_CASE("nexenne::chrono::period_from gives an exact, strictly positive duration") {
  constexpr auto p2{ch::period_from<ch::hertz<2>>()};
  static_assert(p2.count() == 1);
  static_assert(p2 == std::chrono::duration<std::int64_t, std::ratio<1, 2>>{1});
  CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(p2) == 500ms);

  constexpr auto pm{ch::period_from<ch::hertz<1'000'000>>()};
  CHECK(std::chrono::duration_cast<std::chrono::microseconds>(pm) == 1us);

  constexpr auto pg{ch::period_from<ch::hertz<1'000'000'000>>()};
  CHECK(std::chrono::duration_cast<std::chrono::nanoseconds>(pg) == 1ns);
}

TEST_CASE("nexenne::chrono::period_from then hertz_from round-trips for exact divisors") {
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
  CHECK(ch::hertz_from(2s) == 0);
  CHECK(ch::hertz_from(1500ms) == 0);
  CHECK(ch::hertz_from(3s) == 0);
  CHECK(ch::hertz_from(1s) == 1);
  CHECK(ch::hertz_from(1001ms) == 0);
}

TEST_CASE("nexenne::chrono::hertz_from handles a multi-tick fine period") {
  CHECK(ch::hertz_from(std::chrono::nanoseconds{250}) == 4'000'000);
  CHECK(ch::hertz_from(std::chrono::microseconds{3}) == 333'333);
  CHECK(ch::hertz_from(std::chrono::milliseconds{7}) == 142);
}

TEST_CASE("nexenne::chrono::period_ns_from inverts hertz to a nanosecond period") {
  CHECK(ch::period_ns_from(1) == 1s);
  CHECK(ch::period_ns_from(1000) == 1ms);
  CHECK(ch::period_ns_from(1'000'000) == 1us);
  CHECK(ch::period_ns_from(1'000'000'000) == 1ns);
  CHECK(ch::period_ns_from(2'000'000'000ULL) == 0ns);
  CHECK(ch::period_ns_from(3) == std::chrono::nanoseconds{333'333'333});
  static_assert(ch::period_ns_from(0) == std::chrono::nanoseconds::max());
  static_assert(ch::period_ns_from(1'000'000'000) == 1ns);
}

TEST_CASE("nexenne::chrono::period_us_from inverts hertz to a microsecond period") {
  CHECK(ch::period_us_from(1) == 1s);
  CHECK(ch::period_us_from(1000) == 1ms);
  CHECK(ch::period_us_from(1'000'000) == 1us);
  CHECK(ch::period_us_from(2'000'000ULL) == 0us);
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
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{250}) == 4'000'000);
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{3}) == 333'333'333ULL);
  static_assert(ch::hz_from_ns(1ns) == 1'000'000'000ULL);
  static_assert(ch::hz_from_ns(0ns) == 0);
}

TEST_CASE("nexenne::chrono hz -> ns-period -> hz round-trips for divisors of 1e9") {
  for (std::uint64_t const hz : {1ULL, 2ULL, 4ULL, 5ULL, 8ULL, 1000ULL, 1'000'000ULL}) {
    auto const period{ch::period_ns_from(hz)};
    CHECK(ch::hz_from_ns(period) == hz);
  }
}

TEST_CASE("nexenne::chrono::period_ns_from handles the very low frequency of 1 Hz family") {
  CHECK(ch::period_ns_from(2) == std::chrono::nanoseconds{500'000'000});
  CHECK(ch::period_ns_from(4) == std::chrono::nanoseconds{250'000'000});
  CHECK(ch::hz_from_ns(std::chrono::nanoseconds{500'000'000}) == 2);
}

TEST_CASE("nexenne::chrono::hertz_from guards a product overflow without UB") {
  // num * count = 2^32 * 2^32 = 2^64, which wraps to zero.
  using big_period = std::chrono::duration<std::int64_t, std::ratio<4'294'967'296, 1>>;
  CHECK(ch::hertz_from(big_period{4'294'967'296}) == 0);
  CHECK(ch::hertz_from(std::chrono::seconds{2}) == 0);
  CHECK(ch::hertz_from(std::chrono::milliseconds{1}) == 1000);
}

TEST_CASE("nexenne::chrono::hertz_from inverts a floating period below one tick") {
  CHECK(ch::hertz_from(std::chrono::duration<double>{0.5}) == 2);
  CHECK(ch::hertz_from(std::chrono::duration<double, std::milli>{0.25}) == 4000);
  CHECK(
    ch::hertz_from(std::chrono::duration<double>{1e-30})
    == std::numeric_limits<std::uint64_t>::max()
  );
  CHECK(
    ch::hertz_from(std::chrono::duration<double>{std::numeric_limits<double>::quiet_NaN()}) == 0
  );
}

static_assert(ch::hertz_from(std::chrono::duration<double>{0.5}) == 2);

}  // namespace
