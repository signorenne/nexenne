/**
 * @file
 * @brief Tests for the J1939 view of a 29-bit identifier.
 */

#include <doctest/doctest.h>

#include <format>

#include <nexenne/can/format.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/j1939_id.hpp>

namespace {

namespace nc = nexenne::can;

TEST_CASE("j1939_id: decode requires an extended identifier") {
  CHECK_FALSE(nc::j1939_id::decode(nc::can_id::standard(0x100)).has_value());
  CHECK(nc::j1939_id::decode(nc::can_id::extended(0x18FEF100)).has_value());
}

TEST_CASE("j1939_id: a PDU2 broadcast message decodes (CCVS, PGN 0xFEF1)") {
  // 0x18FEF100: priority 6, PF 0xFE (>=240, PDU2), PS 0xF1, source 0x00.
  auto const id{*nc::j1939_id::decode(nc::can_id::extended(0x18FEF100))};
  CHECK(id.priority() == 6);
  CHECK(id.pdu_format() == 0xFE);
  CHECK(id.pdu_specific() == 0xF1);
  CHECK(id.source_address() == 0x00);
  CHECK(id.is_pdu2());
  CHECK(id.pgn() == 0xFEF1);
  CHECK(id.destination_address() == nc::j1939_global_address);
  CHECK(id.is_broadcast());
}

TEST_CASE("j1939_id: a PDU1 destination-specific message decodes (Request, PGN 0xEA00)") {
  // 0x18EA2101: priority 6, PF 0xEA (<240, PDU1), destination 0x21, source 0x01.
  auto const id{*nc::j1939_id::decode(nc::can_id::extended(0x18EA2101))};
  CHECK(id.priority() == 6);
  CHECK(id.pdu_format() == 0xEA);
  CHECK(id.is_pdu1());
  CHECK(id.pgn() == 0xEA00);  // the PDU specific byte is not part of a PDU1 PGN
  CHECK(id.destination_address() == 0x21);
  CHECK(id.source_address() == 0x01);
  CHECK_FALSE(id.is_broadcast());
}

TEST_CASE("j1939_id: make rebuilds the identifier from components") {
  // PDU2: destination is ignored, group extension comes from the PGN.
  CHECK(nc::j1939_id::make(6, 0xFEF1, 0x00).identifier() == nc::can_id::extended(0x18FEF100));
  // PDU1: the destination becomes the PDU specific byte.
  CHECK(nc::j1939_id::make(6, 0xEA00, 0x01, 0x21).identifier() == nc::can_id::extended(0x18EA2101));
}

TEST_CASE("j1939_id: decode and make round-trip") {
  auto const original{nc::can_id::extended(0x0CF00400)};  // EEC1, priority 3, PGN 0xF004
  auto const id{*nc::j1939_id::decode(original)};
  auto const rebuilt{
    nc::j1939_id::make(id.priority(), id.pgn(), id.source_address(), id.destination_address())
  };
  CHECK(rebuilt.identifier() == original);
}

TEST_CASE("j1939_id: format shows the priority, PGN, and addresses") {
  auto const id{*nc::j1939_id::decode(nc::can_id::extended(0x18FEF100))};
  CHECK(std::format("{}", id) == "j1939(prio=6, pgn=0x0FEF1, sa=0x00, da=0xFF)");
}

}  // namespace
