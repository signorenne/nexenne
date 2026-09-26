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

[[nodiscard]] auto
event_at(std::chrono::nanoseconds const when, bool const physical, ng::edge_kind const edge)
  -> ng::line_event {
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

  CHECK_FALSE(debounce.feed(event_at(1ms, true, ng::edge_kind::rising)).has_value());
  CHECK_FALSE(debounce.feed(event_at(2ms, false, ng::edge_kind::falling)).has_value());
  CHECK_FALSE(debounce.feed(event_at(3ms, true, ng::edge_kind::rising)).has_value());

  auto const settled{debounce.feed(event_at(9ms, true, ng::edge_kind::rising))};
  REQUIRE(settled.has_value());
  CHECK(settled->physical == true);
  CHECK(settled->edge == ng::edge_kind::rising);

  CHECK_FALSE(debounce.feed(event_at(20ms, true, ng::edge_kind::rising)).has_value());
}

TEST_CASE("event_debounce: the settled edge is derived, not carried") {
  ng::event_debounce debounce{5ms};
  REQUIRE(debounce.feed(event_at(0ms, true, ng::edge_kind::none)).has_value());

  CHECK_FALSE(debounce.feed(event_at(1ms, false, ng::edge_kind::falling)).has_value());

  auto const settled{debounce.feed(event_at(7ms, false, ng::edge_kind::rising))};
  REQUIRE(settled.has_value());
  CHECK(settled->physical == false);
  CHECK(settled->edge == ng::edge_kind::falling);
}

TEST_CASE("event_debounce: stable reports the settled level, not the bounce") {
  ng::event_debounce debounce{5ms};

  CHECK_FALSE(debounce.stable().has_value());

  REQUIRE(debounce.feed(event_at(0ms, true, ng::edge_kind::none)).has_value());
  CHECK(*debounce.stable() == true);

  CHECK_FALSE(debounce.feed(event_at(1ms, false, ng::edge_kind::falling)).has_value());
  CHECK(*debounce.stable() == true);

  REQUIRE(debounce.feed(event_at(7ms, false, ng::edge_kind::falling)).has_value());
  CHECK(*debounce.stable() == false);

  debounce.reset();
  CHECK_FALSE(debounce.stable().has_value());
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

TEST_CASE("event_debounce: an overdue level settles on the next opposite edge") {
  ng::event_debounce debounce{5ms};
  REQUIRE(debounce.feed(event_at(0ms, false, ng::edge_kind::falling)).has_value());

  CHECK_FALSE(debounce.feed(event_at(10ms, true, ng::edge_kind::rising)).has_value());
  auto const press{debounce.feed(event_at(100ms, false, ng::edge_kind::falling))};
  REQUIRE(press.has_value());
  CHECK(press->physical == true);
  CHECK(press->edge == ng::edge_kind::rising);
  CHECK(press->timestamp == ng::event_time{10ms});
  CHECK(*debounce.stable() == true);
}

TEST_CASE("event_debounce: expire settles a held level at its deadline") {
  ng::event_debounce debounce{5ms};
  CHECK_FALSE(debounce.deadline().has_value());
  REQUIRE(debounce.feed(event_at(0ms, true, ng::edge_kind::rising)).has_value());
  CHECK_FALSE(debounce.deadline().has_value());

  CHECK_FALSE(debounce.feed(event_at(100ms, false, ng::edge_kind::falling)).has_value());
  CHECK_FALSE(debounce.feed(event_at(101ms, true, ng::edge_kind::rising)).has_value());
  CHECK_FALSE(debounce.deadline().has_value());
  CHECK_FALSE(debounce.feed(event_at(102ms, false, ng::edge_kind::falling)).has_value());
  REQUIRE(debounce.deadline().has_value());
  CHECK(*debounce.deadline() == ng::event_time{107ms});

  CHECK_FALSE(debounce.expire(ng::event_time{106ms}).has_value());
  auto const press{debounce.expire(ng::event_time{107ms})};
  REQUIRE(press.has_value());
  CHECK(press->physical == false);
  CHECK(press->edge == ng::edge_kind::falling);
  CHECK(press->timestamp == ng::event_time{102ms});
  CHECK_FALSE(debounce.deadline().has_value());
  CHECK_FALSE(debounce.expire(ng::event_time{200ms}).has_value());

  CHECK_FALSE(debounce.feed(event_at(500ms, true, ng::edge_kind::rising)).has_value());
  auto const release{debounce.expire(ng::event_time{600ms})};
  REQUIRE(release.has_value());
  CHECK(release->edge == ng::edge_kind::rising);

  CHECK_FALSE(debounce.feed(event_at(700ms, false, ng::edge_kind::falling)).has_value());
  debounce.reset();
  CHECK_FALSE(debounce.deadline().has_value());
}

TEST_CASE("event_debounce: clean presses each settle once over a kernel-shaped stream") {
  ng::event_debounce debounce{5ms};
  auto forwarded{0};
  auto const deliver{[&](std::chrono::milliseconds const at, bool const physical) {
    if (debounce.expire(ng::event_time{at}).has_value()) {
      ++forwarded;
    }
    auto const edge{physical ? ng::edge_kind::rising : ng::edge_kind::falling};
    if (debounce.feed(event_at(at, physical, edge)).has_value()) {
      ++forwarded;
    }
  }};
  deliver(0ms, true);
  for (auto const at : {1000ms, 2000ms, 3000ms}) {
    deliver(at, false);
    deliver(at + 300ms, true);
  }
  if (debounce.expire(ng::event_time{4000ms}).has_value()) {
    ++forwarded;
  }
  CHECK(forwarded == 7);
}

}  // namespace
