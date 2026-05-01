/**
 * @file
 * @brief Tests for nexenne::utility::narrow_cast.
 *
 * narrow_cast checks value preservation with an \c assert in debug builds, so
 * the failure path aborts rather than returning; it cannot be exercised from a
 * normal test without a death test. These tests therefore cover the full
 * in-range surface exhaustively: the type matrix, exact boundary values, the
 * widening / same-type identity, cross-sign positive values, and floating
 * point, both at compile time (\c static_assert, the real guarantee) and at
 * run time (so the path is executed under the sanitizers). The reject side of
 * the float-to-integral range check is covered by probing the detail
 * classifier directly, which returns instead of asserting.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <limits>

#include <nexenne/utility/narrow_cast.hpp>

namespace {

using nexenne::utility::narrow_cast;

// The cast is unconditionally noexcept and constexpr.
static_assert(noexcept(narrow_cast<std::int8_t>(0)));

// Same-type and widening conversions never change the value.
static_assert(narrow_cast<int>(5) == 5);
static_assert(narrow_cast<std::int64_t>(std::int32_t{-7}) == -7);
static_assert(narrow_cast<std::uint64_t>(std::uint32_t{42}) == 42u);
static_assert(narrow_cast<double>(3.0F) == 3.0);

// Exact boundary values of the target type round-trip cleanly.
static_assert(narrow_cast<std::int8_t>(127) == std::numeric_limits<std::int8_t>::max());
static_assert(narrow_cast<std::int8_t>(-128) == std::numeric_limits<std::int8_t>::min());
static_assert(narrow_cast<std::uint8_t>(255) == std::numeric_limits<std::uint8_t>::max());
static_assert(narrow_cast<std::uint8_t>(0) == 0u);
static_assert(narrow_cast<std::int16_t>(32767) == 32767);
static_assert(narrow_cast<std::int16_t>(-32768) == -32768);
static_assert(narrow_cast<std::uint16_t>(65535) == 65535u);

// Cross-sign conversions are allowed as long as the value and sign survive.
static_assert(narrow_cast<unsigned>(5) == 5u);
static_assert(narrow_cast<int>(5u) == 5);
static_assert(narrow_cast<std::uint32_t>(std::int64_t{0}) == 0u);

// Float-to-integral narrowing at the exact target boundaries: the range check
// runs before the cast, so these are well-defined even in a constant
// expression (where any UB would be a compile error, making these
// static_asserts the strongest possible no-UB witness).
static_assert(narrow_cast<std::int8_t>(127.0) == 127);
static_assert(narrow_cast<std::int8_t>(-128.0) == -128);
static_assert(narrow_cast<std::uint8_t>(255.0) == 255u);
static_assert(narrow_cast<std::uint8_t>(0.0) == 0u);
static_assert(narrow_cast<std::int32_t>(2147483647.0) == 2147483647);
static_assert(narrow_cast<std::int32_t>(-2147483648.0) == -2147483648);
static_assert(narrow_cast<std::uint32_t>(4294967295.0) == 4294967295u);
// The largest float below 2^31 (2^31 - 128, exactly representable).
static_assert(narrow_cast<std::int32_t>(2147483520.0F) == 2147483520);

// The classifier behind the pre-cast range check, probed directly so the
// reject side (which would assert inside narrow_cast) is covered too. The
// bounds are exact powers of two, so the first out-of-range integer on either
// side must classify as false.
namespace detail = nexenne::utility::detail;

static_assert(detail::float_in_integral_range<std::int8_t>(127.0));
static_assert(!detail::float_in_integral_range<std::int8_t>(128.0));
static_assert(detail::float_in_integral_range<std::int8_t>(-128.0));
static_assert(!detail::float_in_integral_range<std::int8_t>(-129.0));
static_assert(detail::float_in_integral_range<std::uint8_t>(255.0));
static_assert(!detail::float_in_integral_range<std::uint8_t>(256.0));
static_assert(!detail::float_in_integral_range<std::uint8_t>(-1.0));
// (-1, 0) truncates to zero, so the range check admits it; the round-trip
// assert inside narrow_cast is what rejects the value change afterwards.
static_assert(detail::float_in_integral_range<std::uint8_t>(-0.5));
// 2^63 rounds to itself as a double, one past the signed maximum.
static_assert(!detail::float_in_integral_range<std::int64_t>(9223372036854775808.0));
static_assert(detail::float_in_integral_range<std::int64_t>(-9223372036854775808.0));
// 2^64 - 2048 is the largest double below 2^64.
static_assert(detail::float_in_integral_range<std::uint64_t>(18446744073709549568.0));
static_assert(!detail::float_in_integral_range<std::uint64_t>(18446744073709551616.0));

// NaN compares false against both bounds, so it is classified out of range for
// every integral target (the documented NaN behaviour: assert in debug).
static_assert(!detail::float_in_integral_range<std::int32_t>(std::numeric_limits<double>::quiet_NaN()
));
static_assert(!detail::float_in_integral_range<std::uint32_t>(std::numeric_limits<float>::quiet_NaN()
));
static_assert(!detail::float_in_integral_range<std::int8_t>(std::numeric_limits<double>::infinity())
);
static_assert(!detail::float_in_integral_range<std::int8_t>(-std::numeric_limits<double>::infinity()
));

TEST_CASE("narrow_cast preserves in-range integer values at run time") {
  CHECK(narrow_cast<std::int16_t>(std::int32_t{300}) == 300);
  CHECK(narrow_cast<std::uint8_t>(std::int32_t{255}) == 255);
  CHECK(narrow_cast<std::int8_t>(std::int32_t{-128}) == -128);
  CHECK(narrow_cast<std::int8_t>(std::int32_t{0}) == 0);
}

TEST_CASE("narrow_cast hits each integer target type's boundaries") {
  CHECK(narrow_cast<std::int8_t>(127) == 127);
  CHECK(narrow_cast<std::int8_t>(-128) == -128);
  CHECK(narrow_cast<std::uint8_t>(255) == 255);
  CHECK(narrow_cast<std::int16_t>(-32768) == -32768);
  CHECK(narrow_cast<std::uint16_t>(65535) == 65535);
  CHECK(
    narrow_cast<std::int32_t>(std::int64_t{std::numeric_limits<std::int32_t>::min()})
    == std::numeric_limits<std::int32_t>::min()
  );
  CHECK(
    narrow_cast<std::uint32_t>(std::uint64_t{std::numeric_limits<std::uint32_t>::max()})
    == std::numeric_limits<std::uint32_t>::max()
  );
}

TEST_CASE("narrow_cast on widening and same-type conversions is the identity") {
  CHECK(narrow_cast<int>(5) == 5);
  CHECK(narrow_cast<long long>(std::int8_t{-3}) == -3);
  CHECK(narrow_cast<std::uint64_t>(std::uint8_t{200}) == 200u);
}

TEST_CASE("narrow_cast across signedness keeps positive values and sign") {
  CHECK(narrow_cast<unsigned>(0) == 0u);
  CHECK(narrow_cast<unsigned>(123) == 123u);
  CHECK(narrow_cast<int>(123u) == 123);
  CHECK(narrow_cast<std::int8_t>(std::uint8_t{100}) == 100);
  CHECK(narrow_cast<std::uint8_t>(std::int8_t{100}) == 100u);
}

TEST_CASE("narrow_cast on floating point: exactly representable values") {
  CHECK(narrow_cast<float>(1.5) == doctest::Approx{1.5});
  CHECK(narrow_cast<float>(2.0) == 2.0F);
  CHECK(narrow_cast<float>(-0.0) == 0.0F);
  CHECK(narrow_cast<int>(42.0) == 42);
  CHECK(narrow_cast<int>(-42.0F) == -42);
  CHECK(
    narrow_cast<double>(std::numeric_limits<float>::max())
    == static_cast<double>(std::numeric_limits<float>::max())
  );
}

TEST_CASE("narrow_cast on floating point: integral targets at their boundaries") {
  // Executed at run time so the pre-cast range check runs under the
  // sanitizers: none of these may reach an out-of-range float-to-int cast.
  CHECK(narrow_cast<std::int8_t>(127.0) == 127);
  CHECK(narrow_cast<std::int8_t>(-128.0) == -128);
  CHECK(narrow_cast<std::uint8_t>(255.0) == 255);
  CHECK(narrow_cast<std::int32_t>(2147483647.0) == std::numeric_limits<std::int32_t>::max());
  CHECK(narrow_cast<std::int32_t>(-2147483648.0) == std::numeric_limits<std::int32_t>::min());
  CHECK(narrow_cast<std::uint32_t>(4294967295.0) == std::numeric_limits<std::uint32_t>::max());
  CHECK(narrow_cast<std::int32_t>(2147483520.0F) == 2147483520);
  CHECK(narrow_cast<std::int64_t>(-9223372036854775808.0) == std::numeric_limits<std::int64_t>::min());
}

TEST_CASE("narrow_cast float range classifier rejects the first value past each bound") {
  namespace detail = nexenne::utility::detail;
  CHECK(detail::float_in_integral_range<std::int16_t>(32767.0));
  CHECK_FALSE(detail::float_in_integral_range<std::int16_t>(32768.0));
  CHECK(detail::float_in_integral_range<std::int16_t>(-32768.0));
  CHECK_FALSE(detail::float_in_integral_range<std::int16_t>(-32769.0));
  CHECK_FALSE(detail::float_in_integral_range<std::uint16_t>(-1.0));
  CHECK_FALSE(detail::float_in_integral_range<int>(std::numeric_limits<double>::quiet_NaN()));
  CHECK_FALSE(detail::float_in_integral_range<int>(std::numeric_limits<double>::infinity()));
}

TEST_CASE("narrow_cast preserves char and bool round-trips") {
  CHECK(narrow_cast<char>(65) == 'A');
  CHECK(narrow_cast<int>('A') == 65);
  CHECK(narrow_cast<int>(true) == 1);
  CHECK(narrow_cast<int>(false) == 0);
}

}  // namespace
