/**
 * @file
 * @brief Tests for the name-addressed chip handle over the mock backend.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <type_traits>
#include <vector>

#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>

namespace {

namespace ng = nexenne::gpio;

using mock = ng::mock_chip<8>;

std::array const specs{
  ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low
  ),
  ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
};
std::array const configs{ng::line_config{}, ng::line_config{}};

TEST_CASE("chip: an unbound chip reports not_open, a bound one validates spans") {
  ng::chip<mock> unbound{};
  CHECK(unbound.open(specs, configs).error() == ng::gpio_error::not_open);
  CHECK(unbound.read("button").error() == ng::gpio_error::not_open);
  CHECK_FALSE(unbound.line_for("button").has_value());

  mock backend{};
  ng::chip<mock> chip{backend};
  CHECK_FALSE(chip.is_open());

  // The wrapper is the single place that rejects a malformed request.
  CHECK(chip.open({}, {}).error() == ng::gpio_error::invalid_argument);
  CHECK(
    chip.open(specs, std::span<ng::line_config const>{configs.data(), 1}).error()
    == ng::gpio_error::invalid_argument
  );

  static_assert(std::is_trivially_copyable_v<ng::chip<mock>>);
}

TEST_CASE("chip: open exposes the spec table; close drops it") {
  mock backend{};
  ng::chip<mock> chip{backend};

  REQUIRE(chip.open(specs, configs).has_value());
  CHECK(chip.is_open());
  CHECK(chip.specs().size() == 2);

  REQUIRE(chip.spec("led").has_value());
  CHECK(chip.spec("led")->offset() == ng::line_offset{4});
  CHECK_FALSE(chip.spec("missing").has_value());

  chip.close();
  CHECK_FALSE(chip.is_open());
  CHECK(chip.specs().empty());
  CHECK_FALSE(chip.spec("led").has_value());
}

TEST_CASE("chip: name-addressed read and write stay in the logical domain") {
  mock backend{};
  ng::chip<mock> chip{backend};
  REQUIRE(chip.open(specs, configs).has_value());

  // Active-low button: wire low means logically pressed.
  REQUIRE(backend.set_physical(ng::line_offset{17}, false).has_value());
  CHECK(*chip.read("button") == true);

  REQUIRE(chip.write("led", true).has_value());
  CHECK(*backend.physical(ng::line_offset{4}) == true);

  CHECK(chip.read("missing").error() == ng::gpio_error::not_found);
  CHECK(chip.write("missing", true).error() == ng::gpio_error::not_found);
  CHECK(chip.write("button", true).error() == ng::gpio_error::invalid_argument);
}

TEST_CASE("chip: snapshot delivers the logical baseline of every input") {
  mock backend{};
  ng::chip<mock> chip{backend};
  REQUIRE(chip.open(specs, configs).has_value());

  // Wire low on the active-low button: logically pressed at startup.
  REQUIRE(backend.set_physical(ng::line_offset{17}, false).has_value());

  std::vector<ng::line_value> baseline{};
  REQUIRE(
    chip.snapshot([&](ng::line_value const& value) { baseline.push_back(value); }).has_value()
  );

  // Only the input is delivered; the output line is not part of the baseline.
  REQUIRE(baseline.size() == 1);
  CHECK(baseline[0].name() == "button");
  CHECK(baseline[0].logical() == true);
  CHECK(baseline[0].edge() == ng::edge_kind::none);
  CHECK(baseline[0].sequence() == ng::event_sequence{0});

  ng::chip<mock> const unbound{};
  CHECK(unbound.snapshot([](ng::line_value const&) {}).error() == ng::gpio_error::not_open);
}

TEST_CASE("chip: reconfigure swaps behaviour and the spec view in place") {
  static_assert(ng::reconfigurable_gpio_backend<mock>);

  mock backend{};
  ng::chip<mock> chip{backend};
  REQUIRE(chip.open(specs, configs).has_value());

  // Same lines, new behaviour: the button flips to active_high polarity.
  std::array const changed{
    ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17}),
    ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
  };
  std::array const changed_configs{
    ng::line_config{ng::edge_detection::rising},
    ng::line_config{ng::edge_detection::none, std::chrono::nanoseconds{0}, true},
  };
  REQUIRE(chip.reconfigure(changed, changed_configs).has_value());

  // The spec view now reflects the new polarity, and the output took its
  // new initial level without a close and reopen.
  CHECK(chip.spec("button")->polarity() == ng::line_polarity::active_high);
  CHECK(*backend.physical(ng::line_offset{4}) == true);

  // Addressing different lines is rejected and changes nothing.
  std::array const wrong{
    ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{18}),
    ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
  };
  CHECK(backend.reconfigure(wrong, changed_configs).error() == ng::gpio_error::invalid_argument);
  CHECK(chip.reconfigure({}, {}).error() == ng::gpio_error::invalid_argument);
}

TEST_CASE("chip: line_for mints a bound handle sharing the backend") {
  mock backend{};
  ng::chip<mock> chip{backend};
  REQUIRE(chip.open(specs, configs).has_value());

  auto led{chip.line_for("led")};
  REQUIRE(led.has_value());
  CHECK(led->valid());
  CHECK(led->spec().name() == "led");

  REQUIRE(led->set().has_value());
  CHECK(*chip.read("led") == true);

  CHECK_FALSE(chip.line_for("missing").has_value());
}

}  // namespace
