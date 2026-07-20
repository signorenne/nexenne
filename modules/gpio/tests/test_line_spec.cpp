/**
 * @file
 * @brief Tests for the static line description.
 */

#include <doctest/doctest.h>

#include <string>
#include <type_traits>

#include <nexenne/gpio/line_spec.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("line_spec: default is an unnamed active-high input") {
  ng::line_spec const spec{};

  CHECK(spec.name().empty());
  CHECK(spec.chip() == ng::chip_id{0});
  CHECK(spec.offset() == ng::line_offset{0});
  CHECK(spec.direction() == ng::line_direction::input);
  CHECK(spec.polarity() == ng::line_polarity::active_high);
  CHECK(spec.bias() == ng::line_bias::as_is);
  CHECK(spec.drive() == ng::line_drive::push_pull);

  static_assert(std::is_trivially_copyable_v<ng::line_spec>);
  static_assert(std::is_same_v<ng::line_spec::value_type, bool>);
}

TEST_CASE("line_spec: the input factory fixes direction and drive") {
  auto const button{ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low,
    ng::line_bias::pull_up
  )};

  CHECK(button.name() == "button");
  CHECK(button.offset() == ng::line_offset{17});
  CHECK(button.direction() == ng::line_direction::input);
  CHECK(button.polarity() == ng::line_polarity::active_low);
  CHECK(button.bias() == ng::line_bias::pull_up);
  CHECK(button.drive() == ng::line_drive::push_pull);
}

TEST_CASE("line_spec: the output factory fixes direction and bias") {
  auto const led{ng::line_spec::output(
    "led", ng::chip_id{1}, ng::line_offset{4}, ng::line_polarity::active_high,
    ng::line_drive::open_drain
  )};

  CHECK(led.name() == "led");
  CHECK(led.chip() == ng::chip_id{1});
  CHECK(led.direction() == ng::line_direction::output);
  CHECK(led.bias() == ng::line_bias::as_is);
  CHECK(led.drive() == ng::line_drive::open_drain);
}

TEST_CASE("line_spec: mutable accessors rewrite one field at a time") {
  ng::line_spec spec{};
  spec.name() = "estop";
  spec.offset() = ng::line_offset{9};
  spec.polarity() = ng::line_polarity::active_low;

  CHECK(spec.name() == "estop");
  CHECK(spec.offset() == ng::line_offset{9});
  CHECK(spec.polarity() == ng::line_polarity::active_low);
}

TEST_CASE("line_spec: to_logical and to_physical are inverse under both polarities") {
  auto active_low{ng::line_spec::input(
    "in", ng::chip_id{0}, ng::line_offset{0}, ng::line_polarity::active_low
  )};

  CHECK(active_low.to_logical(false) == true);
  CHECK(active_low.to_logical(true) == false);
  CHECK(active_low.to_physical(active_low.to_logical(true)) == true);

  active_low.polarity() = ng::line_polarity::active_high;
  CHECK(active_low.to_logical(true) == true);
  CHECK(active_low.to_physical(false) == false);
}

TEST_CASE("line_spec: equality compares name content, not storage identity") {
  std::string const owned{"button"};
  auto const a{ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17})};
  auto const b{ng::line_spec::input(owned, ng::chip_id{0}, ng::line_offset{17})};
  auto const c{ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{18})};

  CHECK(a == b);
  CHECK(a != c);
}

}  // namespace
