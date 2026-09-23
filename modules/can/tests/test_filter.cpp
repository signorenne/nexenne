/**
 * @file
 * @brief Tests for the CAN identifier filter.
 */

#include <doctest/doctest.h>

#include <format>

#include <nexenne/can/filter.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("filter: equals accepts one identifier and format") {
  auto const f{nc::filter::equals(nc::can_id::standard(0x100))};
  CHECK(f.matches(nc::can_id::standard(0x100)));
  CHECK_FALSE(f.matches(nc::can_id::standard(0x101)));
  // Same numeric value but extended format does not match.
  CHECK_FALSE(f.matches(nc::can_id::extended(0x100)));
  // The remote flag is ignored.
  auto rtr{nc::can_id::standard(0x100)};
  rtr.remote() = true;
  CHECK(f.matches(rtr));
}

TEST_CASE("filter: a mask accepts a family of identifiers") {
  // Accept any standard id whose top 4 bits are 0x7, i.e. 0x700..0x7FF.
  auto const f{nc::filter::standard(0x700, 0x700)};
  CHECK(f.matches(nc::can_id::standard(0x700)));
  CHECK(f.matches(nc::can_id::standard(0x7AB)));
  CHECK_FALSE(f.matches(nc::can_id::standard(0x100)));
}

TEST_CASE("filter: extended factory requires the extended flag") {
  auto const f{nc::filter::extended(0x18FEF100)};
  CHECK(f.matches(nc::can_id::extended(0x18FEF100)));
  CHECK_FALSE(f.matches(nc::can_id::standard(0x100)));
}

TEST_CASE("filter: equality and format") {
  CHECK(nc::filter::standard(0x100) == nc::filter::standard(0x100));
  CHECK(nc::filter{0x100, 0x7FF} != nc::filter{0x100, 0x700});
  CHECK(std::format("{}", nc::filter{0x100, 0x7FF}) == "filter(id=0x100, mask=0x7FF)");
}

TEST_CASE("filter::equals keeps the error flag out of the id (can-04)") {
  // Bit 29 is the error flag in a can_id but CAN_INV_FILTER in a kernel filter
  // id, so an error-frame id produced an inverted hardware filter.
  auto id{nc::can_id::standard(0x123)};
  id.error_frame() = true;
  auto const f{nc::filter::equals(id)};
  CHECK((f.id() & nc::error_flag) == 0U);
  CHECK(f.matches(nc::can_id::standard(0x123)));
}

}  // namespace
