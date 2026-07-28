/**
 * @file
 * @brief discovery: list GPIO chips and their lines from the kernel.
 *
 * A read-only walk over /dev/gpiochipN: for each chip that exists, print
 * its identity and every line the kernel describes, including who holds it.
 * This is how a program resolves a device-tree line name to an offset
 * instead of hard-coding the magic number that breaks on the next board
 * revision. Safe to run anywhere; off Linux it reports unsupported.
 */

#include <print>

#include <nexenne/gpio/format.hpp>
#include <nexenne/gpio/io/chardev_info.hpp>

namespace {

namespace ng = nexenne::gpio;

}  // namespace

auto main() -> int {
  bool found{false};
  for (std::uint16_t index{0}; index < 16; ++index) {
    auto const chip{ng::read_chip_info(ng::chip_id{index})};
    if (!chip.has_value()) {
      if (chip.error() == ng::gpio_error::unsupported) {
        std::println("GPIO discovery is unsupported on this platform.");
        return 0;
      }
      // A missing index just ends the walk; permissions end it loudly.
      if (chip.error() == ng::gpio_error::permission_denied) {
        std::println("gpiochip{}: permission denied (udev rules or group?)", index);
      }
      continue;
    }
    found = true;
    std::println("{}", *chip);
    for (std::uint32_t offset{0}; offset < chip->lines(); ++offset) {
      auto const line{ng::read_line_info(ng::chip_id{index}, ng::line_offset{offset})};
      if (line.has_value()) {
        std::println("  {}", *line);
      }
    }
  }
  if (!found) {
    std::println("no accessible GPIO chip found");
  }
  return 0;
}
