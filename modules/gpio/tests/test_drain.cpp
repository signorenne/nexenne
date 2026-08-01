/**
 * @file
 * @brief Tests for the source-to-sink event pump.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>

#include <nexenne/gpio/drain.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>
#include <nexenne/gpio/io/queue_sink.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

std::array const specs{
  ng::line_spec::input("button", ng::chip_id{0}, ng::line_offset{17}),
};
std::array const configs{ng::line_config{ng::edge_detection::both}};

[[nodiscard]] auto numbered_event(std::uint64_t const sequence) -> ng::line_event {
  ng::line_event event{};
  event.offset = ng::line_offset{17};
  event.sequence = ng::event_sequence{sequence};
  return event;
}

TEST_CASE("drain_events: moves everything ready and stops on the clean miss") {
  ng::mock_chip<8> backend{};
  REQUIRE(backend.open(specs, configs).has_value());
  REQUIRE(backend.inject(numbered_event(1)));
  REQUIRE(backend.inject(numbered_event(2)));
  REQUIRE(backend.inject(numbered_event(3)));

  ng::queue_sink<8> sink{};
  auto const report{ng::drain_events(backend, sink)};
  REQUIRE(report.has_value());
  CHECK(report->delivered == 3);
  CHECK(report->rejected == 0);

  CHECK(sink.try_pop()->sequence == ng::event_sequence{1});
  CHECK(sink.try_pop()->sequence == ng::event_sequence{2});
  CHECK(sink.try_pop()->sequence == ng::event_sequence{3});
  CHECK_FALSE(sink.try_pop().has_value());

  // A second drain with nothing ready reports zeros.
  auto const empty{ng::drain_events(backend, sink)};
  REQUIRE(empty.has_value());
  CHECK(*empty == ng::drain_report{});
}

TEST_CASE("drain_events: a full sink is counted, the source is still emptied") {
  ng::mock_chip<8> backend{};
  REQUIRE(backend.open(specs, configs).has_value());
  for (std::uint64_t i{1}; i <= 5; ++i) {
    REQUIRE(backend.inject(numbered_event(i)));
  }

  // Room for three: the last two must be rejected but still consumed.
  ng::queue_sink<4> sink{};
  auto const report{ng::drain_events(backend, sink)};
  REQUIRE(report.has_value());
  CHECK(report->delivered == 3);
  CHECK(report->rejected == 2);
  CHECK(sink.dropped() == 2);

  auto const after{backend.wait_event(0ns)};
  REQUIRE(after.has_value());
  CHECK_FALSE(after->has_value());  // the source really is empty
}

TEST_CASE("drain_events: a closed source surfaces the wait error") {
  ng::mock_chip<8> backend{};
  ng::queue_sink<4> sink{};
  CHECK(ng::drain_events(backend, sink).error() == ng::gpio_error::not_open);
}

}  // namespace
