/**
 * @file
 * @brief Tests for the CAN identifier type.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <format>

#include <nexenne/can/format.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("can_id: standard identifier carries its value and clears the extended flag") {
  constexpr auto id{nc::can_id::standard(0x123)};
  static_assert(!id.extended());
  static_assert(id.identifier() == 0x123);
  CHECK_FALSE(id.remote());
  CHECK_FALSE(id.error_frame());
}

TEST_CASE("can_id: extended identifier sets the extended flag and keeps 29 bits") {
  constexpr auto id{nc::can_id::extended(0x18FE'F100)};
  static_assert(id.extended());
  static_assert(id.identifier() == 0x18FE'F100);
}

TEST_CASE("can_id: raw word matches the SocketCAN layout") {
  // Extended id with the extended flag in the top bit.
  CHECK(nc::can_id::extended(0x1).raw() == (0x1U | nc::extended_flag));
  // Standard id is just the value, no flags.
  CHECK(nc::can_id::standard(0x7FF).raw() == 0x7FFU);
  // from_raw round-trips the stored word verbatim.
  constexpr std::uint32_t word{0x18FE'F100U | nc::extended_flag};
  CHECK(nc::can_id::from_raw(word).raw() == word);
  CHECK(nc::can_id::from_raw(word).extended());
  CHECK(nc::can_id::from_raw(word).identifier() == 0x18FE'F100);
}

TEST_CASE("can_id: the remote flag is a settable accessor that leaves the id alone") {
  auto id{nc::can_id::standard(0x200)};
  id.remote() = true;
  CHECK(id.remote());
  CHECK(id.identifier() == 0x200);
  id.remote() = false;
  CHECK_FALSE(id.remote());
}

TEST_CASE("can_id: equality compares the whole word") {
  CHECK(nc::can_id::standard(0x10) == nc::can_id::standard(0x10));
  CHECK(nc::can_id::standard(0x10) != nc::can_id::extended(0x10));
  auto remote{nc::can_id::standard(0x10)};
  remote.remote() = true;
  CHECK(nc::can_id::standard(0x10) != remote);
}

TEST_CASE("can_id: format prints the value, width, and flags") {
  CHECK(std::format("{}", nc::can_id::standard(0x123)) == "0x123 std");
  CHECK(std::format("{}", nc::can_id::extended(0x18FEF100)) == "0x18FEF100 ext");
  auto rtr{nc::can_id::standard(0x7E0)};
  rtr.remote() = true;
  CHECK(std::format("{}", rtr) == "0x7E0 std rtr");
}

}  // namespace
