/**
 * @file
 * @brief signal: packing physical values into a frame and reading them back.
 *
 * A nexenne::can::signal says where a value lives in the payload (start bit,
 * width, byte order, sign) and how its raw bits scale to a physical quantity.
 * The packing_plan compiles that layout once, and pack/unpack move values in and
 * out of a frame. This tour encodes a vehicle speed and an oil temperature into
 * one frame, then decodes them, and shows the Intel versus Motorola difference.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/codec.hpp>
#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/can/signal_builder.hpp>
#include <nexenne/utility/discard.hpp>

namespace {

namespace nc = nexenne::can;

}  // namespace

auto main() -> int {
  // Two signals sharing one 8-byte frame:
  //   speed: bits 0..15, Intel, 0.01 km/h per count.
  //   oil temperature: bits 16..23, Intel, 1 C per count with a -40 C offset.
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
  auto const speed_plan{nc::packing_plan::from_signal(speed)};
  auto const oil_plan{nc::packing_plan::from_signal(oil_temp)};

  std::array<std::byte, 8> zeros{};
  auto frame{*nc::frame::classic(nc::can_id::standard(0x100), zeros)};

  // Encode the physical values; the codec inverts the scaling and packs the bits.
  nexenne::utility::discard(nc::encode(speed, speed_plan, frame, 87.5));
  nexenne::utility::discard(nc::encode(oil_temp, oil_plan, frame, 90.0));
  std::println("encoded: {}", frame);

  // Decode them back out.
  std::println(
    "speed    = {} {}", nc::decode(speed, speed_plan, frame).value_or(0.0), speed.unit()
  );
  std::println(
    "oil_temp = {} {}", nc::decode(oil_temp, oil_plan, frame).value_or(0.0), oil_temp.unit()
  );

  // The same 16-bit value packed Intel and Motorola lands in opposite byte order.
  // Intel starts at the LSB (bit 0); Motorola starts at the MSB (bit 7 of byte 0).
  nc::signal const big{7, 16, nc::byte_order::big_endian, false};
  auto const big_plan{nc::packing_plan::from_signal(big)};
  std::array<std::byte, 2> intel_bytes{};
  std::array<std::byte, 2> motorola_bytes{};
  auto intel_frame{*nc::frame::classic(nc::can_id::standard(0x1), intel_bytes)};
  auto motorola_frame{*nc::frame::classic(nc::can_id::standard(0x1), motorola_bytes)};
  nc::signal const little{0, 16, nc::byte_order::little_endian, false};
  nexenne::utility::discard(
    nc::encode(little, nc::packing_plan::from_signal(little), intel_frame, 0x1234)
  );
  nexenne::utility::discard(nc::encode(big, big_plan, motorola_frame, 0x1234));
  std::println("0x1234 intel    -> {}", intel_frame);
  std::println("0x1234 motorola -> {}", motorola_frame);

  return 0;
}
