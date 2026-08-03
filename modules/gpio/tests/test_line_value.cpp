/**
 * @file
 * @brief Tests for the logical line observation.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <type_traits>

#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_value.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("line_value: default is an empty steady-state observation") {
  ng::line_value const value{};

  CHECK(value.name().empty());
  CHECK(value.logical() == false);
  CHECK(value.edge() == ng::edge_kind::none);
  CHECK(value.sequence() == ng::event_sequence{0});

  static_assert(std::is_trivially_copyable_v<ng::line_value>);
  static_assert(std::is_same_v<ng::line_value::value_type, bool>);
}

TEST_CASE("line_value: field constructor stores everything verbatim") {
  ng::line_value const value{
    "button",
    ng::chip_id{2},
    ng::line_offset{17},
    true,
    ng::edge_kind::rising,
    ng::event_sequence{9},
    ng::event_time{std::chrono::nanoseconds{500}},
  };

  CHECK(value.name() == "button");
  CHECK(value.chip() == ng::chip_id{2});
  CHECK(value.offset() == ng::line_offset{17});
  CHECK(value.logical() == true);
  CHECK(value.edge() == ng::edge_kind::rising);
  CHECK(value.sequence() == ng::event_sequence{9});
  CHECK(value.timestamp().time_since_epoch() == std::chrono::nanoseconds{500});
}

TEST_CASE("line_value: spec constructor copies the identity, not the polarity") {
  auto const spec{ng::line_spec::input(
    "button", ng::chip_id{1}, ng::line_offset{4}, ng::line_polarity::active_low
  )};

  // The level passed in is stored verbatim: decode applies polarity, not this type.
  ng::line_value const value{spec, true, ng::edge_kind::falling};

  CHECK(value.name() == "button");
  CHECK(value.chip() == ng::chip_id{1});
  CHECK(value.offset() == ng::line_offset{4});
  CHECK(value.logical() == true);
  CHECK(value.edge() == ng::edge_kind::falling);
}

TEST_CASE("line_value: equality compares name content") {
  auto const spec{ng::line_spec::input("in", ng::chip_id{0}, ng::line_offset{3})};
  ng::line_value const a{spec, true};
  ng::line_value const b{"in", ng::chip_id{0}, ng::line_offset{3}, true};
  ng::line_value const c{spec, false};

  CHECK(a == b);
  CHECK(a != c);
}

}  // namespace
