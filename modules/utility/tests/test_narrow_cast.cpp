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
 * run time (so the path is executed under the sanitizers).
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

TEST_CASE("narrow_cast preserves char and bool round-trips") {
  CHECK(narrow_cast<char>(65) == 'A');
  CHECK(narrow_cast<int>('A') == 65);
  CHECK(narrow_cast<int>(true) == 1);
  CHECK(narrow_cast<int>(false) == 0);
}

}  // namespace
