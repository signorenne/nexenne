/**
 * @file
 * @brief End-to-end test of the whole edge path through the umbrella header.
 *
 * Exercises the composition a real application uses: open a chip, inject
 * raw physical edges, drain them, debounce, track drops, decode to the
 * logical domain, and fan out through a sink. Everything is included
 * through the umbrella header to confirm it pulls the module in whole.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>
#include <vector>

#include <nexenne/gpio/gpio.hpp>

namespace {

namespace ng = nexenne::gpio;
namespace utility = nexenne::utility;
using namespace std::chrono_literals;

TEST_CASE("gpio: raw bounces become one settled, polarity-correct press") {
  // An active-low button on offset 17: pressing pulls the wire low.
  std::array const specs{
    ng::line_spec::input(
      "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low,
      ng::line_bias::pull_up
    ),
  };
  std::array const configs{ng::line_config{ng::edge_detection::both}};

  ng::mock_chip<8> backend{};
  ng::chip<ng::mock_chip<8>> chip{backend};
  REQUIRE(chip.open(specs, configs).has_value());

  auto const raw{[&](std::uint64_t const seq, std::chrono::nanoseconds const at,
                     bool const physical) {
    ng::line_event event{};
    event.chip = ng::chip_id{0};
    event.offset = ng::line_offset{17};
    event.sequence = ng::event_sequence{seq};
    event.timestamp = ng::event_time{at};
    event.physical = physical;
    event.edge = physical ? ng::edge_kind::rising : ng::edge_kind::falling;
    return event;
  }};

  // Idle high (unpressed), then a press: bounce, bounce, settle low.
  // Sequence 4 was lost to an overflow on the way.
  REQUIRE(backend.inject(raw(1, 0ms, true)));
  REQUIRE(backend.inject(raw(2, 20ms, false)));
  REQUIRE(backend.inject(raw(3, 21ms, true)));
  REQUIRE(backend.inject(raw(5, 22ms, false)));
  REQUIRE(backend.inject(raw(6, 40ms, false)));

  ng::event_debounce debounce{5ms};
  ng::sequence_tracker tracker{};
  std::vector<ng::line_value> presses{};
  auto handler{[&](ng::line_event const& event) noexcept -> bool {
    presses.push_back(ng::decode(specs[0], event));
    return true;
  }};
  ng::callback_sink sink{handler};

  while (true) {
    auto const drained{backend.wait_event(0ns)};
    REQUIRE(drained.has_value());
    if (!drained->has_value()) {
      break;
    }
    // Drops are observed, not fatal: the settled level below still lands.
    utility::discard(tracker.feed((**drained).sequence));
    if (auto const settled{debounce.feed(**drained)}) {
      REQUIRE(sink.push(*settled));
    }
  }

  // One dropped event was detected between 3 and 5.
  CHECK(tracker.dropped() == 1);

  // The burst produced exactly two deliveries: the initial acceptance of the
  // idle level, then ONE settled press.
  REQUIRE(presses.size() == 2);
  CHECK(presses[0].name() == "button");
  CHECK(presses[0].logical() == false);  // idle: wire high, active-low => released
  CHECK(presses[0].edge() == ng::edge_kind::none);

  CHECK(presses[1].logical() == true);  // settled: wire low => pressed
  CHECK(presses[1].edge() == ng::edge_kind::rising);
  CHECK(presses[1].sequence() == ng::event_sequence{6});
}

}  // namespace
