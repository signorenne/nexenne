/**
 * @file
 * @brief Tests for the J1939 transport protocol (BAM segmentation and reassembly).
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include <nexenne/can/format.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/j1939_transport.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

TEST_CASE("transport: BAM segments a payload into TP.CM plus TP.DT frames") {
  std::vector<std::byte> payload;
  for (unsigned i{0}; i < 16; ++i) {
    payload.push_back(b(i));
  }
  auto const frames{nc::segment_bam(7, 0xFEF1, 0x20, payload)};
  REQUIRE(frames.has_value());
  // 16 bytes -> 3 packets, plus the announce frame.
  REQUIRE(frames->size() == 4);
  // First frame is TP.CM with the BAM control byte and packet count.
  CHECK((*frames)[0].data()[0] == b(nc::j1939_tp_bam));
  CHECK((*frames)[0].data()[3] == b(3));
  // Data frames are sequenced from 1.
  CHECK((*frames)[1].data()[0] == b(1));
  CHECK((*frames)[3].data()[0] == b(3));
}

TEST_CASE("transport: segment then reassemble round-trips the payload") {
  std::vector<std::byte> payload;
  for (unsigned i{0}; i < 20; ++i) {
    payload.push_back(b(0xA0 + i));
  }
  auto const frames{*nc::segment_bam(7, 0xFECA, 0x11, payload)};

  nc::transport_reassembler reassembler;
  std::optional<nc::transport_message> message;
  for (nc::frame const& f : frames) {
    auto const got{reassembler.accept(f)};
    REQUIRE(got.has_value());
    if (got->has_value()) {
      message = *got;
    }
  }

  REQUIRE(message.has_value());
  CHECK(message->pgn() == 0xFECA);
  CHECK(message->source() == 0x11);
  REQUIRE(message->size() == 20);
  CHECK(message->data()[0] == b(0xA0));
  CHECK(message->data()[19] == b(0xA0 + 19));
}

TEST_CASE("transport: an out-of-sequence data packet drops the session") {
  std::vector<std::byte> payload;
  for (unsigned i{0}; i < 20; ++i) {  // 20 bytes spans three data packets
    payload.push_back(b(0xA0 + i));
  }
  auto const frames{*nc::segment_bam(7, 0xFECA, 0x11, payload)};
  REQUIRE(frames.size() == 4);  // one TP.CM plus three TP.DT

  nc::transport_reassembler reassembler;
  for (std::size_t i : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {  // skip packet two
    auto const got{reassembler.accept(frames[i])};
    REQUIRE(got.has_value());
    CHECK_FALSE(got->has_value());  // the gap drops the session, nothing completes
  }
}

TEST_CASE("transport: a non-transport frame yields nothing") {
  nc::transport_reassembler reassembler;
  auto const f{*nc::frame::classic(nc::can_id::standard(0x100), std::array{b(0x01)})};
  auto const got{reassembler.accept(f)};
  REQUIRE(got.has_value());
  CHECK_FALSE(got->has_value());
}

TEST_CASE("transport: a payload over 1785 bytes is rejected") {
  std::vector<std::byte> big(1786);
  CHECK(nc::segment_bam(7, 0xFEF1, 0x00, big).error() == nc::can_error::payload_too_large);
}

// Builds a raw TP.CM announce frame with explicit fields (for hostile-input tests).
auto make_cm(
  std::uint8_t const source,
  std::uint8_t const destination,
  std::uint8_t const control,
  std::uint16_t const size,
  std::uint8_t const packets,
  std::uint32_t const pgn
) -> nc::frame {
  auto const id{nc::j1939_id::make(7, nc::j1939_pgn_tp_cm, source, destination).identifier()};
  std::array const data{
    b(control),
    b(size & 0xFFU),
    b((size >> 8U) & 0xFFU),
    b(packets),
    b(0xFF),
    b(pgn & 0xFFU),
    b((pgn >> 8U) & 0xFFU),
    b((pgn >> 16U) & 0xFFU)
  };
  return *nc::frame::classic(id, data);
}

TEST_CASE("transport: an inconsistent announce opens no session and yields no spurious message") {
  nc::transport_reassembler reassembler;
  // size 0 / packets 0: the announce is rejected, so no session exists and a
  // following data frame completes nothing (regression: it used to complete an
  // empty message).
  auto const cm_zero{make_cm(0x30, 0xFF, nc::j1939_tp_bam, 0, 0, 0xFEF1)};
  REQUIRE(reassembler.accept(cm_zero).has_value());
  auto const id{nc::j1939_id::make(7, nc::j1939_pgn_tp_dt, 0x30, 0xFF).identifier()};
  std::array const dt{b(1), b(1), b(2), b(3), b(4), b(5), b(6), b(7)};
  auto const got{reassembler.accept(*nc::frame::classic(id, dt))};
  REQUIRE(got.has_value());
  CHECK_FALSE(got->has_value());

  // size 1785 announced with only 1 packet (should be 255): rejected.
  auto const cm_bad{make_cm(0x31, 0xFF, nc::j1939_tp_bam, 1785, 1, 0xFEF1)};
  REQUIRE(reassembler.accept(cm_bad).has_value());
  CHECK_FALSE(reassembler.accept(cm_bad)->has_value());
}

TEST_CASE("transport: a data frame contributes only its seven bytes, never overrunning the size") {
  // Announce 9 bytes across 2 packets, then send the first TP.DT as an oversized
  // (mis-tagged FD) frame. It must add exactly 7 bytes, so the completed message
  // is the announced 9 bytes, not the full frame length.
  nc::transport_reassembler reassembler;
  auto const cm{make_cm(0x40, 0xFF, nc::j1939_tp_bam, 9, 2, 0xFEF1)};
  REQUIRE(reassembler.accept(cm).has_value());

  auto const dt_id{nc::j1939_id::make(7, nc::j1939_pgn_tp_dt, 0x40, 0xFF).identifier()};
  std::array<std::byte, 32> oversized{};
  oversized[0] = b(1);  // sequence
  for (std::size_t i{1}; i < oversized.size(); ++i) {
    oversized[i] = b(0xC8);
  }
  REQUIRE_FALSE(reassembler.accept(*nc::frame::fd(dt_id, oversized))->has_value());

  std::array const dt2{b(2), b(0x01), b(0x02), b(0x03), b(0x04), b(0x05), b(0x06), b(0x07)};
  auto const done{reassembler.accept(*nc::frame::classic(dt_id, dt2))};
  REQUIRE(done.has_value());
  REQUIRE(done->has_value());
  CHECK((*done)->size() == 9);           // never the 32-byte frame length
  CHECK((*done)->data()[7] == b(0x01));  // byte 8 is from packet 2, not the FD frame
}

TEST_CASE("transport: a transport_message is formattable") {
  nc::transport_message const msg{0xFECA, 0x11, 0xFF, {}};
  CHECK(nc::to_string(msg).starts_with("transport(pgn=0x0FECA"));
}

}  // namespace
