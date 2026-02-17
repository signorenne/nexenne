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

// Added coverage

TEST_CASE("nexenne::chrono::to_count_sat is usable in a constant expression") {
  // constexpr facts: in-range, positive saturation, negative-into-unsigned.
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
  // 1500 us cast to milliseconds truncates to 1 ms, not rounds to 2.
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(1500us) == 1U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(1999us) == 1U);
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(999us) == 0U);
  // Negative truncates toward zero too: -1500 us -> -1 ms, clamped to 0 unsigned.
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(-1500us) == 0U);
  // Into a signed target the truncated magnitude survives with its sign.
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(-1500us) == -1);
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(-1999us) == -1);
}

TEST_CASE("nexenne::chrono::to_count_sat handles the exact bound without clamping") {
  // UINT32_MAX microseconds expressed directly fits exactly.
  using us = std::chrono::microseconds;
  constexpr auto top{std::numeric_limits<std::uint32_t>::max()};
  CHECK(ch::to_count_sat<std::uint32_t, us>(us{top}) == top);
  // One past the top saturates, not wraps.
  CHECK(ch::to_count_sat<std::uint32_t, us>(us{static_cast<std::int64_t>(top) + 1}) == top);
  // int8 exact bounds.
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{127}) == 127);
  CHECK(ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{-128}) == -128);
  CHECK(
    ch::to_count_sat<std::int8_t, std::chrono::seconds>(std::chrono::seconds{128})
    == std::numeric_limits<std::int8_t>::max()
  );
}

TEST_CASE("nexenne::chrono::to_count_sat saturates a narrowing without wrapping") {
  // The saturation contract guards the narrowing from the ToDur count to Int.
  // ToDur == seconds avoids overflowing the duration_cast itself (which is
  // outside the function's documented protection), isolating the narrow step:
  // seconds::max() (~9.2e18) narrows into uint32/int32 and must clamp to max.
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::seconds>(std::chrono::seconds::max())
    == std::numeric_limits<std::uint32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::seconds>(std::chrono::seconds::max())
    == std::numeric_limits<std::int32_t>::max()
  );
  // Most negative seconds saturates the signed-target minimum, clamps unsigned to 0.
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::seconds>(std::chrono::seconds::min())
    == std::numeric_limits<std::int32_t>::min()
  );
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::seconds>(std::chrono::seconds::min()) == 0U);
  // A widening narrow-step (int64 source into uint64 target) keeps the value.
  CHECK(
    ch::to_count_sat<std::uint64_t, std::chrono::seconds>(std::chrono::seconds{1'000'000})
    == 1'000'000U
  );
}

TEST_CASE("nexenne::chrono::to_count_sat moves an unsigned ToDur count through the wide path") {
  // A ToDur whose rep is unsigned exercises the unsigned-intermediate branch:
  // duration_cast keeps the count unsigned, then the narrowing saturates.
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
  // int16 worth of seconds into an int64: identity, no clamp.
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::seconds>(std::chrono::seconds{1234}) == 1234);
  CHECK(ch::to_count_sat<std::int64_t, std::chrono::seconds>(std::chrono::seconds{-1234}) == -1234);
}

TEST_CASE("nexenne::chrono::to_count_sat with a floating ToDur rep saturates and zeros NaN") {
  // ToDur itself carries a floating rep: the integral-source branch routes the
  // count through saturate_from_ld.
  using fms = std::chrono::duration<double, std::milli>;
  CHECK(ch::to_count_sat<std::int32_t, fms>(2s) == 2000);
  CHECK(ch::to_count_sat<std::uint32_t, fms>(-2s) == 0U);
  // A huge integral source cast to a floating ms count clamps at the target max.
  CHECK(ch::to_count_sat<std::uint8_t, fms>(1s) == std::numeric_limits<std::uint8_t>::max());
}

TEST_CASE("nexenne::chrono::to_count_sat saturates infinities on a floating source") {
  using fsec = std::chrono::duration<double>;
  auto const inf{std::numeric_limits<double>::infinity()};
  // +inf clamps to max for both signedness.
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{inf})
    == std::numeric_limits<std::int32_t>::max()
  );
  CHECK(
    ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{inf})
    == std::numeric_limits<std::uint32_t>::max()
  );
  // -inf clamps to min (signed) and to 0 (unsigned).
  CHECK(
    ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{-inf})
    == std::numeric_limits<std::int32_t>::min()
  );
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{-inf}) == 0U);
}

TEST_CASE("nexenne::chrono::to_count_sat truncates a fractional floating count toward zero") {
  using fsec = std::chrono::duration<double>;
  // 1.9999 s -> 1999.9 ms -> truncates to 1999.
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{1.9999}) == 1999);
  CHECK(ch::to_count_sat<std::int32_t, std::chrono::milliseconds>(fsec{-1.9999}) == -1999);
  // A negative fractional value into an unsigned target clamps to zero.
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::milliseconds>(fsec{-0.5}) == 0U);
  // A small positive fractional below one unit truncates to zero.
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
  // For an in-range value the count survives the saturating cast unchanged.
  constexpr std::uint32_t value{123'456};
  auto const dur{std::chrono::microseconds{value}};
  CHECK(ch::to_count_sat<std::uint32_t, std::chrono::microseconds>(dur) == value);
  CHECK(ch::to_us_u32(dur) == value);
}

TEST_CASE("nexenne::chrono::to_count_sat float rep into a floating ToDur saturates NaN to zero") {
  using fsec = std::chrono::duration<double>;
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  // Floating source always uses the long-double path regardless of ToDur rep.
  using fms = std::chrono::duration<double, std::milli>;
  CHECK(ch::to_count_sat<std::int32_t, fms>(fsec{nan}) == 0);
  CHECK(ch::to_count_sat<std::int32_t, fms>(fsec{2.5}) == 2500);
}

}  // namespace
