/**
 * @file
 * @brief Smoke test: the chrono umbrella header compiles as a unit.
 */

#include <doctest/doctest.h>

#include <nexenne/chrono/chrono.hpp>

namespace {

TEST_CASE("nexenne::chrono umbrella header is includable") {
  CHECK(true);
}

}  // namespace
