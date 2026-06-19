/**
 * @file
 * @brief Tests for the nexenne::utility bit helpers.
 */

#include <doctest/doctest.h>

#include <bit>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <nexenne/utility/bit.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::reverse_bits<std::uint8_t>(0b1101'0000) == 0b0000'1011);

// Zero and all-ones are fixed points for every width.
static_assert(util::reverse_bits<std::uint8_t>(0) == 0);
static_assert(util::reverse_bits<std::uint16_t>(0) == 0);
static_assert(util::reverse_bits<std::uint32_t>(0) == 0);
static_assert(util::reverse_bits<std::uint64_t>(0) == 0);
static_assert(util::reverse_bits<std::uint8_t>(0xFF) == 0xFF);
static_assert(util::reverse_bits<std::uint16_t>(0xFFFF) == 0xFFFF);
static_assert(util::reverse_bits<std::uint32_t>(0xFFFFFFFFU) == 0xFFFFFFFFU);
static_assert(util::reverse_bits<std::uint64_t>(~std::uint64_t{0}) == ~std::uint64_t{0});

// A single low bit reverses to the single top bit, for every width.
static_assert(util::reverse_bits<std::uint8_t>(1) == (std::uint8_t{1} << 7));
static_assert(util::reverse_bits<std::uint16_t>(1) == (std::uint16_t{1} << 15));
static_assert(util::reverse_bits<std::uint32_t>(1) == (std::uint32_t{1} << 31));
static_assert(util::reverse_bits<std::uint64_t>(1) == (std::uint64_t{1} << 63));

// Alternating patterns swap under reversal (even/odd width => 0x55<->0xAA).
static_assert(util::reverse_bits<std::uint8_t>(0x55) == 0xAA);
static_assert(util::reverse_bits<std::uint8_t>(0xAA) == 0x55);
static_assert(util::reverse_bits<std::uint16_t>(0x5555) == 0xAAAA);
static_assert(util::reverse_bits<std::uint32_t>(0x55555555U) == 0xAAAAAAAAU);

// reverse_bits is its own inverse (involution) over every width.
static_assert(
  util::reverse_bits<std::uint64_t>(util::reverse_bits<std::uint64_t>(0x0123456789ABCDEFULL))
  == 0x0123456789ABCDEFULL
);
static_assert(
  util::reverse_bits<std::uint16_t>(util::reverse_bits<std::uint16_t>(0xABCD)) == 0xABCD
);

static_assert(util::test_bit<std::uint8_t>(0b0000'0100, 2));
static_assert(!util::test_bit<std::uint8_t>(0b0000'0100, 1));
static_assert(util::test_bit<std::uint64_t>(std::uint64_t{1} << 63, 63));
static_assert(!util::test_bit<std::uint64_t>(std::uint64_t{1} << 63, 62));

static_assert(util::set_bit<std::uint8_t>(0, 3) == 0b0000'1000);
static_assert(util::set_bit<std::uint64_t>(0, 63) == (std::uint64_t{1} << 63));
static_assert(util::set_bit<std::uint8_t>(0xFF, 3) == 0xFF);  // already set: idempotent

static_assert(util::clear_bit<std::uint8_t>(0b1111'1111, 0) == 0b1111'1110);
static_assert(util::clear_bit<std::uint8_t>(0, 0) == 0);  // already clear: idempotent
static_assert(util::clear_bit<std::uint64_t>(~std::uint64_t{0}, 63) == (~std::uint64_t{0} >> 1));

static_assert(util::toggle_bit<std::uint8_t>(0b0000'0001, 0) == 0);
static_assert(util::toggle_bit<std::uint8_t>(0, 0) == 1);
static_assert(
  util::toggle_bit<std::uint8_t>(util::toggle_bit<std::uint8_t>(0b1010, 1), 1) == 0b1010
);  // toggle twice is identity

static_assert(util::set_bits_mask<std::uint8_t>(2, 5) == 0b0011'1100);
static_assert(util::set_bits_mask<std::uint8_t>(0, 7) == 0xFF);  // full-width all-ones branch
static_assert(util::set_bits_mask<std::uint8_t>(3, 3) == 0b0000'1000);  // single bit
static_assert(util::set_bits_mask<std::uint8_t>(0, 0) == 0b0000'0001);  // low single bit
static_assert(util::set_bits_mask<std::uint8_t>(7, 7) == 0b1000'0000);  // top single bit
static_assert(util::set_bits_mask<std::uint16_t>(0, 15) == 0xFFFF);
static_assert(util::set_bits_mask<std::uint32_t>(0, 31) == 0xFFFFFFFFU);
static_assert(util::set_bits_mask<std::uint64_t>(0, 63) == ~std::uint64_t{0});
static_assert(util::set_bits_mask<std::uint64_t>(32, 63) == (~std::uint64_t{0} << 32));

static_assert(util::extract_bits<std::uint16_t>(0xABCD, 4, 8) == 0xBC);
static_assert(util::extract_bits<std::uint8_t>(0xFF, 0, 8) == 0xFF);  // full-width branch
static_assert(util::extract_bits<std::uint8_t>(0xFF, 0, 1) == 1);     // single low bit
static_assert(util::extract_bits<std::uint8_t>(0x80, 7, 1) == 1);     // single top bit
static_assert(util::extract_bits<std::uint64_t>(~std::uint64_t{0}, 0, 64) == ~std::uint64_t{0});
static_assert(util::extract_bits<std::uint16_t>(0xF0F0, 4, 4) == 0xF);

static_assert(util::pack_bits<std::uint16_t>(0xFF00, 0xFF, 0, 4) == 0xFF0F);  // truncates src
static_assert(util::pack_bits<std::uint8_t>(0, 0xFF, 0, 8) == 0xFF);          // full width
static_assert(util::pack_bits<std::uint8_t>(0xFF, 0, 0, 8) == 0);             // clears full width
static_assert(
  util::extract_bits<std::uint16_t>(util::pack_bits<std::uint16_t>(0, 0xABCD, 4, 8), 4, 8) == 0xCD
);  // pack/extract round-trip in constexpr

TEST_CASE("nexenne::utility::reverse_bits matches a reference per-bit reversal") {
  auto const reference{[](std::uint32_t v) {
    std::uint32_t out{0};
    for (std::size_t i{0}; i < 32; ++i) {
      if (((v >> i) & 1U) != 0U) {
        out |= std::uint32_t{1} << (31 - i);
      }
    }
    return out;
  }};

  for (auto const v :
       {std::uint32_t{0},
        std::uint32_t{1},
        std::uint32_t{0xDEADBEEF},
        std::uint32_t{0x55555555},
        std::uint32_t{0xAAAAAAAA},
        std::uint32_t{0xFFFFFFFF},
        std::uint32_t{0x80000000}}) {
    CHECK(util::reverse_bits<std::uint32_t>(v) == reference(v));
    CHECK(util::reverse_bits<std::uint32_t>(util::reverse_bits<std::uint32_t>(v)) == v);
  }
}

TEST_CASE("nexenne::utility::reverse_bits widths and boundaries at runtime") {
  CHECK(util::reverse_bits<std::uint8_t>(0b1101'0000) == 0b0000'1011);
  CHECK(util::reverse_bits<std::uint8_t>(0xFF) == 0xFF);
  CHECK(util::reverse_bits<std::uint8_t>(0) == 0);
  CHECK(util::reverse_bits<std::uint8_t>(0x55) == 0xAA);

  CHECK(util::reverse_bits<std::uint16_t>(0x0001) == 0x8000);
  CHECK(util::reverse_bits<std::uint16_t>(0x8000) == 0x0001);
  CHECK(util::reverse_bits<std::uint16_t>(0xFFFE) == 0x7FFF);  // max-1 pattern

  CHECK(util::reverse_bits<std::uint64_t>(1) == (std::uint64_t{1} << 63));
  CHECK(util::reverse_bits<std::uint64_t>(~std::uint64_t{0}) == ~std::uint64_t{0});
}

TEST_CASE("nexenne::utility single-bit ops touch only the named bit") {
  for (std::size_t i{0}; i < 8; ++i) {
    auto const set{util::set_bit<std::uint8_t>(0, i)};
    CHECK(util::test_bit<std::uint8_t>(set, i));
    CHECK(std::popcount(set) == 1);

    auto const cleared{util::clear_bit<std::uint8_t>(0xFF, i)};
    CHECK_FALSE(util::test_bit<std::uint8_t>(cleared, i));
    CHECK(std::popcount(static_cast<unsigned>(cleared)) == 7);

    auto const toggled{util::toggle_bit<std::uint8_t>(0, i)};
    CHECK(util::test_bit<std::uint8_t>(toggled, i));
    CHECK(util::toggle_bit<std::uint8_t>(toggled, i) == 0);  // toggle twice restores
  }
}

TEST_CASE("nexenne::utility set/clear are idempotent and inverse") {
  auto const x{std::uint16_t{0b1010'0101'1100'0011}};
  for (std::size_t i{0}; i < 16; ++i) {
    auto const s{util::set_bit<std::uint16_t>(x, i)};
    CHECK(util::set_bit<std::uint16_t>(s, i) == s);  // idempotent
    CHECK(util::test_bit<std::uint16_t>(s, i));

    auto const c{util::clear_bit<std::uint16_t>(x, i)};
    CHECK(util::clear_bit<std::uint16_t>(c, i) == c);  // idempotent
    CHECK_FALSE(util::test_bit<std::uint16_t>(c, i));

    // Setting then clearing leaves the surrounding bits untouched.
    CHECK(
      util::clear_bit<std::uint16_t>(util::set_bit<std::uint16_t>(x, i), i)
      == util::clear_bit<std::uint16_t>(x, i)
    );
  }
}

TEST_CASE("nexenne::utility::set_bits_mask covers ranges and boundaries") {
  CHECK(util::set_bits_mask<std::uint8_t>(2, 5) == 0b0011'1100);
  CHECK(util::set_bits_mask<std::uint8_t>(0, 0) == 0b0000'0001);
  CHECK(util::set_bits_mask<std::uint8_t>(7, 7) == 0b1000'0000);
  CHECK(util::set_bits_mask<std::uint8_t>(0, 7) == 0xFF);  // full-width all-ones branch
  CHECK(util::set_bits_mask<std::uint32_t>(0, 31) == 0xFFFFFFFFU);
  CHECK(util::set_bits_mask<std::uint64_t>(0, 63) == ~std::uint64_t{0});
  CHECK(util::set_bits_mask<std::uint64_t>(32, 63) == (~std::uint64_t{0} << 32));

  // Popcount of any single-range mask equals the inclusive span.
  for (std::size_t lo{0}; lo < 8; ++lo) {
    for (std::size_t hi{lo}; hi < 8; ++hi) {
      auto const mask{util::set_bits_mask<std::uint8_t>(lo, hi)};
      CHECK(std::popcount(static_cast<unsigned>(mask)) == static_cast<int>(hi - lo + 1));
    }
  }
}

TEST_CASE("nexenne::utility::extract_bits reads right-aligned fields") {
  CHECK(util::extract_bits<std::uint16_t>(0xABCD, 4, 8) == 0xBC);
  CHECK(util::extract_bits<std::uint16_t>(0xABCD, 0, 4) == 0xD);
  CHECK(util::extract_bits<std::uint16_t>(0xABCD, 12, 4) == 0xA);  // top nibble
  CHECK(util::extract_bits<std::uint8_t>(0xFF, 0, 8) == 0xFF);     // full-width branch
  CHECK(util::extract_bits<std::uint8_t>(0x80, 7, 1) == 1);        // single top bit
  CHECK(util::extract_bits<std::uint64_t>(~std::uint64_t{0}, 0, 64) == ~std::uint64_t{0});
  CHECK(util::extract_bits<std::uint64_t>(~std::uint64_t{0}, 32, 32) == 0xFFFFFFFFU);
}

TEST_CASE("nexenne::utility::pack_bits replaces a field and preserves the rest") {
  CHECK(util::pack_bits<std::uint16_t>(0xFF00, 0xFF, 0, 4) == 0xFF0F);  // truncates src to width
  CHECK(util::pack_bits<std::uint8_t>(0, 0xFF, 0, 8) == 0xFF);          // full-width write
  CHECK(util::pack_bits<std::uint8_t>(0xFF, 0, 0, 8) == 0);             // clears the whole word
  CHECK(util::pack_bits<std::uint8_t>(0b1111'0000, 0b101, 0, 4) == 0b1111'0101);

  // Writing then reading the same field round-trips, and bits outside survive.
  auto const dest{std::uint16_t{0xF00F}};
  auto const packed{util::pack_bits<std::uint16_t>(dest, 0xABCD, 4, 8)};
  CHECK(util::extract_bits<std::uint16_t>(packed, 4, 8) == 0xCD);  // low 8 bits of src
  CHECK(util::extract_bits<std::uint16_t>(packed, 0, 4) == 0xF);   // untouched low nibble
  CHECK(util::extract_bits<std::uint16_t>(packed, 12, 4) == 0xF);  // untouched top nibble
}

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

TEST_CASE("nexenne::utility::for_each_set_bit invocation count equals popcount") {
  for (auto const v :
       {std::uint16_t{0},
        std::uint16_t{1},
        std::uint16_t{0xFFFF},
        std::uint16_t{0x5555},
        std::uint16_t{0xAAAA},
        std::uint16_t{0x8001}}) {
    std::size_t count{0};
    util::for_each_set_bit<std::uint16_t>(v, [&](std::size_t) { ++count; });
    CHECK(count == static_cast<std::size_t>(std::popcount(v)));
  }
}

TEST_CASE("nexenne::utility::for_each_set_bit on all-ones visits every index in order") {
  std::vector<std::size_t> seen;
  util::for_each_set_bit<std::uint8_t>(0xFF, [&](std::size_t i) { seen.push_back(i); });
  CHECK(seen == std::vector<std::size_t>{0, 1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("nexenne::utility::for_each_set_bit reaches the 64-bit top bit") {
  std::vector<std::size_t> seen;
  util::for_each_set_bit<std::uint64_t>(std::uint64_t{1} << 63, [&](std::size_t i) {
    seen.push_back(i);
  });
  CHECK(seen == std::vector<std::size_t>{63});
}

}  // namespace
