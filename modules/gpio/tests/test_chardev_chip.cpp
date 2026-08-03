/**
 * @file
 * @brief Tests for the Linux character-device backend.
 *
 * The request-building validation and the closed-state error paths are
 * tested directly; a live open needs a GPIO chip the test user may touch
 * (a gpio-sim chip, the vcan equivalent for GPIO), so live traffic is left
 * to the runnable examples and skipped here.
 */

#include <doctest/doctest.h>

#include <array>
#include <chrono>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/io/chardev_chip.hpp>

namespace {

namespace ng = nexenne::gpio;
using namespace std::chrono_literals;

std::array const specs{
  ng::line_spec::input("in", ng::chip_id{0}, ng::line_offset{0}),
  ng::line_spec::output("out", ng::chip_id{0}, ng::line_offset{1}),
};
std::array const configs{ng::line_config{ng::edge_detection::both}, ng::line_config{}};

TEST_CASE("chardev_chip: satisfies every backend tier on every platform") {
  static_assert(ng::gpio_backend<ng::chardev_chip>);
  static_assert(ng::bulk_gpio_backend<ng::chardev_chip>);
  static_assert(ng::edge_source<ng::chardev_chip>);
  static_assert(ng::reconfigurable_gpio_backend<ng::chardev_chip>);
  static_assert(ng::chardev_chip::max_lines == 64);
  CHECK(true);
}

TEST_CASE("chardev_chip: a fresh backend is closed with no handle") {
  ng::chardev_chip backend{ng::chip_id{3}};
  CHECK(backend.chip() == ng::chip_id{3});
  CHECK_FALSE(backend.is_open());
  CHECK(backend.native_handle() == -1);

  CHECK_FALSE(backend.read(ng::line_offset{0}).has_value());
  CHECK_FALSE(backend.write(ng::line_offset{0}, true).has_value());
  CHECK_FALSE(backend.wait_event(0ns).has_value());
  CHECK_FALSE(backend.reconfigure(specs, configs).has_value());

  backend.close();  // safe when already closed
  CHECK_FALSE(backend.is_open());
}

#ifdef __linux__

TEST_CASE("chardev_chip: open validates the request before touching a device") {
  ng::chardev_chip backend{ng::chip_id{0}};

  // Mismatched, empty, and oversized tables.
  CHECK(
    backend.open(specs, std::span<ng::line_config const>{configs.data(), 1}).error()
    == ng::gpio_error::invalid_argument
  );
  CHECK(backend.open({}, {}).error() == ng::gpio_error::invalid_argument);

  // A spec naming another chip cannot be requested here.
  std::array const foreign{
    ng::line_spec::input("in", ng::chip_id{7}, ng::line_offset{0}),
  };
  std::array const one_config{ng::line_config{}};
  CHECK(backend.open(foreign, one_config).error() == ng::gpio_error::invalid_argument);

  // A consumer label the kernel field cannot hold.
  ng::chardev_chip labeled{
    ng::chip_id{0}, "a-consumer-label-well-beyond-the-31-bytes-the-kernel-allows"
  };
  CHECK(labeled.open(specs, configs).error() == ng::gpio_error::invalid_argument);
}

TEST_CASE("chardev_chip: opening a chip that does not exist maps the errno") {
  // No system has 4000 GPIO chips; the open must fail cleanly, not crash.
  ng::chardev_chip backend{ng::chip_id{4000}};
  std::array const remote{
    ng::line_spec::input("in", ng::chip_id{4000}, ng::line_offset{0}),
  };
  std::array const one_config{ng::line_config{}};

  auto const opened{backend.open(remote, one_config)};
  REQUIRE_FALSE(opened.has_value());
  CHECK(
    (opened.error() == ng::gpio_error::not_found
     || opened.error() == ng::gpio_error::permission_denied)
  );
  CHECK_FALSE(backend.is_open());
}

TEST_CASE("chardev_chip: a debounce period beyond the kernel range is rejected") {
  ng::chardev_chip backend{ng::chip_id{0}};
  std::array const one_spec{
    ng::line_spec::input("in", ng::chip_id{0}, ng::line_offset{0}),
  };
  // The kernel field is 32-bit microseconds; two hours overflows it.
  std::array const huge{ng::line_config{ng::edge_detection::both, 2h}};
  CHECK(backend.open(one_spec, huge).error() == ng::gpio_error::invalid_argument);
}

#else

TEST_CASE("chardev_chip: every operation reports unsupported off Linux") {
  ng::chardev_chip backend{};
  CHECK(backend.open(specs, configs).error() == ng::gpio_error::unsupported);
  CHECK(backend.read(ng::line_offset{0}).error() == ng::gpio_error::unsupported);
  CHECK(backend.wait_event(0ns).error() == ng::gpio_error::unsupported);
}

#endif

}  // namespace
