/**
 * @file
 * @brief loopback: a full send and receive round trip with no hardware.
 *
 * This showcase ties the module together. It defines a small database, encodes
 * signals into a frame, sends the frame over an in-memory loopback bus, receives
 * it back, and decodes its signals through a registry. It is the path a real
 * application follows, with the SocketCAN backend swapped for the loopback bus so
 * the example runs anywhere.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace nc = nexenne::can;

}  // namespace

auto main() -> int {
  // 1. Describe the bus.
  auto const speed{nc::signal_builder{}
                     .name("speed")
                     .start_bit(0)
                     .length(16)
                     .endianness(nc::byte_order::little_endian)
                     .scale(0.01)
                     .unit("km/h")
                     .build()};
  auto const oil_temp{nc::signal_builder{}
                        .name("oil_temp")
                        .start_bit(16)
                        .length(8)
                        .endianness(nc::byte_order::little_endian)
                        .scale(1.0)
                        .offset(-40.0)
                        .unit("C")
                        .build()};
  auto const db{
    nc::database_builder{}
      .add_message(
        nc::message_builder{nc::can_id::standard(0x100), "status"}.add(speed).add(oil_temp).build()
      )
      .build()
  };
  nc::registry const reg{db};

  // 2. Encode a frame to transmit.
  std::array<std::byte, 8> bytes{};
  auto tx{*nc::frame::classic(nc::can_id::standard(0x100), bytes)};
  nexenne::utility::discard(nc::encode(speed, nc::packing_plan::from_signal(speed), tx, 87.5));
  nexenne::utility::discard(nc::encode(oil_temp, nc::packing_plan::from_signal(oil_temp), tx, 90.0)
  );

  // 3. Send it over the loopback bus.
  nc::loopback_bus bus;
  if (auto const sent{bus.send(tx)}; !sent) {
    std::println("send failed: {}", nc::to_string(sent.error()));
    return 1;
  }
  std::println("sent     {}", tx);

  // 4. Receive and decode.
  auto const received{bus.receive()};
  if (!received || !received->has_value()) {
    std::println("nothing received");
    return 1;
  }
  auto const& rx{**received};
  std::println("received {}", rx);

  if (auto const* const msg{reg.match(rx)}) {
    std::println("decoded '{}':", msg->name());
    reg.decode_signals(rx, [](nc::signal const& sig, double const value) {
      std::println("  {} = {} {}", sig.name(), value, sig.unit());
    });
  }

  return 0;
}
