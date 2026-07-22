/**
 * @file
 * @brief Tests for the in-memory mock backend.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

using mock = ng::mock_chip<8>;

[[nodiscard]] auto opened_mock() -> mock {
  return {};
}

std::array const specs{
  ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17}),
  ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
};
std::array const configs{
  ng::line_config{ng::edge_detection::both},
  ng::line_config{ng::edge_detection::none, 0ns, true},
};

TEST_CASE("mock_chip: models all three backend tiers") {
  static_assert(ng::gpio_backend<mock>);
  static_assert(ng::bulk_gpio_backend<mock>);
  static_assert(ng::edge_source<mock>);
  CHECK(true);
}

TEST_CASE("mock_chip: open validates the spans and applies initial values") {
  mock chip{};
  CHECK_FALSE(chip.is_open());

  // Mismatched and empty spans are rejected.
  CHECK(
    chip.open(specs, std::span<ng::line_config const>{configs.data(), 1}).error()
    == ng::gpio_error::invalid_argument
  );
  CHECK(chip.open({}, {}).error() == ng::gpio_error::invalid_argument);

  REQUIRE(chip.open(specs, configs).has_value());
  CHECK(chip.is_open());

  // The output starts at its configured initial value; the input starts low.
  CHECK(*chip.read(ng::line_offset{4}) == true);
  CHECK(*chip.read(ng::line_offset{17}) == false);
}

TEST_CASE("mock_chip: read and write enforce the request set and direction") {
  mock chip{};

  // Everything fails while closed.
  CHECK(chip.read(ng::line_offset{17}).error() == ng::gpio_error::not_open);
  CHECK(chip.write(ng::line_offset{4}, true).error() == ng::gpio_error::not_open);

  REQUIRE(chip.open(specs, configs).has_value());

  CHECK(chip.read(ng::line_offset{99}).error() == ng::gpio_error::not_found);
  CHECK(chip.write(ng::line_offset{17}, true).error() == ng::gpio_error::invalid_argument);

  REQUIRE(chip.write(ng::line_offset{4}, false).has_value());
  CHECK(*chip.physical(ng::line_offset{4}) == false);

  // The rig can drive an input; read then sees it.
  REQUIRE(chip.set_physical(ng::line_offset{17}, true).has_value());
  CHECK(*chip.read(ng::line_offset{17}) == true);
}

TEST_CASE("mock_chip: bulk operations mirror the per-line ones") {
  mock chip{};
  REQUIRE(chip.open(specs, configs).has_value());
  REQUIRE(chip.set_physical(ng::line_offset{17}, true).has_value());

  std::array const offsets{ng::line_offset{17}, ng::line_offset{4}};
  std::array<bool, 2> levels{};
  REQUIRE(chip.read_lines(offsets, levels).has_value());
  CHECK(levels[0] == true);
  CHECK(levels[1] == true);

  std::array const out_offsets{ng::line_offset{4}};
  std::array const out_levels{false};
  REQUIRE(chip.write_lines(out_offsets, out_levels).has_value());
  CHECK(*chip.physical(ng::line_offset{4}) == false);

  // A length mismatch is rejected before any line is touched.
  CHECK(
    chip.read_lines(offsets, std::span<bool>{levels.data(), 1}).error()
    == ng::gpio_error::invalid_argument
  );
}

TEST_CASE("mock_chip: injected events come back in order, then a clean miss") {
  mock chip{};
  REQUIRE(chip.open(specs, configs).has_value());

  ng::line_event first{};
  first.offset = ng::line_offset{17};
  first.sequence = ng::event_sequence{1};
  first.physical = true;
  first.edge = ng::edge_kind::rising;
  auto second{first};
  second.sequence = ng::event_sequence{2};
  second.physical = false;
  second.edge = ng::edge_kind::falling;

  CHECK(chip.inject(first));
  CHECK(chip.inject(second));

  auto const a{chip.wait_event(0ns)};
  REQUIRE(a.has_value());
  REQUIRE(a->has_value());
  CHECK(**a == first);

  auto const b{chip.wait_event(0ns)};
  REQUIRE(b.has_value());
  CHECK(**b == second);

  auto const none{chip.wait_event(0ns)};
  REQUIRE(none.has_value());
  CHECK_FALSE(none->has_value());
}

TEST_CASE("mock_chip: close drops pending events; a reopen starts clean") {
  mock chip{};
  REQUIRE(chip.open(specs, configs).has_value());

  ng::line_event event{};
  event.offset = ng::line_offset{17};
  CHECK(chip.inject(event));

  chip.close();
  CHECK_FALSE(chip.is_open());
  CHECK_FALSE(chip.inject(event));

  // The reopened chip must not replay the event injected before the close.
  REQUIRE(chip.open(specs, configs).has_value());
  auto const none{chip.wait_event(0ns)};
  REQUIRE(none.has_value());
  CHECK_FALSE(none->has_value());
}

TEST_CASE("mock_chip: native handle is the documented -1 placeholder") {
  mock const chip{};
  CHECK(chip.native_handle() == -1);
  CHECK(opened_mock().native_handle() == -1);
}

}  // namespace
