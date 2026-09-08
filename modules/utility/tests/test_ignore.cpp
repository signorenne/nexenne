/**
 * @file
 * @brief Tests for nexenne::utility::ignore.
 */

#include <doctest/doctest.h>

#include <stdexcept>

#include <nexenne/utility/ignore.hpp>

namespace {

using nexenne::utility::ignore;

[[nodiscard]] auto nodiscard_result() -> int {
  return 42;
}

}  // namespace

static_assert(noexcept(ignore(1)));
static_assert([] {
  auto sum{0};
  ignore(sum += 3);
  ignore(sum += 4);
  return sum == 7;
}());

TEST_CASE("nexenne::utility::ignore evaluates and drops its arguments") {
  auto calls{0};
  auto const bump{[&calls] {
    ++calls;
    return calls;
  }};

  ignore(bump());
  ignore(bump(), bump());
  CHECK(calls == 3);
}

TEST_CASE("nexenne::utility::ignore consumes a [[nodiscard]] result") {
  ignore(nodiscard_result());
  CHECK(true);
}

TEST_CASE("nexenne::utility::ignore propagates exceptions thrown while evaluating arguments") {
  auto const throwing{[]() -> int { throw std::runtime_error{"boom"}; }};
  CHECK_THROWS_AS(ignore(throwing()), std::runtime_error);
}

TEST_CASE("nexenne::utility::ignore accepts no arguments") {
  ignore();
  CHECK(true);
}
