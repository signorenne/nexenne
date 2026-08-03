/**
 * @file
 * @brief fields: payload fill, not-available, text fields, and multiplexing.
 *
 * Beyond plain scaled signals, real buses use a few more idioms. This example
 * shows them: filling a frame with 0xFF before packing (the J1939 "unused = not
 * available" convention), the per-signal not-available sentinel, byte and text
 * fields for raw blobs and strings, and a multiplexed message whose selector
 * chooses which signals are present.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/byte_field.hpp>
#include <nexenne/can/codec.hpp>
#include <nexenne/can/database_builder.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
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
  // Fill and not-available: start the frame as all-0xFF (J1939 unused bytes),
  // then pack one 8-bit signal.
  auto const temperature{
    nc::signal_builder{}.name("temp").start_bit(0).length(8).scale(1.0).offset(-40.0).build()
  };
  auto frame{*nc::frame::filled(nc::can_id::standard(0x100), 8, byte_of(0xFF))};
  auto const plan{nc::packing_plan::from_signal(temperature)};
  nexenne::utility::discard(nc::encode(temperature, plan, frame, 25.0));
  std::println("filled frame: {}", frame);

  // The other bytes are still 0xFF; a second 8-bit field there reads as the
  // all-ones "not available" sentinel.
  nc::packing_plan const other{8, 8, nc::byte_order::little_endian, false};
  std::println("byte 1 not available: {}", nc::is_not_available(other, frame).value_or(false));

  // Text and byte fields.
  std::array<std::byte, 8> blank{};
  auto text_frame{*nc::frame::classic(nc::can_id::standard(0x200), blank)};
  nc::byte_field const tag{0, 8, "tag"};
  nexenne::utility::discard(nc::write_text(tag, text_frame, "HELLO"));
  std::println("text field: '{}'", nc::read_text(tag, text_frame).value_or(""));

  // Multiplexed message.
  // Byte 0 selects the group; group 0 puts a signal in byte 1, group 1 in byte 2.
  auto const db{nc::database_builder{}
                  .add_message(
                    nc::message_builder{nc::can_id::standard(0x300), "diagnostics"}
                      .add(
                        nc::signal_builder{}
                          .name("page")
                          .start_bit(0)
                          .length(8)
                          .mux_role(nc::multiplex_role::selector)
                          .build()
                      )
                      .add(
                        nc::signal_builder{}
                          .name("voltage")
                          .start_bit(8)
                          .length(8)
                          .mux_role(nc::multiplex_role::multiplexed)
                          .mux_value(0)
                          .build()
                      )
                      .add(
                        nc::signal_builder{}
                          .name("current")
                          .start_bit(16)
                          .length(8)
                          .mux_role(nc::multiplex_role::multiplexed)
                          .mux_value(1)
                          .build()
                      )
                      .build()
                  )
                  .build()};
  nc::registry const reg{db};

  std::array const page1{byte_of(0x01), byte_of(0x00), byte_of(0x2A)};  // selector = 1
  auto const received{*nc::frame::classic(nc::can_id::standard(0x300), page1)};
  std::println("multiplexed (page 1) decodes:");
  reg.decode_signals(received, [](nc::signal const& sig, double const value) {
    std::println("  {} = {}", sig.name(), value);
  });

  return 0;
}
