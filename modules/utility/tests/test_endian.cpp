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

TEST_CASE("nexenne::utility a single byte is order independent") {
  auto buf{std::array<std::byte, 1>{}};
  util::write_be(std::span{buf}, std::uint8_t{0x7FU});
  CHECK(util::read_be<std::uint8_t>(std::span{buf}) == 0x7FU);
  CHECK(util::read_le<std::uint8_t>(std::span{buf}) == 0x7FU);
}
