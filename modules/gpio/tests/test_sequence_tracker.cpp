/**
 * @file
 * @brief Tests for the sequence-gap drop detector.
 */

#include <doctest/doctest.h>

#include <nexenne/gpio/sequence_tracker.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("sequence_tracker: the first tracked number is the baseline") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.last() == ng::event_sequence{0});
  CHECK(tracker.dropped() == 0);

  CHECK(tracker.feed(ng::event_sequence{5}) == 0);
  CHECK(tracker.last() == ng::event_sequence{5});
  CHECK(tracker.dropped() == 0);
}

TEST_CASE("sequence_tracker: consecutive numbers report no gap") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{1}) == 0);
  CHECK(tracker.feed(ng::event_sequence{2}) == 0);
  CHECK(tracker.feed(ng::event_sequence{3}) == 0);
  CHECK(tracker.dropped() == 0);
}

TEST_CASE("sequence_tracker: a jump reports the missing count and accumulates") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{1}) == 0);

  // 2, 3, 4 were lost to an overflow.
  CHECK(tracker.feed(ng::event_sequence{5}) == 3);
  CHECK(tracker.dropped() == 3);

  CHECK(tracker.feed(ng::event_sequence{6}) == 0);
  CHECK(tracker.feed(ng::event_sequence{9}) == 2);
  CHECK(tracker.dropped() == 5);
}

TEST_CASE("sequence_tracker: duplicates and reordered numbers report zero") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{4}) == 0);
  CHECK(tracker.feed(ng::event_sequence{4}) == 0);
  CHECK(tracker.feed(ng::event_sequence{2}) == 0);
  CHECK(tracker.last() == ng::event_sequence{4});
  CHECK(tracker.dropped() == 0);
}

TEST_CASE("sequence_tracker: the unset sequence is ignored entirely") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{0}) == 0);
  CHECK(tracker.last() == ng::event_sequence{0});

  CHECK(tracker.feed(ng::event_sequence{7}) == 0);
  CHECK(tracker.feed(ng::event_sequence{0}) == 0);
  CHECK(tracker.feed(ng::event_sequence{8}) == 0);
  CHECK(tracker.dropped() == 0);
}

TEST_CASE("sequence_tracker: reset forgets the baseline and the count") {
  ng::sequence_tracker tracker{};
  CHECK(tracker.feed(ng::event_sequence{1}) == 0);
  CHECK(tracker.feed(ng::event_sequence{10}) == 8);

  tracker.reset();
  CHECK(tracker.last() == ng::event_sequence{0});
  CHECK(tracker.dropped() == 0);
  CHECK(tracker.feed(ng::event_sequence{100}) == 0);
}

}  // namespace
