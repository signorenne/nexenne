/**
 * @file
 * @brief Tests for the userspace event-stream debounce.
 */

#include <doctest/doctest.h>

#include <chrono>

#include <nexenne/gpio/debounce.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

[[nodiscard]] auto event_at(
  std::chrono::nanoseconds const when, bool const physical, ng::edge_kind const edge
) -> ng::line_event {
  ng::line_event event{};
  event.timestamp = ng::event_time{when};
  event.offset = ng::line_offset{17};
  event.physical = physical;
  event.edge = edge;
  return event;
}

TEST_CASE("event_debounce: the first event is accepted as state, not a transition") {
  ng::event_debounce debounce{5ms};
  CHECK(debounce.period() == 5ms);

  auto const first{debounce.feed(event_at(0ns, true, ng::edge_kind::rising))};
  REQUIRE(first.has_value());
  CHECK(first->physical == true);
  CHECK(first->edge == ng::edge_kind::none);
}

TEST_CASE("event_debounce: a bounce burst settles to one transition") {
  ng::event_debounce debounce{5ms};
  REQUIRE(debounce.feed(event_at(0ms, false, ng::edge_kind::none)).has_value());

  // Contact bounce: rapid alternation well inside the settle period.
  CHECK_FALSE(debounce.feed(event_at(1ms, true, ng::edge_kind::rising)).has_value());
  CHECK_FALSE(debounce.feed(event_at(2ms, false, ng::edge_kind::falling)).has_value());
  CHECK_FALSE(debounce.feed(event_at(3ms, true, ng::edge_kind::rising)).has_value());

  // The level holds high past the period: exactly one settled rising edge.
  auto const settled{debounce.feed(event_at(9ms, true, ng::edge_kind::rising))};
  REQUIRE(settled.has_value());
  CHECK(settled->physical == true);
  CHECK(settled->edge == ng::edge_kind::rising);

  // Staying high produces nothing further.
  CHECK_FALSE(debounce.feed(event_at(20ms, true, ng::edge_kind::rising)).has_value());
}

TEST_CASE("event_debounce: the settled edge is derived, not carried") {
  ng::event_debounce debounce{5ms};
  REQUIRE(debounce.feed(event_at(0ms, true, ng::edge_kind::none)).has_value());

  CHECK_FALSE(debounce.feed(event_at(1ms, false, ng::edge_kind::falling)).has_value());

  // The raw event lies about its edge; the debouncer reports the settled
  // transition it actually observed.
  auto const settled{debounce.feed(event_at(7ms, false, ng::edge_kind::rising))};
  REQUIRE(settled.has_value());
  CHECK(settled->physical == false);
  CHECK(settled->edge == ng::edge_kind::falling);
}

TEST_CASE("event_debounce: reset makes the next event an acceptance again") {
  ng::event_debounce debounce{5ms};
  REQUIRE(debounce.feed(event_at(0ms, true, ng::edge_kind::none)).has_value());

  debounce.reset();
  auto const accepted{debounce.feed(event_at(10ms, false, ng::edge_kind::falling))};
  REQUIRE(accepted.has_value());
  CHECK(accepted->edge == ng::edge_kind::none);
}

TEST_CASE("event_debounce: a zero period passes transitions straight through") {
  ng::event_debounce debounce{};
  REQUIRE(debounce.feed(event_at(0ns, false, ng::edge_kind::none)).has_value());

  auto const passed{debounce.feed(event_at(1ns, true, ng::edge_kind::rising))};
  REQUIRE(passed.has_value());
  CHECK(passed->edge == ng::edge_kind::rising);
}

}  // namespace
