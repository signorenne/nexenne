/**
 * @file
 * @brief Tests for the SocketCAN backend.
 *
 * The frame conversions and the bound-socket error paths are tested directly; a
 * live send and receive needs a CAN interface, so it is left to the runnable
 * example and skipped here.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/socketcan_bus.hpp>
#include <nexenne/utility/flags.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

TEST_CASE("socketcan_bus satisfies the can_bus concept on every platform") {
  static_assert(nc::can_bus<nc::socketcan_bus>);
}

#ifdef __linux__

TEST_CASE("socketcan conversion: classic frame round-trips through can_frame") {
  std::array const payload{b(0xDE), b(0xAD), b(0xBE)};
  auto const original{*nc::frame::classic(nc::can_id::standard(0x123), payload)};

  auto const kernel{nc::to_can_frame(original)};
  CHECK(kernel.can_id == 0x123U);
  CHECK(kernel.can_dlc == 3);
  CHECK(kernel.data[0] == 0xDE);

  auto const back{nc::from_can_frame(kernel)};
  REQUIRE(back.has_value());
  CHECK(*back == original);
}

TEST_CASE("socketcan conversion: an over-long classic frame cannot overrun the kernel struct") {
  // frame::length() hands out a mutable reference documented only as "at most
  // 64", so a Classic frame can carry an FD-sized payload. to_can_frame used
  // to memcpy all of it into an 8-byte can_frame::data, guarded by an assert
  // that vanishes under NDEBUG.
  std::array const payload{b(0x11), b(0x22), b(0x33)};
  auto oversized{*nc::frame::classic(nc::can_id::standard(0x321), payload)};
  oversized.length() = 40;

  auto const kernel{nc::to_can_frame(oversized)};
  CHECK(kernel.can_dlc == nc::max_classic_length);
  CHECK(kernel.data[0] == 0x11);

  // The same length is fine for the FD converter, whose struct is 64 wide.
  auto fd_sized{*nc::frame::fd(nc::can_id::standard(0x321), payload)};
  fd_sized.length() = 40;
  CHECK(nc::to_canfd_frame(fd_sized).len == 48);  // 40 rounds up to 48
}

TEST_CASE("socketcan conversion: an extended id keeps its flag through the kernel word") {
  auto const original{*nc::frame::classic(nc::can_id::extended(0x18FEF100), std::array{b(0x01)})};
  auto const kernel{nc::to_can_frame(original)};
  CHECK((kernel.can_id & nc::extended_flag) != 0U);
  auto const back{nc::from_can_frame(kernel)};
  REQUIRE(back.has_value());
  CHECK(back->id().extended());
  CHECK(back->id().identifier() == 0x18FEF100);
}

TEST_CASE("socketcan conversion: CAN FD frame carries length, BRS, and ESI") {
  std::array<std::byte, 16> payload{};
  payload[0] = b(0x55);
  auto const flags{nexenne::utility::flags<nc::fd_flag>{nc::fd_flag::brs} | nc::fd_flag::esi};
  auto const original{*nc::frame::fd(nc::can_id::extended(0x100), payload, flags)};

  auto const kernel{nc::to_canfd_frame(original)};
  CHECK(kernel.len == 16);
  CHECK((kernel.flags & CANFD_BRS) != 0);
  CHECK((kernel.flags & CANFD_ESI) != 0);

  auto const back{nc::from_canfd_frame(kernel)};
  REQUIRE(back.has_value());
  CHECK(back->is_fd());
  CHECK(back->flags().has(nc::fd_flag::brs));
  CHECK(back->flags().has(nc::fd_flag::esi));
  CHECK(back->length() == 16);
  CHECK(back->data()[0] == b(0x55));
}

TEST_CASE("socketcan conversion: a non-discrete FD length is padded up on the wire") {
  std::array<std::byte, 9> payload{};
  auto const original{*nc::frame::fd(nc::can_id::standard(0x1), payload)};
  CHECK(nc::to_canfd_frame(original).len == 12);  // 9 rounds up to 12
}

TEST_CASE("socketcan_bus: opening a missing interface fails cleanly") {
  auto const bus{nc::socketcan_bus::open("nx_no_such_can")};
  REQUIRE_FALSE(bus.has_value());
  CHECK(bus.error() == nc::can_error::io_error);
}

#endif  // __linux__

}  // namespace
