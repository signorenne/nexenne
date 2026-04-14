/**
 * @file
 * @brief frame: building and inspecting Classic CAN and CAN FD frames.
 *
 * A nexenne::can::frame is the unit on the bus: an identifier, a data length,
 * the payload bytes, and the CAN FD flags. One trivially copyable type covers
 * both Classic CAN (up to 8 bytes) and CAN FD (up to 64 bytes). This tour builds
 * one of each, shows the identifier helpers, and prints them with std::format.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  // A Classic CAN frame: an 11-bit standard id and up to 8 data bytes
  std::array const payload{byte_of(0xDE), byte_of(0xAD), byte_of(0xBE), byte_of(0xEF)};
  auto const classic{nc::frame::classic(nc::can_id::standard(0x123), payload)};
  if (!classic) {
    std::println("classic frame rejected: {}", nc::to_string(classic.error()));
    return 1;
  }
  std::println("classic: {}", *classic);
  std::println(
    "  id 0x{:X}, extended {}, length {}",
    classic->id().identifier(),
    classic->id().extended(),
    classic->length()
  );

  // A CAN FD frame: a 29-bit extended id, up to 64 bytes, bit-rate switch
  std::array<std::byte, 16> fd_payload{};
  fd_payload[0] = byte_of(0x55);
  fd_payload[15] = byte_of(0xAA);
  auto const fd{nc::frame::fd(nc::can_id::extended(0x18FEF100), fd_payload, nc::fd_flag::brs)};
  if (!fd) {
    std::println("fd frame rejected: {}", nc::to_string(fd.error()));
    return 1;
  }
  std::println("fd:      {}", *fd);
  std::println(
    "  is_fd {}, brs {}, length {}", fd->is_fd(), fd->flags().has(nc::fd_flag::brs), fd->length()
  );

  // The DLC mapping that CAN FD uses on the wire
  // A 9-byte FD payload does not fit a discrete size, so a transmitter pads it.
  std::println(
    "an FD payload of 9 bytes is sent as {} bytes (DLC {})",
    nc::fd_padded_length(9),
    nc::fd_length_to_dlc(9)
  );

  return 0;
}
