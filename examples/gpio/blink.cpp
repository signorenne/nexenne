/**
 * @file
 * @brief blink: drive an output line on real Linux hardware.
 *
 * The GPIO hello world: request one output line through the character
 * device and toggle it. Pass the chip index and line offset on the command
 * line (default chip 0, line 4). Exits cleanly with a message when the
 * device is missing, busy, or not permitted, so it is safe to try anywhere;
 * on a Raspberry Pi wire an LED to the chosen line and watch it blink.
 */

#include <array>
#include <chrono>
#include <cstdlib>
#include <print>
#include <thread>

#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/format.hpp>
#include <nexenne/gpio/io/chardev_chip.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

}  // namespace

auto main(int const argc, char** const argv) -> int {
  auto const chip_index{
    static_cast<std::uint16_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0)
  };
  auto const offset{
    static_cast<std::uint32_t>(argc > 2 ? std::strtoul(argv[2], nullptr, 10) : 4)
  };

  std::array const specs{
    ng::line_spec::output("led", ng::chip_id{chip_index}, ng::line_offset{offset}),
  };
  std::array const configs{ng::line_config{}};

  ng::chardev_chip backend{ng::chip_id{chip_index}, "nexenne-blink"};
  ng::chip<ng::chardev_chip> chip{backend};
  if (auto const opened{chip.open(specs, configs)}; !opened.has_value()) {
    std::println(
      "cannot open gpiochip{} line {}: {} (missing hardware, permissions, or busy)",
      chip_index, offset, opened.error()
    );
    return 0;
  }

  std::println("blinking gpiochip{} line {}; ctrl-c to stop", chip_index, offset);
  auto led{*chip.line_for("led")};
  for (int i{0}; i < 10; ++i) {
    if (auto const toggled{led.toggle()}; !toggled.has_value()) {
      std::println("toggle failed: {}", toggled.error());
      return 1;
    }
    std::println("led is {}", *led.read() ? "on" : "off");
    std::this_thread::sleep_for(300ms);
  }
  return 0;
}
