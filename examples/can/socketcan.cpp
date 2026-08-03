/**
 * @file
 * @brief socketcan: sending and receiving over a real Linux CAN interface.
 *
 * This is the SocketCAN backend in action. It opens a CAN interface, installs a
 * hardware filter, sends a frame, and reads one back. It runs against a virtual
 * CAN interface so no hardware is needed; set one up with:
 *
 *   sudo ip link add dev vcan0 type vcan
 *   sudo ip link set up vcan0
 *
 * If the interface is missing (or this is not Linux), the example reports the
 * error and exits cleanly, so it is always safe to run.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/error.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/socketcan_bus.hpp>
#include <nexenne/can/socket_options.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  nc::socket_options options;
  options.receive_own_messages = true;  // so a sent frame reads back on the same socket

  auto bus{nc::socketcan_bus::open("vcan0", options)};
  if (!bus) {
    std::println(
      "socketcan unavailable ({}); set up vcan0 to run this for real", nc::to_string(bus.error())
    );
    return 0;
  }

  // Accept only standard id 0x123.
  std::array const filters{nc::filter::equals(nc::can_id::standard(0x123))};
  if (auto const set{bus->set_filters(filters)}; !set) {
    std::println("set_filters failed: {}", nc::to_string(set.error()));
    return 1;
  }

  std::array const payload{byte_of(0xDE), byte_of(0xAD), byte_of(0xBE), byte_of(0xEF)};
  auto const tx{*nc::frame::classic(nc::can_id::standard(0x123), payload)};
  if (auto const sent{bus->send(tx)}; !sent) {
    std::println("send failed: {}", nc::to_string(sent.error()));
    return 1;
  }
  std::println("sent     {}", tx);

  auto const received{bus->receive()};
  if (received && received->has_value()) {
    std::println("received {}", **received);
  } else if (received) {
    std::println("no frame ready");
  } else {
    std::println("receive failed: {}", nc::to_string(received.error()));
  }
  std::println("bus state: {}", bus->state());

  return 0;
}
