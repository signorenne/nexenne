/**
 * @file
 * @brief Tests for the compiled signal bit layout, the highest-risk bit math.
 *
 * Endianness is where CAN signal codecs go wrong, so these tests check Intel and
 * Motorola extraction against byte patterns computed by hand, plus round-trips
 * and the boundary cases (unaligned start, byte straddling, full width).
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/packing_plan.hpp>

namespace {

namespace nc = nexenne::can;

constexpr auto b(unsigned const v) noexcept -> std::byte {
  return std::byte{static_cast<unsigned char>(v)};
}

TEST_CASE("intel: a byte-aligned 16-bit field is little-endian") {
  // Intel start bit 0, width 16. Byte 0 is the least significant byte.
  nc::packing_plan const plan{0, 16, nc::byte_order::little_endian, false};
  std::array const payload{b(0x34), b(0x12)};
  CHECK(plan.extract(payload) == 0x1234);
  CHECK(plan.required_length() == 2);
  CHECK(plan.chunk_count() == 2);
}

TEST_CASE("motorola: a byte-aligned 16-bit field is big-endian") {
  // Motorola start bit 7 (MSB of byte 0), width 16. Byte 0 is the most
  // significant byte.
  nc::packing_plan const plan{7, 16, nc::byte_order::big_endian, false};
  std::array const payload{b(0x12), b(0x34)};
  CHECK(plan.extract(payload) == 0x1234);
  CHECK(plan.required_length() == 2);
}

TEST_CASE("intel: an unaligned 8-bit field straddles two bytes") {
  // Bits 4..11: high nibble of byte 0 plus low nibble of byte 1.
  nc::packing_plan const plan{4, 8, nc::byte_order::little_endian, false};
  std::array const payload{b(0x30), b(0x02)};  // byte0 bits 4..7 = 0x3, byte1 bits 0..3 = 0x2
  CHECK(plan.extract(payload) == 0x23);
  CHECK(plan.chunk_count() == 2);
}

TEST_CASE("motorola: a 12-bit field rounds down through the sawtooth") {
  // Motorola start bit 7, width 12: all of byte 0 then the top nibble of byte 1.
  nc::packing_plan const plan{7, 12, nc::byte_order::big_endian, false};
  std::array const payload{b(0xAB), b(0xC0)};
  CHECK(plan.extract(payload) == 0xABC);
}

TEST_CASE("a single bit is one chunk") {
  nc::packing_plan const plan{3, 1, nc::byte_order::little_endian, false};
  std::array const set{b(0x08)};  // bit 3 set
  std::array const clear{b(0x00)};
  CHECK(plan.extract(set) == 1);
  CHECK(plan.extract(clear) == 0);
  CHECK(plan.chunk_count() == 1);
}

TEST_CASE("insert is the inverse of extract and preserves neighbour bits") {
  nc::packing_plan const plan{4, 8, nc::byte_order::little_endian, false};
  std::array payload{b(0xFF), b(0xFF)};  // start with all ones around the field
  plan.insert(payload, 0x23);
  CHECK(plan.extract(payload) == 0x23);
  // The low nibble of byte 0 and the high nibble of byte 1 were not touched.
  CHECK((std::to_integer<unsigned>(payload[0]) & 0x0F) == 0x0F);
  CHECK((std::to_integer<unsigned>(payload[1]) & 0xF0) == 0xF0);
}

TEST_CASE("intel and motorola round-trip every value of a field width") {
  for (auto const order : {nc::byte_order::little_endian, nc::byte_order::big_endian}) {
    nc::packing_plan const plan{
      order == nc::byte_order::little_endian ? std::uint16_t{0} : std::uint16_t{7}, 10, order, false
    };
    for (std::uint64_t v{0}; v < 1024; ++v) {
      std::array payload{b(0), b(0)};
      plan.insert(payload, v);
      CHECK(plan.extract(payload) == v);
    }
  }
}

TEST_CASE("a full 64-bit intel field spans eight aligned bytes") {
  nc::packing_plan const plan{0, 64, nc::byte_order::little_endian, false};
  std::array const payload{b(0x01), b(0x23), b(0x45), b(0x67), b(0x89), b(0xAB), b(0xCD), b(0xEF)};
  CHECK(plan.extract(payload) == 0xEFCD'AB89'6745'2301);
  CHECK(plan.required_length() == 8);
  CHECK(plan.chunk_count() == 8);
}

TEST_CASE("a byte-aligned 64-bit motorola field spans eight bytes via the sawtooth") {
  // Start bit 7 (MSB of byte 0), width 64: byte 0 is the most significant byte.
  nc::packing_plan const plan{7, 64, nc::byte_order::big_endian, false};
  std::array const payload{b(0x01), b(0x23), b(0x45), b(0x67), b(0x89), b(0xAB), b(0xCD), b(0xEF)};
  CHECK(plan.extract(payload) == 0x0123'4567'89AB'CDEF);
  CHECK(plan.required_length() == 8);
  CHECK(plan.chunk_count() == 8);

  std::array round_trip{b(0), b(0), b(0), b(0), b(0), b(0), b(0), b(0)};
  plan.insert(round_trip, 0x0123'4567'89AB'CDEF);
  CHECK(plan.extract(round_trip) == 0x0123'4567'89AB'CDEF);
}

TEST_CASE("an unaligned 64-bit intel field genuinely spans nine bytes and round-trips") {
  // Start bit 1, width 64: 7 bits of byte 0, all of bytes 1..7, 1 bit of byte 8.
  // This is the nine-chunk path max_chunks{9} exists for.
  nc::packing_plan const plan{1, 64, nc::byte_order::little_endian, false};
  CHECK(plan.chunk_count() == 9);
  CHECK(plan.required_length() == 9);

  std::array<std::byte, 9> buffer{};
  constexpr std::uint64_t value{0xFEDC'BA98'7654'3210};
  plan.insert(buffer, value);
  CHECK(plan.extract(buffer) == value);
  // Bit 0 of byte 0 and the high 7 bits of byte 8 are outside the field: untouched.
  CHECK((std::to_integer<unsigned>(buffer[0]) & 0x01U) == 0U);
}

}  // namespace
