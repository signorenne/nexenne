/**
 * @file
 * @brief Tests for the nexenne::can error policy and its formatter.
 */

#include <doctest/doctest.h>

#include <format>
#include <sstream>
#include <string_view>

#include <nexenne/can/error.hpp>
#include <nexenne/can/format.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("can_error: to_string names every enumerator") {
  CHECK(nc::to_string(nc::can_error::invalid_id) == "invalid_id");
  CHECK(nc::to_string(nc::can_error::invalid_dlc) == "invalid_dlc");
  CHECK(nc::to_string(nc::can_error::payload_too_large) == "payload_too_large");
  CHECK(nc::to_string(nc::can_error::signal_out_of_range) == "signal_out_of_range");
  CHECK(nc::to_string(nc::can_error::value_out_of_range) == "value_out_of_range");
  CHECK(nc::to_string(nc::can_error::unsupported) == "unsupported");
  CHECK(nc::to_string(nc::can_error::io_error) == "io_error");
  CHECK(nc::to_string(nc::can_error::bus_off) == "bus_off");
  CHECK(nc::to_string(nc::can_error::buffer_full) == "buffer_full");
  CHECK(nc::to_string(nc::can_error::parse_error) == "parse_error");

  static_assert(nc::to_string(nc::can_error::invalid_id) == std::string_view{"invalid_id"});
}

TEST_CASE("can_error: std::format and ostream agree with to_string") {
  CHECK(std::format("{}", nc::can_error::io_error) == "io_error");

  std::ostringstream os;
  os << nc::can_error::bus_off;
  CHECK(os.str() == "bus_off");
}

TEST_CASE("result: carries either a value or an error") {
  nc::result<int> ok{42};
  REQUIRE(ok.has_value());
  CHECK(*ok == 42);

  nc::result<int> bad{std::unexpected{nc::can_error::value_out_of_range}};
  REQUIRE_FALSE(bad.has_value());
  CHECK(bad.error() == nc::can_error::value_out_of_range);
}

}  // namespace
