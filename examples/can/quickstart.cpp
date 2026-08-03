/**
 * @file
 * @brief quickstart: a guided tour of nexenne::can, the one-file cookbook.
 *
 * Read this first. It walks the whole module in numbered sections you can copy
 * from: build signals and a database with the builders, encode and decode through
 * a registry, work with CAN FD, set identifier masks, use the payload-fill and
 * not-available conventions, read and write text fields, decode a multiplexed
 * message, do a hardware-free send and receive round trip, and decode a J1939 id.
 * Each focused example file drills into one of these; here they sit side by side.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/byte_field.hpp>
#include <nexenne/can/codec.hpp>
#include <nexenne/can/database.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/io/loopback_bus.hpp>
#include <nexenne/can/j1939_id.hpp>
#include <nexenne/can/message_builder.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/registry.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto byte_of(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

}  // namespace

auto main() -> int {
  // 1. Build a signal with the builder: one named setter per field. A 16-bit
  //    little-endian speed scaled by 0.01 km/h, clamped to a physical range.
  auto const speed{nc::signal_builder{}
                     .name("speed")
                     .start_bit(0)
                     .length(16)
                     .endianness(nc::byte_order::little_endian)
                     .is_signed(false)
                     .scale(0.01)
                     .offset(0.0)
                     .minimum(0.0)
                     .maximum(655.35)
                     .unit("km/h")
                     .build()};
  auto const oil_temp{nc::signal_builder{}
                        .name("oil_temp")
                        .start_bit(16)
                        .length(8)
                        .scale(1.0)
                        .offset(-40.0)
                        .unit("C")
                        .invalid(nc::invalid_value::all_ones)  // 0xFF means "not available"
                        .build()};

  // 2. Group signals into a message, and messages into a database.
  auto const db{nc::database_builder{}
                  .add_message(nc::message_builder{nc::can_id::standard(0x100), "vehicle_status"}
                                 .add(speed)
                                 .add(oil_temp)
                                 .build())
                  .build()};
  nc::registry const reg{db};

  // 3. Encode: pack physical values into a frame. Start from 0xFF so any byte we
  //    do not set reads back as "not available".
  auto tx{*nc::frame::filled(nc::can_id::standard(0x100), 8, byte_of(0xFF))};
  nexenne::utility::discard(nc::encode(speed, nc::packing_plan::from_signal(speed), tx, 87.5));
  nexenne::utility::discard(nc::encode(oil_temp, nc::packing_plan::from_signal(oil_temp), tx, 90.0)
  );
  std::println("1-3 encoded: {}", tx);

  // 4. Decode: the registry calls back per signal with its physical value, and
  //    skips signals whose field reads as not-available.
  std::println("    decoded:");
  reg.decode_signals(tx, [](nc::signal const& sig, double const value) {
    std::println("      {} = {} {}", sig.name(), value, sig.unit());
  });

  // 5. CAN FD: up to 64 data bytes, with the bit-rate-switch flag.
  std::array<std::byte, 16> fd_payload{};
  fd_payload[0] = byte_of(0x5A);
  auto const fd{*nc::frame::fd(nc::can_id::extended(0x18FEF100), fd_payload, nc::fd_flag::brs)};
  std::println("5   CAN FD: {}", fd);

  // 6. Filters and masks: id + mask, where a mask bit of 0 is "don't care".
  auto const family{nc::filter::standard(0x700, 0x700)};  // accepts 0x700..0x7FF
  std::println(
    "6   filter {} accepts 0x7AB: {}", family, family.matches(nc::can_id::standard(0x7AB))
  );

  // 7. Text fields: a named byte range read and written as a string.
  std::array<std::byte, 8> text_bytes{};
  auto text_frame{*nc::frame::classic(nc::can_id::standard(0x700), text_bytes)};
  nc::byte_field const tag{0, 8, "tag"};
  nexenne::utility::discard(nc::write_text(tag, text_frame, "HELLO"));
  std::println("7   text field: '{}'", nc::read_text(tag, text_frame).value_or(""));

  // 8. Multiplexing: a selector byte picks which signals are present.
  auto const mux_db{nc::database_builder{}
                      .add_message(nc::message_builder{nc::can_id::standard(0x300), "diag"}
                                     .add(nc::signal_builder{}
                                            .name("page")
                                            .start_bit(0)
                                            .length(8)
                                            .mux_role(nc::multiplex_role::selector)
                                            .build())
                                     .add(nc::signal_builder{}
                                            .name("voltage")
                                            .start_bit(8)
                                            .length(8)
                                            .mux_role(nc::multiplex_role::multiplexed)
                                            .mux_value(0)
                                            .build())
                                     .build())
                      .build()};
  nc::registry const mux_reg{mux_db};
  auto const mux_frame{
    *nc::frame::classic(nc::can_id::standard(0x300), std::array{byte_of(0x00), byte_of(0x2A)})
  };
  std::println("8   multiplexed decode:");
  mux_reg.decode_signals(mux_frame, [](nc::signal const& sig, double const value) {
    std::println("      {} = {}", sig.name(), value);
  });

  // 9. Send and receive over an in-memory bus (swap for socketcan_bus on Linux).
  nc::loopback_bus bus;
  nexenne::utility::discard(bus.send(tx));
  if (auto const got{bus.receive()}; got && got->has_value()) {
    std::println("9   looped back: {}", **got);
  }

  // 10. J1939: decode a 29-bit id into priority, PGN, and addresses.
  if (auto const j1939{nc::j1939_id::decode(nc::can_id::extended(0x18FEF100))}) {
    std::println("10  {}", *j1939);
  }

  return 0;
}
