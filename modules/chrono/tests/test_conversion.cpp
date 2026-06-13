/**
 * @file
 * @brief Tests for nexenne::chrono saturating duration-to-integer conversion.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>

#include <nexenne/chrono/conversion.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::to_count_sat converts without saturating when it fits") {
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(5s) == 5'000'000U);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::milliseconds>(2s) == 2000);
}

TEST_CASE("nexenne::chrono::to_count_sat clamps an overflowing positive value") {
  // 5000 seconds in microseconds is 5e9, beyond UINT32_MAX (~4.29e9).
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(5000s)
    == std::numeric_limits<std::uint32_t>::max()
  );
}

TEST_CASE("nexenne::chrono::to_count_sat clamps a negative value into an unsigned target") {
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(-5s) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat clamps both bounds for a signed target") {
  using lim = std::numeric_limits<std::int8_t>;
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(1000s) == lim::max());
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(-1000s) == lim::min());
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(5s) == 5);
}

TEST_CASE("nexenne::chrono::to_count_sat handles a floating-point rep, NaN goes to zero") {
  using fsec = std::chrono::duration<double>;
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{1.5}) == 1500);
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{nan}) == 0);
}

TEST_CASE("nexenne::chrono convenience wrappers") {
  CHECK(ch::to_us_u32(3ms) == 3000U);
  CHECK(ch::to_ms_u32(2s) == 2000U);
  // 5e6 seconds is 5e9 ms, beyond UINT32_MAX (~4.29e9), so it saturates.
  CHECK(ch::to_ms_u32(5'000'000s) == std::numeric_limits<std::uint32_t>::max());
}

}  // namespace
