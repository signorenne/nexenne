/**
 * @file
 * @brief j1939: decoding and building SAE J1939 identifiers.
 *
 * J1939 gives a 29-bit CAN identifier a fixed meaning: a priority, a Parameter
 * Group Number naming the message, and source and destination addresses. This
 * tour decodes two real messages, a broadcast and a destination-specific one, and
 * rebuilds an identifier from its components.
 */

#include <print>

#include <nexenne/can/format.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/j1939_id.hpp>

namespace {

namespace nc = nexenne::can;

}  // namespace

auto main() -> int {
  // CCVS (Cruise Control / Vehicle Speed), PGN 0xFEF1: a PDU2 broadcast message.
  if (auto const ccvs{nc::j1939_id::decode(nc::can_id::extended(0x18FEF100))}) {
    std::println("CCVS  {}", *ccvs);
    std::println(
      "  priority {}, pgn 0x{:X}, source 0x{:02X}, broadcast {}",
      ccvs->priority(),
      ccvs->pgn(),
      ccvs->source_address(),
      ccvs->is_broadcast()
    );
  }

  // A Request (PGN 0xEA00): a PDU1 message addressed to one node.
  if (auto const request{nc::j1939_id::decode(nc::can_id::extended(0x18EA2101))}) {
    std::println("Request {}", *request);
    std::println(
      "  pdu1 {}, destination 0x{:02X}, source 0x{:02X}",
      request->is_pdu1(),
      request->destination_address(),
      request->source_address()
    );
  }

  // Build an identifier from components: priority 6, PGN 0xFEF1, source 0x10.
  auto const built{nc::j1939_id::make(6, 0xFEF1, 0x10)};
  std::println("built   {} -> {}", built, built.identifier());

  return 0;
}
