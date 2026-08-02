/**
 * @file
 * @brief Tests for the line-info watcher over the character device.
 *
 * On a Linux host with an accessible /dev/gpiochip0 the watch is armed
 * against the real kernel and, when a line can also be requested, a real
 * requested/released change pair is observed end to end; everywhere else
 * the error paths are exercised.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>

#include <nexenne/gpio/io/chardev_chip.hpp>
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

  watcher.close();  // safe when already closed
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

TEST_CASE("chardev_watcher: live watch sees a request and a release") {
  ng::chardev_watcher watcher{ng::chip_id{0}};
  std::array const offsets{ng::line_offset{0}};
  auto const armed{watcher.watch(offsets)};
  if (!armed.has_value()) {
    MESSAGE("skipping live watch: ", ng::to_string(armed.error()));
    return;
  }
  CHECK(watcher.is_open());
  CHECK(watcher.native_handle() >= 0);

  // Nothing has changed yet: a zero-timeout wait is a clean miss.
  auto const quiet{watcher.wait_change(0ns)};
  REQUIRE(quiet.has_value());
  CHECK_FALSE(quiet->has_value());

  // Request the watched line as an input (harmless) and release it; the
  // watcher must observe both transitions with our consumer label.
  ng::chardev_chip requester{ng::chip_id{0}, "nexenne-watch-test"};
  std::array const specs{ng::line_spec::input("probe", ng::chip_id{0}, ng::line_offset{0})};
  std::array const configs{ng::line_config{}};
  if (!requester.open(specs, configs).has_value()) {
    MESSAGE("skipping live change check: line 0 not requestable here");
    return;
  }

  auto const requested{watcher.wait_change(500ms)};
  REQUIRE(requested.has_value());
  REQUIRE(requested->has_value());
  CHECK((**requested).kind == ng::line_change_kind::requested);
  CHECK((**requested).info.offset() == ng::line_offset{0});
  CHECK((**requested).info.used());
  CHECK((**requested).info.consumer() == "nexenne-watch-test");

  requester.close();
  auto const released{watcher.wait_change(500ms)};
  REQUIRE(released.has_value());
  REQUIRE(released->has_value());
  CHECK((**released).kind == ng::line_change_kind::released);
  CHECK_FALSE((**released).info.used());
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
