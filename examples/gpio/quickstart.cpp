/**
 * @file
 * @brief quickstart: a guided tour of nexenne::gpio, the one-file cookbook.
 *
 * Read this first. It walks the whole module hardware-free over the mock
 * backend: describe lines with specs, open a chip, read and write in the
 * logical domain, mint line handles, and run the full edge path (drain,
 * drop-track, debounce, decode). Swap \c mock_chip for \c chardev_chip and
 * the same code runs on real Linux hardware; the focused examples drill
 * into that.
 */

#include <array>
#include <chrono>
#include <print>

#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/debounce.hpp>
#include <nexenne/gpio/decode.hpp>
#include <nexenne/gpio/format.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>
#include <nexenne/gpio/line.hpp>
#include <nexenne/gpio/sequence_tracker.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

}  // namespace

auto main() -> int {
  // 1) Describe the lines once. The button is wired active-low behind a
  //    pull-up, so "pressed" is a low wire; the spec records that and the
  //    rest of the program never thinks about it again.
  std::array const specs{
    ng::line_spec::input(
      "button", ng::chip_id{0}, ng::line_offset{17}, ng::line_polarity::active_low,
      ng::line_bias::pull_up
    ),
    ng::line_spec::output("led", ng::chip_id{0}, ng::line_offset{4}),
  };
  std::array const configs{
    ng::line_config{ng::edge_detection::both, 5ms},
    ng::line_config{},
  };

  // 2) Open a backend through the name-addressed chip handle.
  ng::mock_chip<16> backend{};
  ng::chip<ng::mock_chip<16>> chip{backend};
  if (auto const opened{chip.open(specs, configs)}; !opened.has_value()) {
    std::println("open failed: {}", opened.error());
    return 1;
  }
  std::println("opened {} lines:", chip.specs().size());
  for (auto const& spec : chip.specs()) {
    std::println("  {}", spec);
  }

  // 3) Logical-domain read and write by name.
  nexenne::utility::discard(backend.set_physical(ng::line_offset{17}, false));  // press
  std::println("button pressed: {}", *chip.read("button"));
  nexenne::utility::discard(chip.write("led", true));

  // 4) Or mint a cheap line handle and keep it.
  auto led{*chip.line_for("led")};
  nexenne::utility::discard(led.toggle());
  std::println("led after toggle: {}", *led.read());

  // 5) The edge path: the backend emits raw physical events; the drain loop
  //    tracks drops, debounces the bounce burst, and decodes to logical.
  auto const raw{[](std::uint64_t const seq, std::chrono::nanoseconds const at,
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
  // One statement per inject: argument evaluation order is unspecified, and
  // the queue must receive these in chronological order.
  nexenne::utility::discard(backend.inject(raw(1, 0ms, true)));    // idle high (released)
  nexenne::utility::discard(backend.inject(raw(2, 20ms, false)));  // press: bounce...
  nexenne::utility::discard(backend.inject(raw(3, 21ms, true)));   //        ...bounce...
  nexenne::utility::discard(backend.inject(raw(5, 22ms, false)));  //        ...settles (4 lost)
  nexenne::utility::discard(backend.inject(raw(6, 40ms, false)));

  ng::event_debounce debounce{5ms};
  ng::sequence_tracker tracker{};
  while (true) {
    auto const drained{backend.wait_event(0ms)};
    if (!drained.has_value() || !drained->has_value()) {
      break;
    }
    nexenne::utility::discard(tracker.feed((**drained).sequence));
    if (auto const settled{debounce.feed(**drained)}) {
      std::println("settled: {}", ng::decode(specs[0], *settled));
    }
  }
  std::println("events dropped upstream: {}", tracker.dropped());
  return 0;
}
