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

  // A chip index is not a line offset: the types must not interconvert.
  static_assert(!std::is_convertible_v<ng::chip_id, ng::line_offset>);
  static_assert(!std::is_convertible_v<ng::line_offset, ng::chip_id>);
  static_assert(!std::is_convertible_v<std::uint32_t, ng::line_offset>);
}

TEST_CASE("event_time: timestamps subtract to a duration and stay distinct from it") {
  ng::event_time const before{std::chrono::nanoseconds{1'000}};
  ng::event_time const after{std::chrono::nanoseconds{4'500}};

  CHECK(after - before == std::chrono::nanoseconds{3'500});
  CHECK(before < after);

  // A point in time is not a span of time.
  static_assert(!std::is_convertible_v<ng::event_time, std::chrono::nanoseconds>);
  static_assert(ng::event_clock::is_steady);
}

TEST_CASE("apply_polarity: level and edge invert together under active_low") {
  // active_high passes both through untouched.
  CHECK(ng::apply_polarity(true, ng::line_polarity::active_high) == true);
  CHECK(ng::apply_polarity(false, ng::line_polarity::active_high) == false);
  CHECK(
    ng::apply_polarity(ng::edge_kind::rising, ng::line_polarity::active_high)
    == ng::edge_kind::rising
  );

  // active_low inverts the level and flips the edge direction.
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

  // none is steady state; there is no direction to flip.
  CHECK(
    ng::apply_polarity(ng::edge_kind::none, ng::line_polarity::active_low) == ng::edge_kind::none
  );

  // The level mapping is an involution: applying it twice is the identity.
  static_assert(
    ng::apply_polarity(
      ng::apply_polarity(true, ng::line_polarity::active_low), ng::line_polarity::active_low
    )
    == true
  );
}

TEST_CASE("enums: subscription side is wider than the sample side") {
  // edge_kind names what one event was; edge_detection names what to deliver.
  static_assert(std::is_same_v<std::underlying_type_t<ng::edge_kind>, std::uint8_t>);
  static_assert(std::is_same_v<std::underlying_type_t<ng::edge_detection>, std::uint8_t>);

  CHECK(ng::edge_kind::rising != ng::edge_kind::falling);
  CHECK(ng::edge_detection::both != ng::edge_detection::rising);
}

}  // namespace
