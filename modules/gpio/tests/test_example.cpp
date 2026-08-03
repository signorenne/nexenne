/**
 * @file
 * @brief Tests for nexenne::gpio.
 */

#include <doctest/doctest.h>

#include <nexenne/gpio/example.hpp>

namespace {

TEST_CASE("nexenne::gpio::identity") {
  using nexenne::gpio::identity;

  CHECK(identity(0.0) == doctest::Approx{0.0});
  CHECK(identity(1.5) == doctest::Approx{1.5});
  CHECK(identity(-2.0) == doctest::Approx{-2.0});

  static_assert(identity(3.0) == 3.0);
}

}  // namespace
