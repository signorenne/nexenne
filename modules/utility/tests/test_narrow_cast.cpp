/**
 * @file
 * @brief Tests for nexenne::utility::narrow_cast.
 */

#include <doctest/doctest.h>

#include <cstdint>

#include <nexenne/utility/narrow_cast.hpp>

namespace {

TEST_CASE("nexenne::utility::narrow_cast preserves in-range values") {
  using nexenne::utility::narrow_cast;

  CHECK(narrow_cast<std::int16_t>(std::int32_t{300}) == 300);
  CHECK(narrow_cast<std::uint8_t>(std::int32_t{255}) == 255);
  CHECK(narrow_cast<std::int8_t>(std::int32_t{-128}) == -128);
  CHECK(narrow_cast<float>(double{1.5}) == doctest::Approx{1.5});

  static_assert(narrow_cast<std::int8_t>(std::int32_t{127}) == 127);
  static_assert(narrow_cast<std::int8_t>(std::int32_t{-128}) == -128);
  static_assert(narrow_cast<std::uint8_t>(std::int32_t{255}) == 255);
  static_assert(narrow_cast<int>(5) == 5);  // same-type / widening: no change
  static_assert(narrow_cast<std::uint32_t>(std::uint64_t{42}) == 42);
  static_assert(narrow_cast<float>(2.0) == 2.0F);  // double -> float, exactly representable
}

}  // namespace
