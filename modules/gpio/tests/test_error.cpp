/**
 * @file
 * @brief Tests for the nexenne::gpio error policy.
 */

#include <doctest/doctest.h>

#include <string_view>

#include <nexenne/gpio/error.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("gpio_error: to_string names every enumerator") {
  CHECK(ng::to_string(ng::gpio_error::invalid_argument) == "invalid_argument");
  CHECK(ng::to_string(ng::gpio_error::not_found) == "not_found");
  CHECK(ng::to_string(ng::gpio_error::not_open) == "not_open");
  CHECK(ng::to_string(ng::gpio_error::permission_denied) == "permission_denied");
  CHECK(ng::to_string(ng::gpio_error::busy) == "busy");
  CHECK(ng::to_string(ng::gpio_error::io_error) == "io_error");
  CHECK(ng::to_string(ng::gpio_error::timeout) == "timeout");
  CHECK(ng::to_string(ng::gpio_error::overflow) == "overflow");
  CHECK(ng::to_string(ng::gpio_error::unsupported) == "unsupported");

  static_assert(ng::to_string(ng::gpio_error::busy) == std::string_view{"busy"});
}

TEST_CASE("is_transient: retryable and permanent errors split cleanly") {
  // Worth a backoff retry: the condition can clear on its own.
  CHECK(ng::is_transient(ng::gpio_error::busy));
  CHECK(ng::is_transient(ng::gpio_error::timeout));
  CHECK(ng::is_transient(ng::gpio_error::io_error));
  CHECK(ng::is_transient(ng::gpio_error::overflow));

  // Never clears without operator action: retrying is noise.
  CHECK_FALSE(ng::is_transient(ng::gpio_error::invalid_argument));
  CHECK_FALSE(ng::is_transient(ng::gpio_error::not_found));
  CHECK_FALSE(ng::is_transient(ng::gpio_error::not_open));
  CHECK_FALSE(ng::is_transient(ng::gpio_error::permission_denied));
  CHECK_FALSE(ng::is_transient(ng::gpio_error::unsupported));

  static_assert(ng::is_transient(ng::gpio_error::busy));
}

TEST_CASE("result: carries either a value or an error") {
  ng::result<int> const ok{42};
  REQUIRE(ok.has_value());
  CHECK(*ok == 42);

  ng::result<int> const bad{std::unexpected{ng::gpio_error::not_open}};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == ng::gpio_error::not_open);
}

}  // namespace
