/**
 * @file
 * @brief Tests for nexenne::chrono saturating duration-to-integer conversion.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <ratio>

#include <nexenne/chrono/conversion.hpp>

namespace {

namespace ch = nexenne::chrono;
using namespace std::chrono_literals;

TEST_CASE("nexenne::chrono::to_count_sat converts without saturating when it fits") {
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(5s) == 5'000'000U);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::milliseconds>(2s) == 2000);
}

TEST_CASE("nexenne::chrono::to_count_sat clamps an overflowing positive value") {
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
  CHECK(ch::to_ms_u32(5'000'000s) == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("nexenne::chrono::to_count_sat is usable in a constant expression") {
  static_assert(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(5s) == 5'000'000U);
  static_assert(
    ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(5000s)
    == std::numeric_limits<std::uint32_t>::max()
  );
  static_assert(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(-5s) == 0U);
  static_assert(
    ch::to_count_sat<std::int8_t, std::chrono::seconds>(-1000s)
    == std::numeric_limits<std::int8_t>::min()
  );
  static_assert(ch::to_us_u32(3ms) == 3000U);
  static_assert(ch::to_ms_u32(2s) == 2000U);
  CHECK(true);
}

TEST_CASE("nexenne::chrono::to_count_sat maps a zero duration to zero for every target") {
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(0s) == 0U);
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::milliseconds>(0ms) == 0);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::seconds>(0s) == 0);
  CHECK(ch::to_us_u32(0s) == 0U);
  CHECK(ch::to_ms_u32(0s) == 0U);
  using fsec = std::chrono::duration<double>;
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{0.0}) == 0);
  static_assert(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(0s) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat truncates a sub-unit duration toward zero") {
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(1500us) == 1U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(1999us) == 1U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(999us) == 0U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(-1500us) == 0U);
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(-1500us) == -1);
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(-1999us) == -1);
}

TEST_CASE("nexenne::chrono::to_count_sat handles the exact bound without clamping") {
  using us = std::chrono::microseconds;
  constexpr auto top{std::numeric_limits<std::uint32_t>::max()};
  CHECK(ch::to_count_sat<std::uint32_t, us>(us{top}) == top);
  CHECK(ch::to_count_sat<std::uint32_t, us>(us{static_cast<std::int64_t>(top) + 1}) == top);
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{127}) == 127);
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{-128}) == -128);
  CHECK(
    ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{128})
    == std::numeric_limits<std::int8_t>::max()
  );
}

TEST_CASE("nexenne::chrono::to_count_sat saturates a narrowing without wrapping") {
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::seconds>(std::chrono::seconds::max())
    == std::numeric_limits<std::uint32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::seconds>(std::chrono::seconds::max())
    == std::numeric_limits<std::int32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::seconds>(std::chrono::seconds::min())
    == std::numeric_limits<std::int32_t>::min()
  );
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::seconds>(std::chrono::seconds::min()) == 0U);
  CHECK(
    ch::to_count_sat<std::uint64_t, std::chrono::seconds>(std::chrono::seconds{1'000'000})
    == 1'000'000U
  );
}

TEST_CASE("nexenne::chrono::to_count_sat moves an unsigned ToDur count through the wide path") {
  using ums = std::chrono::duration<std::uint64_t, std::milli>;
  CHECK(ch::to_count_sat<std::int32_t, ums>(1s) == 1000);
  CHECK(
    ch::to_count_sat<std::int32_t, ums>(std::chrono::hours{2'000'000})
    == std::numeric_limits<std::int32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::uint16_t, ums>(std::chrono::seconds{70})
    == std::numeric_limits<std::uint16_t>::max()
  );
  CHECK(ch::to_count_sat<std::uint64_t, ums>(42ms) == 42U);
}

TEST_CASE("nexenne::chrono::to_count_sat widening into a larger signed target never clamps") {
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::seconds>(std::chrono::seconds{1234}) == 1234);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::seconds>(std::chrono::seconds{-1234}) == -1234);
}

TEST_CASE("nexenne::chrono::to_count_sat with a floating ToDur rep saturates and zeros NaN") {
  using fms = std::chrono::duration<double, std::milli>;
  CHECK(ch::to_count_sat<std::int32_t, fms>(2s) == 2000);
  CHECK(ch::to_count_sat<std::uint32_t, fms>(-2s) == 0U);
  CHECK(ch::to_count_sat<std::uint8_t, fms>(1s) == std::numeric_limits<std::uint8_t>::max());
}

TEST_CASE("nexenne::chrono::to_count_sat saturates infinities on a floating source") {
  using fsec = std::chrono::duration<double>;
  auto const inf{std::numeric_limits<double>::infinity()};
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{inf})
    == std::numeric_limits<std::int32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{inf})
    == std::numeric_limits<std::uint32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{-inf})
    == std::numeric_limits<std::int32_t>::min()
  );
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{-inf}) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat truncates a fractional floating count toward zero") {
  using fsec = std::chrono::duration<double>;
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{1.9999}) == 1999);
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{-1.9999}) == -1999);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{-0.5}) == 0U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::seconds>(fsec{0.4}) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat clamps an enormous float beyond the target range") {
  using fsec = std::chrono::duration<double>;
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::seconds>(fsec{1e30})
    == std::numeric_limits<std::uint32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::seconds>(fsec{-1e30})
    == std::numeric_limits<std::int32_t>::min()
  );
}

TEST_CASE("nexenne::chrono::to_count_sat round-trips a count through duration and back") {
  constexpr std::uint32_t value{123'456};
  auto const dur{std::chrono::microseconds{value}};
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(dur) == value);
  CHECK(ch::to_us_u32(dur) == value);
}

TEST_CASE("nexenne::chrono::to_count_sat float rep into a floating ToDur saturates NaN to zero") {
  using fsec = std::chrono::duration<double>;
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  using fms = std::chrono::duration<double, std::milli>;
  CHECK(ch::to_count_sat<std::int32_t, fms>(fsec{nan}) == 0);
  CHECK(ch::to_count_sat<std::int32_t, fms>(fsec{2.5}) == 2500);
}

TEST_CASE("nexenne::chrono::to_count_sat clamps before the cast on a coarse-to-fine extreme") {
  auto const big{std::chrono::seconds{std::numeric_limits<std::int64_t>::max() / 1000}};
  CHECK(ch::to_us_u32(big) == std::numeric_limits<std::uint32_t>::max());
  auto const r{ch::to_count_sat<std::int64_t, std::chrono::microseconds>(big)};
  CHECK(r > std::int64_t{9'000'000'000'000'000'000});
  auto const neg{std::chrono::seconds{std::numeric_limits<std::int64_t>::min() / 1000}};
  auto const rn{ch::to_count_sat<std::int64_t, std::chrono::microseconds>(neg)};
  CHECK(rn < std::int64_t{-9'000'000'000'000'000'000});
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(neg) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat coarse-to-fine clamp is usable at compile time") {
  constexpr auto v{
    ch::to_us_u32(std::chrono::seconds{std::numeric_limits<std::int64_t>::max() / 1000})
  };
  static_assert(v == std::numeric_limits<std::uint32_t>::max());
  CHECK(v == std::numeric_limits<std::uint32_t>::max());
}

TEST_CASE("nexenne::chrono::to_count_sat converts an unsigned source rep exactly") {
  using u64_seconds = std::chrono::duration<std::uint64_t>;
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(u64_seconds{5}) == 5'000'000U);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::microseconds>(u64_seconds{5}) == 5'000'000);
  CHECK(
    ch::to_count_sat<std::uint64_t, std::chrono::milliseconds>(
      std::chrono::duration<std::uint32_t>{std::numeric_limits<std::uint32_t>::max()}
    )
    == std::uint64_t{std::numeric_limits<std::uint32_t>::max()} * 1000U
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::nanoseconds>(
      std::chrono::duration<std::uint64_t>{std::numeric_limits<std::uint64_t>::max()}
    )
    == std::numeric_limits<std::int32_t>::max()
  );
}

TEST_CASE("nexenne::chrono::to_count_sat saturates into a non-integral tick period") {
  // 32.768 kHz: one tick is 1e9 / 32768 = 1953125 / 64 ns.
  using rtc_tick = std::chrono::duration<std::int64_t, std::ratio<1, 32768>>;
  CHECK(
    ch::to_count_sat<std::uint32_t, rtc_tick>(std::chrono::nanoseconds::max())
    == std::numeric_limits<std::uint32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int64_t, rtc_tick>(std::chrono::nanoseconds::max())
    == std::chrono::nanoseconds::max().count() / 1'953'125 * 64
         + (std::chrono::nanoseconds::max().count() % 1'953'125) * 64 / 1'953'125
  );
  CHECK(ch::to_count_sat<std::int64_t, rtc_tick>(std::chrono::seconds{1}) == 32768);
  CHECK(ch::to_count_sat<std::int64_t, rtc_tick>(std::chrono::nanoseconds{-30'518}) == -1);
  CHECK(
    ch::to_count_sat<std::int32_t, rtc_tick>(std::chrono::nanoseconds::min())
    == std::numeric_limits<std::int32_t>::min()
  );
}

static_assert(
  ch::to_count_sat<std::int64_t, std::chrono::duration<std::int64_t, std::ratio<1, 32768>>>(
    std::chrono::nanoseconds::max()
  )
  > 0
);

}  // namespace
