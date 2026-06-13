/**
 * @file
 * @brief Tests for nexenne::chrono frequency helpers.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>

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

}  // namespace
