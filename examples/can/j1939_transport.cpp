/**
 * @file
 * @brief j1939_transport: broadcasting and reassembling a multi-packet message.
 *
 * A J1939 parameter group larger than 8 bytes travels via the transport protocol:
 * one TP.CM announce frame then several TP.DT data frames. This example segments a
 * 20-byte payload into BAM frames, then feeds those frames back through a
 * reassembler to recover the original message, exactly as a receiving node would.
 */

#include <array>
#include <cstddef>
#include <print>

#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/j1939_transport.hpp>

namespace {

namespace nc = nexenne::can;

}  // namespace

auto main() -> int {
  // A 20-byte payload: too large for one frame, so it needs the transport protocol.
  std::array<std::byte, 20> payload{};
  for (std::size_t i{0}; i < payload.size(); ++i) {
    payload[i] = std::byte{static_cast<unsigned char>(0x10 + i)};
  }

  // Segment it for broadcast (BAM): priority 7, PGN 0xFECA, source address 0x11.
  auto const frames{nc::segment_bam(7, 0xFECA, 0x11, payload)};
  if (!frames) {
    std::println("segmentation failed: {}", nc::to_string(frames.error()));
    return 1;
  }
  std::println("broadcast {} bytes as {} frames:", payload.size(), frames->size());
  for (nc::frame const& f : *frames) {
    std::println("  {}", f);
  }

  // A receiver feeds each frame to a reassembler until the message completes.
  nc::transport_reassembler reassembler;
  for (nc::frame const& f : *frames) {
    auto const message{reassembler.accept(f)};
    if (message && message->has_value()) {
      auto const& assembled{**message};
      std::println(
        "reassembled PGN 0x{:X} from 0x{:02X}: {} bytes",
        assembled.pgn(),
        assembled.source(),
        assembled.size()
      );
    }
  }

  return 0;
}
