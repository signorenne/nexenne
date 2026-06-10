/**
 * @file
 * @brief Tests for the nexenne::utility bit helpers.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <vector>

#include <nexenne/utility/bit.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::reverse_bits<std::uint8_t>(0b1101'0000) == 0b0000'1011);
static_assert(util::test_bit<std::uint8_t>(0b0000'0100, 2));
static_assert(!util::test_bit<std::uint8_t>(0b0000'0100, 1));
static_assert(util::set_bit<std::uint8_t>(0, 3) == 0b0000'1000);
static_assert(util::clear_bit<std::uint8_t>(0b1111'1111, 0) == 0b1111'1110);
static_assert(util::toggle_bit<std::uint8_t>(0b0000'0001, 0) == 0);
static_assert(util::set_bits_mask<std::uint8_t>(2, 5) == 0b0011'1100);
static_assert(util::extract_bits<std::uint16_t>(0xABCD, 4, 8) == 0xBC);

// Boundary values: zero, all-ones, full-width branches, top bit.
static_assert(util::reverse_bits<std::uint64_t>(0) == 0);
static_assert(util::reverse_bits<std::uint64_t>(~std::uint64_t{0}) == ~std::uint64_t{0});
static_assert(util::reverse_bits<std::uint64_t>(1) == (std::uint64_t{1} << 63));
static_assert(
  util::reverse_bits<std::uint64_t>(util::reverse_bits<std::uint64_t>(0x0123456789ABCDEFULL))
  == 0x0123456789ABCDEFULL
);                                                               // round-trips
static_assert(util::set_bits_mask<std::uint8_t>(0, 7) == 0xFF);  // full-width all-ones branch
static_assert(util::set_bits_mask<std::uint64_t>(0, 63) == ~std::uint64_t{0});
static_assert(util::set_bits_mask<std::uint8_t>(3, 3) == 0b0000'1000);  // single bit
static_assert(util::extract_bits<std::uint8_t>(0xFF, 0, 8) == 0xFF);    // full-width branch
static_assert(util::set_bit<std::uint64_t>(0, 63) == (std::uint64_t{1} << 63));  // top bit
static_assert(util::test_bit<std::uint64_t>(std::uint64_t{1} << 63, 63));
static_assert(
  util::toggle_bit<std::uint8_t>(util::toggle_bit<std::uint8_t>(0b1010, 1), 1) == 0b1010
);  // toggle twice is identity
static_assert(
  util::pack_bits<std::uint16_t>(0xFF00, 0xFF, 0, 4) == 0xFF0F
);  // truncates src, preserves rest

TEST_CASE("nexenne::utility::pack_bits round-trips with extract_bits") {
  auto const packed{util::pack_bits<std::uint16_t>(0, 0xABCD, 4, 8)};
  CHECK(util::extract_bits<std::uint16_t>(packed, 4, 8) == 0xCD);
}

TEST_CASE("nexenne::utility::for_each_set_bit visits set bits low to high") {
  std::vector<std::size_t> seen;
  util::for_each_set_bit<std::uint8_t>(0b1010'0001, [&](std::size_t i) { seen.push_back(i); });
  CHECK(seen == std::vector<std::size_t>{0, 5, 7});
}

TEST_CASE("nexenne::utility::for_each_set_bit handles zero and the top bit") {
  std::vector<std::size_t> seen;
  util::for_each_set_bit<std::uint8_t>(0, [&](std::size_t i) { seen.push_back(i); });
  CHECK(seen.empty());  // no set bits: callback never runs

  util::for_each_set_bit<std::uint8_t>(0b1000'0000, [&](std::size_t i) { seen.push_back(i); });
  CHECK(seen == std::vector<std::size_t>{7});
}

}  // namespace
