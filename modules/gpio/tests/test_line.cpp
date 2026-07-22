/**
 * @file
 * @brief Tests for the single-line handle over the mock backend.
 */

#include <doctest/doctest.h>

#include <array>
#include <type_traits>

#include <nexenne/gpio/io/mock_chip.hpp>
#include <nexenne/gpio/line.hpp>

namespace {

namespace ng = nexenne::gpio;

using mock = ng::mock_chip<8>;

std::array const specs{
  ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low
  ),
  ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}, ng::line_polarity::active_low),
};
std::array const configs{ng::line_config{}, ng::line_config{}};

TEST_CASE("line: an unbound handle reports not_open for everything") {
  ng::line<mock> handle{};

  CHECK_FALSE(handle.valid());
  CHECK(handle.backend() == nullptr);
  CHECK(handle.read().error() == ng::gpio_error::not_open);
  CHECK(handle.write(true).error() == ng::gpio_error::not_open);

  static_assert(std::is_trivially_copyable_v<ng::line<mock>>);
  static_assert(std::is_same_v<ng::line<mock>::backend_type, mock>);
}

TEST_CASE("line: read applies polarity so the caller stays logical") {
  mock backend{};
  REQUIRE(backend.open(specs, configs).has_value());
  ng::line<mock> const button{backend, specs[0]};

  // Physical low on an active-low button is logically pressed.
  REQUIRE(backend.set_physical(ng::line_offset{17}, false).has_value());
  CHECK(*button.read() == true);

  REQUIRE(backend.set_physical(ng::line_offset{17}, true).has_value());
  CHECK(*button.read() == false);
}

TEST_CASE("line: write converts to physical and rejects inputs") {
  mock backend{};
  REQUIRE(backend.open(specs, configs).has_value());
  ng::line<mock> led{backend, specs[1]};
  ng::line<mock> button{backend, specs[0]};

  // Logical on for an active-low LED drives the wire low.
  REQUIRE(led.write(true).has_value());
  CHECK(*backend.physical(ng::line_offset{4}) == false);

  REQUIRE(led.set().has_value());
  CHECK(*backend.physical(ng::line_offset{4}) == false);
  REQUIRE(led.clear().has_value());
  CHECK(*backend.physical(ng::line_offset{4}) == true);

  CHECK(button.write(true).error() == ng::gpio_error::invalid_argument);
}

TEST_CASE("line: toggle complements the observed logical level") {
  mock backend{};
  REQUIRE(backend.open(specs, configs).has_value());
  ng::line<mock> led{backend, specs[1]};

  REQUIRE(led.clear().has_value());
  REQUIRE(led.toggle().has_value());
  CHECK(*led.read() == true);
  REQUIRE(led.toggle().has_value());
  CHECK(*led.read() == false);
}

}  // namespace
