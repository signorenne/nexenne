/**
 * @file
 * @brief Tests for the nexenne::gpio vocabulary types.
 */

#include <doctest/doctest.h>

#include <chrono>
#include <type_traits>

#include <nexenne/gpio/line_types.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("strong identifiers: distinct types, comparable values") {
  ng::chip_id const chip{2};
  ng::line_offset const offset{17};
  ng::event_sequence const sequence{40};

  CHECK(chip.get() == 2);
  CHECK(offset.get() == 17);
  CHECK(sequence.get() == 40);

  CHECK(ng::chip_id{2} == chip);
  CHECK(ng::line_offset{3} < offset);
  CHECK(ng::event_sequence{41} > sequence);

  static_assert(!std::is_convertible_v<ng::chip_id, ng::line_offset>);
  static_assert(!std::is_convertible_v<ng::line_offset, ng::chip_id>);
  static_assert(!std::is_convertible_v<std::uint32_t, ng::line_offset>);
}

TEST_CASE("event_time: timestamps subtract to a duration and stay distinct from it") {
  ng::event_time const before{std::chrono::nanoseconds{1'000}};
  ng::event_time const after{std::chrono::nanoseconds{4'500}};

  CHECK(after - before == std::chrono::nanoseconds{3'500});
  CHECK(before < after);

  CHECK(before + std::chrono::nanoseconds{3'500} == after);

  static_assert(!std::is_convertible_v<ng::event_time, std::chrono::nanoseconds>);
  static_assert(!std::is_convertible_v<std::chrono::nanoseconds, ng::event_time>);
}

TEST_CASE("event_time: the clock tag decides which typed time point it yields") {
  ng::event_time const steady{std::chrono::nanoseconds{1'000}};
  CHECK(steady.clock() == ng::line_clock::monotonic);
  REQUIRE(steady.monotonic().has_value());
  CHECK(steady.monotonic()->time_since_epoch() == std::chrono::nanoseconds{1'000});
  CHECK_FALSE(steady.realtime().has_value());

  ng::event_time const wall{std::chrono::nanoseconds{2'000}, ng::line_clock::realtime};
  REQUIRE(wall.realtime().has_value());
  CHECK(wall.realtime()->time_since_epoch() == std::chrono::nanoseconds{2'000});
  CHECK_FALSE(wall.monotonic().has_value());

  ng::event_time const engine{std::chrono::nanoseconds{3'000}, ng::line_clock::hte};
  CHECK_FALSE(engine.monotonic().has_value());
  CHECK_FALSE(engine.realtime().has_value());

  CHECK(
    ng::event_time{ng::monotonic_event_clock::time_point{std::chrono::nanoseconds{5}}}
    == ng::event_time{std::chrono::nanoseconds{5}}
  );
  CHECK(
    ng::event_time{ng::realtime_event_clock::time_point{std::chrono::nanoseconds{5}}}
    == ng::event_time{std::chrono::nanoseconds{5}, ng::line_clock::realtime}
  );
  CHECK(steady != ng::event_time{std::chrono::nanoseconds{1'000}, ng::line_clock::realtime});
  CHECK((wall + std::chrono::nanoseconds{1}).clock() == ng::line_clock::realtime);

  static_assert(ng::monotonic_event_clock::is_steady);
  static_assert(!ng::realtime_event_clock::is_steady);
  static_assert(
    !std::
      is_convertible_v<ng::monotonic_event_clock::time_point, ng::realtime_event_clock::time_point>
  );
}

TEST_CASE("apply_polarity: level and edge invert together under active_low") {
  CHECK(ng::apply_polarity(true, ng::line_polarity::active_high) == true);
  CHECK(ng::apply_polarity(false, ng::line_polarity::active_high) == false);
  CHECK(
    ng::apply_polarity(ng::edge_kind::rising, ng::line_polarity::active_high)
    == ng::edge_kind::rising
  );

  CHECK(ng::apply_polarity(true, ng::line_polarity::active_low) == false);
  CHECK(ng::apply_polarity(false, ng::line_polarity::active_low) == true);
  CHECK(
    ng::apply_polarity(ng::edge_kind::rising, ng::line_polarity::active_low)
    == ng::edge_kind::falling
  );
  CHECK(
    ng::apply_polarity(ng::edge_kind::falling, ng::line_polarity::active_low)
    == ng::edge_kind::rising
  );

  CHECK(
    ng::apply_polarity(ng::edge_kind::none, ng::line_polarity::active_low) == ng::edge_kind::none
  );

  static_assert(
    ng::apply_polarity(
      ng::apply_polarity(true, ng::line_polarity::active_low), ng::line_polarity::active_low
    )
    == true
  );
}

TEST_CASE("matches: a subscription admits exactly its own directions") {
  CHECK(ng::matches(ng::edge_detection::both, ng::edge_kind::rising));
  CHECK(ng::matches(ng::edge_detection::both, ng::edge_kind::falling));
  CHECK_FALSE(ng::matches(ng::edge_detection::both, ng::edge_kind::none));

  CHECK(ng::matches(ng::edge_detection::rising, ng::edge_kind::rising));
  CHECK_FALSE(ng::matches(ng::edge_detection::rising, ng::edge_kind::falling));
  CHECK(ng::matches(ng::edge_detection::falling, ng::edge_kind::falling));
  CHECK_FALSE(ng::matches(ng::edge_detection::falling, ng::edge_kind::rising));

  CHECK_FALSE(ng::matches(ng::edge_detection::none, ng::edge_kind::rising));

  static_assert(ng::matches(ng::edge_detection::both, ng::edge_kind::rising));
}

TEST_CASE("enums: subscription side is wider than the sample side") {
  static_assert(std::is_same_v<std::underlying_type_t<ng::edge_kind>, std::uint8_t>);
  static_assert(std::is_same_v<std::underlying_type_t<ng::edge_detection>, std::uint8_t>);

  CHECK(ng::edge_kind::rising != ng::edge_kind::falling);
  CHECK(ng::edge_detection::both != ng::edge_detection::rising);
}

}  // namespace
