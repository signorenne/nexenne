/**
 * @file
 * @brief edges: wait for debounced button edges on real Linux hardware.
 *
 * Requests one input line with both-edge detection and kernel debounce,
 * then blocks on \c wait_event, tracking dropped events and decoding each
 * edge into the logical domain. Pass the chip index and line offset on the
 * command line (default chip 0, line 17). Exits cleanly with a message when
 * the device is missing, busy, or not permitted; on a Raspberry Pi wire a
 * button between the chosen line and ground.
 */

#include <array>
#include <chrono>
#include <cstdlib>
#include <print>

#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/decode.hpp>
#include <nexenne/gpio/format.hpp>
#include <nexenne/gpio/io/chardev_chip.hpp>
#include <nexenne/gpio/sequence_tracker.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

}  // namespace

auto main(int const argc, char** const argv) -> int {
  auto const chip_index{
    static_cast<std::uint16_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0)
  };
  auto const offset{static_cast<std::uint32_t>(argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 17)};

  // Active-low behind the internal pull-up: pressing shorts the line to
  // ground, and the spec keeps the program in "pressed = true" terms.
  std::array const specs{
    ng::line_spec::input(
      "button",
      ng::chip_id{chip_index},
      ng::line_offset{offset},
      ng::line_polarity::active_low,
      ng::line_bias::pull_up
    ),
  };
  // The kernel debounces for us; no userspace debouncer is needed here.
  std::array const configs{ng::line_config{ng::edge_detection::both, 10ms}};

  ng::chardev_chip backend{ng::chip_id{chip_index}, "nexenne-edges"};
  ng::chip<ng::chardev_chip> chip{backend};
  if (auto const opened{chip.open(specs, configs)}; !opened.has_value()) {
    std::println(
      "cannot open gpiochip{} line {}: {} (missing hardware, permissions, or busy)",
      chip_index,
      offset,
      opened.error()
    );
    return 0;
  }

  std::println("waiting for edges on gpiochip{} line {} (10 events, 30s)", chip_index, offset);
  ng::sequence_tracker tracker{};
  for (int seen{0}; seen < 10;) {
    auto const event{backend.wait_event(30s)};
    if (!event.has_value()) {
      std::println("wait failed: {}", event.error());
      return 1;
    }
    if (!event->has_value()) {
      std::println("no edge within 30s; stopping");
      break;
    }
    if (auto const lost{tracker.feed((**event).sequence)}; lost > 0) {
      std::println("overflow: {} events lost", lost);
    }
    std::println("{}", ng::decode(specs[0], **event));
    seen += 1;
  }
  std::println("total dropped: {}", tracker.dropped());
  return 0;
}
