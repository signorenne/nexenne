/**
 * @file
 * @brief database: defining messages and decoding a received frame.
 *
 * A nexenne::can::database is a programmatic description of a bus: each message
 * with its identifier and signals. A registry indexes a database for fast lookup
 * and decodes a whole frame's signals through a callback. This tour builds a two
 * message database, then decodes an incoming frame into named physical values,
 * the way a receiver turns raw bytes into engineering units.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  // Describe the bus: one standard status message with two signals, and one
  // extended engine message.
  auto const status{nc::message_builder{nc::can_id::standard(0x100), "status"}
                      .add(
                        nc::signal_builder{}
                          .name("speed")
                          .start_bit(0)
                          .length(16)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.01)
                          .unit("km/h")
                          .build()
                      )
                      .add(
                        nc::signal_builder{}
                          .name("oil_temp")
                          .start_bit(16)
                          .length(8)
                          .endianness(nc::byte_order::little_endian)
                          .scale(1.0)
                          .offset(-40.0)
                          .unit("C")
                          .build()
                      )
                      .build()};
  auto const engine{nc::message_builder{nc::can_id::extended(0x18FEF100), "engine"}
                      .add(
                        nc::signal_builder{}
                          .name("rpm")
                          .start_bit(0)
                          .length(16)
                          .endianness(nc::byte_order::little_endian)
                          .scale(0.125)
                          .unit("rpm")
                          .build()
                      )
                      .build()};

  auto const db{nc::database_builder{}.add_message(status).add_message(engine).build()};
  std::println("{}", db);

  // A registry indexes the database for the receive path.
  nc::registry const reg{db};

  // A frame arrives: speed raw 5000 -> 50.0 km/h, oil_temp raw 130 -> 90 C.
  std::array const payload{byte_of(0x88), byte_of(0x13), byte_of(0x82)};
  auto const incoming{*nc::frame::classic(nc::can_id::standard(0x100), payload)};
  std::println("received {}", incoming);

  if (auto const* const msg{reg.match(incoming)}) {
    std::println("decoded message '{}':", msg->name());
  }
  reg.decode_signals(incoming, [](nc::signal const& sig, double const value) {
    std::println("  {} = {} {}", sig.name(), value, sig.unit());
  });

  return 0;
}
