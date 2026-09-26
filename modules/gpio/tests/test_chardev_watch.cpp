/**
 * @file
 * @brief Tests for the line-info watcher over the character device.
 *
 * On a Linux host with an accessible /dev/gpiochip0 the watch is armed
 * against the real kernel, which only reads line info; everywhere else the
 * error paths are exercised. No line is requested: a change pair needs a
 * request, which belongs to the gpio-sim integration tier, not to unit tests
 * that may run next to real hardware.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>

#include <nexenne/gpio/io/chardev_watch.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

TEST_CASE("chardev_watcher: a fresh watcher is closed with no handle") {
  ng::chardev_watcher watcher{ng::chip_id{2}};
  CHECK(watcher.chip() == ng::chip_id{2});
  CHECK_FALSE(watcher.is_open());
  CHECK(watcher.native_handle() == -1);
  CHECK_FALSE(watcher.wait_change(0ns).has_value());

  watcher.close();
  CHECK_FALSE(watcher.is_open());
}

#ifdef __linux__

TEST_CASE("chardev_watcher: watch validates its inputs and maps open errors") {
  ng::chardev_watcher watcher{ng::chip_id{0}};
  CHECK(watcher.watch({}).error() == ng::gpio_error::invalid_argument);

  ng::chardev_watcher remote{ng::chip_id{4000}};
  std::array const offsets{ng::line_offset{0}};
  auto const armed{remote.watch(offsets)};
  REQUIRE_FALSE(armed.has_value());
  CHECK((
    armed.error() == ng::gpio_error::not_found || armed.error() == ng::gpio_error::permission_denied
  ));
}

TEST_CASE("chardev_watcher: a live watch arms and is quiet") {
  ng::chardev_watcher watcher{ng::chip_id{0}};
  std::array const offsets{ng::line_offset{0}};
  auto const armed{watcher.watch(offsets)};
  if (!armed.has_value()) {
    MESSAGE("skipping live watch: ", ng::to_string(armed.error()));
    return;
  }
  CHECK(watcher.is_open());
  CHECK(watcher.native_handle() >= 0);

  auto const quiet{watcher.wait_change(0ns)};
  REQUIRE(quiet.has_value());
  CHECK_FALSE(quiet->has_value());
}

#else

TEST_CASE("chardev_watcher: every operation reports unsupported off Linux") {
  ng::chardev_watcher watcher{};
  std::array const offsets{ng::line_offset{0}};
  CHECK(watcher.watch(offsets).error() == ng::gpio_error::unsupported);
  CHECK(watcher.wait_change(0ns).error() == ng::gpio_error::unsupported);
}

#endif

}  // namespace
