/**
 * @file
 * @brief Tests for nexenne::__MODULE__.
 */

#include <doctest/doctest.h>

#include <nexenne/__MODULE__/example.hpp>

namespace {

TEST_CASE("nexenne::__MODULE__::identity") {
  using nexenne::__MODULE__::identity;

  CHECK(identity(0.0) == doctest::Approx{0.0});
  CHECK(identity(1.5) == doctest::Approx{1.5});
  CHECK(identity(-2.0) == doctest::Approx{-2.0});

  static_assert(identity(3.0) == 3.0);
}

}  // namespace
