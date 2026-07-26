/**
 * @file
 * @brief Tests for chip and line discovery over the character device.
 *
 * On a Linux host with an accessible /dev/gpiochip0 the discovery calls run
 * against the real kernel (they are read-only); everywhere else the error
 * paths are exercised.
 */

#include <doctest/doctest.h>

#include <array>

#include <nexenne/gpio/io/chardev_info.hpp>

namespace {

namespace ng = nexenne::gpio;

TEST_CASE("chip_info and line_info: self-contained records with sane defaults") {
  ng::chip_info const chip{};
  CHECK(chip.name().empty());
  CHECK(chip.label().empty());
  CHECK(chip.lines() == 0);

  ng::line_info const line{};
  CHECK(line.name().empty());
  CHECK(line.consumer().empty());
  CHECK(line.direction() == ng::line_direction::input);
  CHECK_FALSE(line.used());
  CHECK_FALSE(line.active_low());
}

TEST_CASE("chip_info: views terminate at the kernel NUL") {
  std::array<char, 32> name{};
  name[0] = 'g';
  name[1] = 'p';
  name[2] = '\0';
  name[3] = 'x';  // garbage after the terminator must not leak into the view
  ng::chip_info const info{name, {}, 8};

  CHECK(info.name() == "gp");
  CHECK(info.lines() == 8);
}

#ifdef __linux__

TEST_CASE("discovery: a chip index that cannot exist reports not_found") {
  auto const info{ng::read_chip_info(ng::chip_id{4000})};
  REQUIRE_FALSE(info.has_value());
  CHECK(
    (info.error() == ng::gpio_error::not_found
     || info.error() == ng::gpio_error::permission_denied)
  );
}

TEST_CASE("discovery: live chip walk when /dev/gpiochip0 is accessible") {
  auto const info{ng::read_chip_info(ng::chip_id{0})};
  if (!info.has_value()) {
    // No chip or no permission on this host: nothing else is testable live.
    MESSAGE("skipping live discovery: ", ng::to_string(info.error()));
    return;
  }

  CHECK_FALSE(info->name().empty());
  REQUIRE(info->lines() > 0);

  // Every advertised offset must be describable; one past the end must not.
  auto const first{ng::read_line_info(ng::chip_id{0}, ng::line_offset{0})};
  REQUIRE(first.has_value());
  CHECK(first->offset() == ng::line_offset{0});

  auto const past{ng::read_line_info(ng::chip_id{0}, ng::line_offset{info->lines()})};
  CHECK_FALSE(past.has_value());

  // A name no board uses is a clean miss, not an error.
  auto const missing{ng::find_line(ng::chip_id{0}, "nexenne-no-such-line-name")};
  REQUIRE(missing.has_value());
  CHECK_FALSE(missing->has_value());
}

#else

TEST_CASE("discovery: every call reports unsupported off Linux") {
  CHECK(ng::read_chip_info(ng::chip_id{0}).error() == ng::gpio_error::unsupported);
  CHECK(
    ng::read_line_info(ng::chip_id{0}, ng::line_offset{0}).error()
    == ng::gpio_error::unsupported
  );
  CHECK(ng::find_line(ng::chip_id{0}, "x").error() == ng::gpio_error::unsupported);
}

#endif

}  // namespace
