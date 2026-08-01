/**
 * @file
 * @brief Tests for the callback and queue edge transports.
 */

#include <doctest/doctest.h>

#include <vector>

#include <nexenne/gpio/io/callback_sink.hpp>
#include <nexenne/gpio/io/queue_sink.hpp>
#include <nexenne/gpio/sink.hpp>

namespace {

namespace ng = nexenne::gpio;

[[nodiscard]] auto numbered_event(std::uint64_t const sequence) -> ng::line_event {
  ng::line_event event{};
  event.sequence = ng::event_sequence{sequence};
  event.offset = ng::line_offset{17};
  return event;
}

TEST_CASE("callback_sink: every push lands in the handler, result forwarded") {
  std::vector<ng::line_event> received{};
  bool accept{true};
  auto handler{[&](ng::line_event const& event) noexcept -> bool {
    received.push_back(event);
    return accept;
  }};
  ng::callback_sink sink{handler};

  static_assert(ng::edge_sink<decltype(sink)>);
  static_assert(!ng::draining_edge_sink<decltype(sink)>);

  CHECK(sink.push(numbered_event(1)));
  accept = false;
  CHECK_FALSE(sink.push(numbered_event(2)));

  REQUIRE(received.size() == 2);
  CHECK(received[0].sequence == ng::event_sequence{1});
  CHECK(received[1].sequence == ng::event_sequence{2});
}

TEST_CASE("queue_sink: FIFO order, drop on full, drain to empty") {
  ng::queue_sink<4> sink{};

  static_assert(ng::edge_sink<ng::queue_sink<4>>);
  static_assert(ng::draining_edge_sink<ng::queue_sink<4>>);
  static_assert(ng::queue_sink<4>::capacity() == 3);

  CHECK(sink.empty());
  CHECK(sink.push(numbered_event(1)));
  CHECK(sink.push(numbered_event(2)));
  CHECK(sink.push(numbered_event(3)));

  // The ring is full: the fourth event is dropped, not blocked on, and the
  // sink itself keeps the tally.
  CHECK(sink.dropped() == 0);
  CHECK_FALSE(sink.push(numbered_event(4)));
  CHECK_FALSE(sink.push(numbered_event(5)));
  CHECK(sink.dropped() == 2);

  CHECK(sink.try_pop()->sequence == ng::event_sequence{1});
  CHECK(sink.try_pop()->sequence == ng::event_sequence{2});
  CHECK(sink.try_pop()->sequence == ng::event_sequence{3});
  CHECK_FALSE(sink.try_pop().has_value());
  CHECK(sink.empty());

  // Space freed by draining is reusable.
  CHECK(sink.push(numbered_event(5)));
  CHECK(sink.try_pop()->sequence == ng::event_sequence{5});
}

}  // namespace
