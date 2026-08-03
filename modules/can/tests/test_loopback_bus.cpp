/**
 * @file
 * @brief Tests for the in-memory loopback bus.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

auto frame_of(nc::can_id const id) -> nc::frame {
  return *nc::frame::classic(id, std::array{b(0x01), b(0x02)});
}

TEST_CASE("loopback_bus satisfies the can_bus concept") {
  static_assert(nc::can_bus<nc::loopback_bus<>>);
}

TEST_CASE("loopback_bus: a sent frame is received back unchanged") {
  nc::loopback_bus bus;
  auto const sent{frame_of(nc::can_id::standard(0x123))};
  REQUIRE(bus.send(sent).has_value());
  CHECK(bus.pending() == 1);

  auto const got{bus.receive()};
  REQUIRE(got.has_value());
  REQUIRE(got->has_value());
  CHECK(**got == sent);
  CHECK(bus.empty());
}

TEST_CASE("loopback_bus: receiving an empty bus yields no frame, not an error") {
  nc::loopback_bus bus;
  auto const got{bus.receive()};
  REQUIRE(got.has_value());
  CHECK_FALSE(got->has_value());
}

TEST_CASE("loopback_bus: send reports a full queue") {
  nc::loopback_bus<2> bus;
  REQUIRE(bus.send(frame_of(nc::can_id::standard(0x1))).has_value());
  REQUIRE(bus.send(frame_of(nc::can_id::standard(0x2))).has_value());
  auto const overflow{bus.send(frame_of(nc::can_id::standard(0x3)))};
  REQUIRE_FALSE(overflow.has_value());
  CHECK(overflow.error() == nc::can_error::buffer_full);
}

TEST_CASE("loopback_bus: filters drop unaccepted frames on receive") {
  nc::loopback_bus bus;
  std::array const filters{nc::filter::equals(nc::can_id::standard(0x200))};
  REQUIRE(bus.set_filters(filters).has_value());

  REQUIRE(bus.send(frame_of(nc::can_id::standard(0x100))).has_value());  // dropped
  REQUIRE(bus.send(frame_of(nc::can_id::standard(0x200))).has_value());  // kept

  auto const got{bus.receive()};
  REQUIRE(got.has_value());
  REQUIRE(got->has_value());
  CHECK((*got)->id() == nc::can_id::standard(0x200));
  // The dropped frame was consumed, so the bus is now empty.
  CHECK(bus.empty());
}

TEST_CASE("loopback_bus: state is always error-active and clear empties the queue") {
  nc::loopback_bus bus;
  CHECK(bus.state() == nc::bus_state::error_active);
  REQUIRE(bus.send(frame_of(nc::can_id::standard(0x1))).has_value());
  bus.clear();
  CHECK(bus.empty());
}

}  // namespace
