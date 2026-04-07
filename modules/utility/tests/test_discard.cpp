/**
 * @file
 * @brief Tests for nexenne::utility::discard.
 */

#include <doctest/doctest.h>

#include <stdexcept>

#include <nexenne/utility/discard.hpp>

namespace {

using nexenne::utility::discard;

[[nodiscard]] auto nodiscard_result() -> int {
  return 42;
}

}  // namespace

// discard is unconditionally noexcept and usable at compile time.
static_assert(noexcept(discard(1)));
static_assert([] {
  auto sum{0};
  discard(sum += 3);
  discard(sum += 4);
  return sum == 7;
}());

TEST_CASE("nexenne::utility::discard evaluates and drops its arguments") {
  auto calls{0};
  auto const bump{[&calls] {
    ++calls;
    return calls;
  }};

  discard(bump());
  discard(bump(), bump());
  CHECK(calls == 3);  // every argument was evaluated exactly once
}

TEST_CASE("nexenne::utility::discard consumes a [[nodiscard]] result") {
  // Calling through discard counts as a use, so there is no warning and the
  // value is dropped.
  discard(nodiscard_result());
  CHECK(true);
}

TEST_CASE("nexenne::utility::discard propagates exceptions thrown while evaluating arguments") {
  auto const throwing{[]() -> int { throw std::runtime_error{"boom"}; }};
  CHECK_THROWS_AS(discard(throwing()), std::runtime_error);
}

TEST_CASE("nexenne::utility::discard accepts no arguments") {
  discard();
  CHECK(true);
}
