/**
 * @file
 * @brief Tests for the compact physical edge event.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <type_traits>

#include <nexenne/gpio/line_event.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("line_event: trivially copyable and padding-free") {
  static_assert(std::is_trivially_copyable_v<ng::line_event>);
  static_assert(std::is_same_v<ng::line_event::value_type, bool>);

  // Largest-field-first layout packs to 24 bytes; events sit inline in
  // transport rings, so the size is part of the contract.
  static_assert(sizeof(ng::line_event) == 24);
}

TEST_CASE("line_event: aggregate fields round-trip") {
  ng::line_event const event{
    .sequence = ng::event_sequence{7},
    .timestamp = ng::event_time{std::chrono::nanoseconds{1'000}},
    .offset = ng::line_offset{17},
    .chip = ng::chip_id{0},
    .physical = true,
    .edge = ng::edge_kind::rising,
  };

  CHECK(event.sequence == ng::event_sequence{7});
  CHECK(event.timestamp.time_since_epoch() == std::chrono::nanoseconds{1'000});
  CHECK(event.offset == ng::line_offset{17});
  CHECK(event.physical == true);
  CHECK(event.edge == ng::edge_kind::rising);

  auto changed{event};
  changed.physical = false;
  CHECK(event != changed);
  CHECK(event == ng::line_event{event});
}

}  // namespace
