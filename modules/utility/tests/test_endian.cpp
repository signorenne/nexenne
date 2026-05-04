/**
 * @file
 * @brief Tests for the byte-order read/write helpers.
 */

#include <doctest/doctest.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include <nexenne/utility/endian.hpp>

namespace {

namespace util = nexenne::utility;

}  // namespace

// The helpers are constexpr, so they round-trip at compile time too.
static_assert([] {
  auto buf{std::array<std::byte, 2>{}};
  util::write_be(std::span{buf}, std::uint16_t{0x1234});
  return buf[0] == std::byte{0x12} && buf[1] == std::byte{0x34}
         && util::read_be<std::uint16_t>(std::span{buf}) == 0x1234U;
}());

// The little-endian pair is constexpr too, mirroring the big-endian check.
static_assert([] {
  auto buf{std::array<std::byte, 2>{}};
  util::write_le(std::span{buf}, std::uint16_t{0x1234});
  return buf[0] == std::byte{0x34} && buf[1] == std::byte{0x12}
         && util::read_le<std::uint16_t>(std::span{buf}) == 0x1234U;
}());

// Top-bit-set values survive at compile time in both orders at 64 bits, where a
// sign-extending shift bug would corrupt the high byte.
static_assert([] {
  auto buf{std::array<std::byte, 8>{}};
  util::write_be(std::span{buf}, std::uint64_t{0x8000'0000'0000'0001ULL});
  auto const be_ok{util::read_be<std::uint64_t>(std::span{buf}) == 0x8000'0000'0000'0001ULL};
  util::write_le(std::span{buf}, std::uint64_t{0x8000'0000'0000'0001ULL});
  auto const le_ok{util::read_le<std::uint64_t>(std::span{buf}) == 0x8000'0000'0000'0001ULL};
  return be_ok && le_ok;
}());

TEST_CASE("nexenne::utility big-endian writes the most significant byte first") {
  auto buf{std::array<std::byte, 4>{}};
  util::write_be(std::span{buf}, std::uint32_t{0x01020304U});
  CHECK(std::to_integer<int>(buf[0]) == 0x01);
  CHECK(std::to_integer<int>(buf[1]) == 0x02);
  CHECK(std::to_integer<int>(buf[2]) == 0x03);
  CHECK(std::to_integer<int>(buf[3]) == 0x04);
  CHECK(util::read_be<std::uint32_t>(std::span{buf}) == 0x01020304U);
}

TEST_CASE("nexenne::utility little-endian writes the least significant byte first") {
  auto buf{std::array<std::byte, 4>{}};
  util::write_le(std::span{buf}, std::uint32_t{0x01020304U});
  CHECK(std::to_integer<int>(buf[0]) == 0x04);
  CHECK(std::to_integer<int>(buf[1]) == 0x03);
  CHECK(std::to_integer<int>(buf[2]) == 0x02);
  CHECK(std::to_integer<int>(buf[3]) == 0x01);
  CHECK(util::read_le<std::uint32_t>(std::span{buf}) == 0x01020304U);
}

TEST_CASE("nexenne::utility byte order round-trips every width") {
  auto buf{std::array<std::byte, 8>{}};

  util::write_be(std::span{buf}.first<2>(), std::uint16_t{0xBEEFU});
  CHECK(util::read_be<std::uint16_t>(std::span{buf}.first<2>()) == 0xBEEFU);

  util::write_le(std::span{buf}.first<2>(), std::uint16_t{0xBEEFU});
  CHECK(util::read_le<std::uint16_t>(std::span{buf}.first<2>()) == 0xBEEFU);

  util::write_be(std::span{buf}, std::uint64_t{0x0123456789ABCDEFULL});
  CHECK(util::read_be<std::uint64_t>(std::span{buf}) == 0x0123456789ABCDEFULL);

  util::write_le(std::span{buf}, std::uint64_t{0x0123456789ABCDEFULL});
  CHECK(util::read_le<std::uint64_t>(std::span{buf}) == 0x0123456789ABCDEFULL);
}

TEST_CASE("nexenne::utility byte order round-trips top-bit-set values at every width") {
  // 0x80... patterns catch any accidental sign extension in the byte shifts.
  auto buf{std::array<std::byte, 8>{}};

  util::write_be(std::span{buf}.first<1>(), std::uint8_t{0x80U});
  CHECK(util::read_be<std::uint8_t>(std::span{buf}.first<1>()) == 0x80U);
  util::write_le(std::span{buf}.first<1>(), std::uint8_t{0x80U});
  CHECK(util::read_le<std::uint8_t>(std::span{buf}.first<1>()) == 0x80U);

  util::write_be(std::span{buf}.first<2>(), std::uint16_t{0x8001U});
  CHECK(util::read_be<std::uint16_t>(std::span{buf}.first<2>()) == 0x8001U);
  util::write_le(std::span{buf}.first<2>(), std::uint16_t{0x8001U});
  CHECK(util::read_le<std::uint16_t>(std::span{buf}.first<2>()) == 0x8001U);

  util::write_be(std::span{buf}.first<4>(), std::uint32_t{0x8000'0001U});
  CHECK(util::read_be<std::uint32_t>(std::span{buf}.first<4>()) == 0x8000'0001U);
  util::write_le(std::span{buf}.first<4>(), std::uint32_t{0x8000'0001U});
  CHECK(util::read_le<std::uint32_t>(std::span{buf}.first<4>()) == 0x8000'0001U);

  util::write_be(std::span{buf}, std::uint64_t{0x8000'0000'0000'0001ULL});
  CHECK(util::read_be<std::uint64_t>(std::span{buf}) == 0x8000'0000'0000'0001ULL);
  CHECK(std::to_integer<int>(buf[0]) == 0x80);  // the top bit lands in the first byte
  util::write_le(std::span{buf}, std::uint64_t{0x8000'0000'0000'0001ULL});
  CHECK(util::read_le<std::uint64_t>(std::span{buf}) == 0x8000'0000'0000'0001ULL);
  CHECK(std::to_integer<int>(buf[7]) == 0x80);  // and in the last byte for LE
}

TEST_CASE("nexenne::utility byte order round-trips all-ones values at every width") {
  auto buf{std::array<std::byte, 8>{}};

  util::write_be(std::span{buf}.first<1>(), std::uint8_t{0xFFU});
  CHECK(util::read_be<std::uint8_t>(std::span{buf}.first<1>()) == 0xFFU);
  util::write_le(std::span{buf}.first<1>(), std::uint8_t{0xFFU});
  CHECK(util::read_le<std::uint8_t>(std::span{buf}.first<1>()) == 0xFFU);

  util::write_be(std::span{buf}.first<2>(), std::uint16_t{0xFFFFU});
  CHECK(util::read_be<std::uint16_t>(std::span{buf}.first<2>()) == 0xFFFFU);
  util::write_le(std::span{buf}.first<2>(), std::uint16_t{0xFFFFU});
  CHECK(util::read_le<std::uint16_t>(std::span{buf}.first<2>()) == 0xFFFFU);

  util::write_be(std::span{buf}.first<4>(), std::uint32_t{0xFFFF'FFFFU});
  CHECK(util::read_be<std::uint32_t>(std::span{buf}.first<4>()) == 0xFFFF'FFFFU);
  util::write_le(std::span{buf}.first<4>(), std::uint32_t{0xFFFF'FFFFU});
  CHECK(util::read_le<std::uint32_t>(std::span{buf}.first<4>()) == 0xFFFF'FFFFU);

  util::write_be(std::span{buf}, ~std::uint64_t{0});
  CHECK(util::read_be<std::uint64_t>(std::span{buf}) == ~std::uint64_t{0});
  util::write_le(std::span{buf}, ~std::uint64_t{0});
  CHECK(util::read_le<std::uint64_t>(std::span{buf}) == ~std::uint64_t{0});
  for (auto const b : buf) {
    CHECK(std::to_integer<int>(b) == 0xFF);  // every byte is saturated either way
  }
}

TEST_CASE("nexenne::utility a single byte is order independent") {
  auto buf{std::array<std::byte, 1>{}};
  util::write_be(std::span{buf}, std::uint8_t{0x7FU});
  CHECK(util::read_be<std::uint8_t>(std::span{buf}) == 0x7FU);
  CHECK(util::read_le<std::uint8_t>(std::span{buf}) == 0x7FU);
}
