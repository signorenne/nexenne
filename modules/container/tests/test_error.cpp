/**
 * @file
 * @brief Tests for the nexenne::container error codes and result alias.
 */

#include <doctest/doctest.h>

#include <expected>
#include <type_traits>

#include <nexenne/container/error.hpp>

namespace {

namespace cn = nexenne::container;

static_assert(std::is_same_v<cn::result<int>, std::expected<int, cn::container_error>>);

static_assert(cn::to_string(cn::container_error::full) == "full");
static_assert(cn::to_string(cn::container_error::empty) == "empty");
static_assert(cn::to_string(cn::container_error::out_of_range) == "out_of_range");
static_assert(cn::to_string(cn::container_error::not_found) == "not_found");

TEST_CASE("nexenne::container::result carries either a value or an error") {
  cn::result<int> const ok{42};
  cn::result<int> const bad{std::unexpect, cn::container_error::empty};

  CHECK(ok.has_value());
  CHECK(ok.value() == 42);
  CHECK_FALSE(bad.has_value());
  CHECK(bad.error() == cn::container_error::empty);
  CHECK(cn::to_string(bad.error()) == "empty");
}

}  // namespace
