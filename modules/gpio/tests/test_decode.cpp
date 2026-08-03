/**
 * @file
 * @brief Tests for the physical-to-logical decode step.
 */

#include <doctest/doctest.h>

#include <chrono>

#include <nexenne/gpio/decode.hpp>

namespace {

namespace ng = nexenne::gpio;

[[nodiscard]] auto rising_event() -> ng::line_event {
  ng::line_event event{};
  event.sequence = ng::event_sequence{3};
  event.timestamp = ng::event_time{std::chrono::nanoseconds{700}};
  event.offset = ng::line_offset{17};
  event.chip = ng::chip_id{0};
  event.physical = true;
  event.edge = ng::edge_kind::rising;
  return event;
}

TEST_CASE("decode: active_high passes level and edge through") {
  auto const spec{ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17})};
  auto const value{ng::decode(spec, rising_event())};

  CHECK(value.name() == "button");
  CHECK(value.offset() == ng::line_offset{17});
  CHECK(value.logical() == true);
  CHECK(value.edge() == ng::edge_kind::rising);
  CHECK(value.sequence() == ng::event_sequence{3});
  CHECK(value.timestamp().time_since_epoch() == std::chrono::nanoseconds{700});
}

TEST_CASE("decode: active_low inverts the level and the edge together") {
  auto const spec{ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low
  )};
  auto const value{ng::decode(spec, rising_event())};

  // A physical rising edge on an active-low line is a logical release:
  // level false, falling edge. Level and edge must agree.
  CHECK(value.logical() == false);
  CHECK(value.edge() == ng::edge_kind::falling);
}

TEST_CASE("decode: a steady-state event keeps edge none under any polarity") {
  auto event{rising_event()};
  event.edge = ng::edge_kind::none;

  auto const spec{ng::line_spec::input(
    "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low
  )};
  CHECK(ng::decode(spec, event).edge() == ng::edge_kind::none);
}

}  // namespace
