/**
 * @file
 * @brief watch: observe who requests, releases, and reconfigures lines.
 *
 * Arms a line-info watch on every line of one chip and prints each change
 * as it happens: which line, what changed, and who holds it now. Run it in
 * one terminal and use gpioset, gpiomon, or the blink example in another
 * to see the ownership changes live. Watching is read-only and never
 * claims a line. Pass the chip index (default 0); stops after 30 seconds
 * without a change.
 */

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <print>
#include <vector>

#include <nexenne/gpio/format.hpp>
#include <nexenne/gpio/io/chardev_info.hpp>
#include <nexenne/gpio/io/chardev_watch.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

}  // namespace

auto main(int const argc, char** const argv) -> int {
  auto const chip_index{
    static_cast<std::uint16_t>(argc > 1 ? std::strtoul(argv[1], nullptr, 10) : 0)
  };

  auto const info{ng::read_chip_info(ng::chip_id{chip_index})};
  if (!info.has_value()) {
    std::println("cannot describe gpiochip{}: {}", chip_index, info.error());
    return 0;
  }

  // Watch every line the chip has, up to the per-watcher capacity.
  auto const count{
    std::min<std::uint32_t>(info->lines(), ng::chardev_watcher::max_lines)
  };
  std::vector<ng::line_offset> offsets{};
  offsets.reserve(count);
  for (std::uint32_t offset{0}; offset < count; ++offset) {
    offsets.push_back(ng::line_offset{offset});
  }

  ng::chardev_watcher watcher{ng::chip_id{chip_index}};
  if (auto const armed{watcher.watch(offsets)}; !armed.has_value()) {
    std::println("cannot watch gpiochip{}: {}", chip_index, armed.error());
    return 0;
  }

  std::println("watching {} lines of {}; trigger changes from another terminal", count, *info);
  while (true) {
    auto const change{watcher.wait_change(30s)};
    if (!change.has_value()) {
      std::println("wait failed: {}", change.error());
      return 1;
    }
    if (!change->has_value()) {
      std::println("no change within 30s; stopping");
      return 0;
    }
    std::println("{}", **change);
  }
}
