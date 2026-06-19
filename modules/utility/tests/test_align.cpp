/**
 * @file
 * @brief Tests for the nexenne::utility alignment helpers.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>

#include <nexenne/utility/align.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::align_up(std::size_t{200}, std::size_t{64}) == 256);
static_assert(util::align_up(std::size_t{256}, std::size_t{64}) == 256);  // already aligned
static_assert(util::align_down(std::size_t{200}, std::size_t{64}) == 192);
static_assert(util::is_aligned(std::size_t{256}, std::size_t{64}));
static_assert(!util::is_aligned(std::size_t{200}, std::size_t{64}));

// Zero is aligned to everything and rounds to itself.
static_assert(util::align_up(std::size_t{0}, std::size_t{64}) == 0);
static_assert(util::align_down(std::size_t{0}, std::size_t{64}) == 0);
static_assert(util::is_aligned(std::size_t{0}, std::size_t{64}));

// Alignment of one is the identity / always aligned.
static_assert(util::align_up(std::size_t{5}, std::size_t{1}) == 5);
static_assert(util::align_down(std::size_t{5}, std::size_t{1}) == 5);
static_assert(util::is_aligned(std::size_t{5}, std::size_t{1}));
static_assert(util::is_aligned(std::size_t{0}, std::size_t{1}));

// Rounding-up at the boundary: one past a multiple rounds to the next.
static_assert(util::align_up(std::size_t{65}, std::size_t{64}) == 128);
static_assert(util::align_up(std::size_t{1}, std::size_t{64}) == 64);
static_assert(util::align_down(std::size_t{63}, std::size_t{64}) == 0);  // value < alignment
static_assert(util::align_down(std::size_t{50}, std::size_t{64}) == 0);
static_assert(util::align_down(std::size_t{256}, std::size_t{64}) == 256);  // already aligned

// A large power-of-two alignment.
static_assert(util::align_up(std::size_t{1}, std::size_t{1} << 20) == (std::size_t{1} << 20));
static_assert(
  util::align_down(std::size_t{(1u << 20) + 7}, std::size_t{1} << 20) == (std::size_t{1} << 20)
);

// Smallest power-of-two alignments behave like a modulo classifier.
static_assert(util::align_up(std::size_t{3}, std::size_t{2}) == 4);
static_assert(util::align_down(std::size_t{3}, std::size_t{2}) == 2);
static_assert(util::is_aligned(std::size_t{4}, std::size_t{2}));
static_assert(!util::is_aligned(std::size_t{3}, std::size_t{2}));

// Narrow unsigned types: stay clear of the value+(alignment-1) overflow on the
// align_up path (its documented precondition), but exercise the wide range.
static_assert(util::align_up<std::uint8_t>(0x40, 0x80) == 0x80);  // rounds up to the top bit
static_assert(util::align_up<std::uint8_t>(0x80, 0x80) == 0x80);  // already aligned, no overflow
static_assert(util::align_up<std::uint8_t>(0x40, 0x40) == 0x40);  // already aligned
static_assert(util::align_down<std::uint8_t>(0xFF, 0x80) == 0x80);
static_assert(util::align_down<std::uint8_t>(0x7F, 0x80) == 0x00);
static_assert(util::is_aligned<std::uint8_t>(0x80, 0x80));
static_assert(!util::is_aligned<std::uint8_t>(0x40, 0x80));

// Other unsigned widths in constexpr.
static_assert(util::align_up<std::uint16_t>(0x1001, 0x1000) == 0x2000);
static_assert(util::align_down<std::uint32_t>(0xDEADBEEF, 0x10000) == 0xDEAD0000);
static_assert(util::align_up<std::uint64_t>(0x1, 0x8000000000000000ULL) == 0x8000000000000000ULL);

TEST_CASE("nexenne::utility::align_up / align_down on integrals") {
  CHECK(util::align_up(std::size_t{200}, std::size_t{64}) == 256);
  CHECK(util::align_up(std::size_t{256}, std::size_t{64}) == 256);  // already aligned
  CHECK(util::align_up(std::size_t{0}, std::size_t{64}) == 0);
  CHECK(util::align_up(std::size_t{1}, std::size_t{64}) == 64);
  CHECK(util::align_up(std::size_t{65}, std::size_t{64}) == 128);  // crosses the boundary

  CHECK(util::align_down(std::size_t{200}, std::size_t{64}) == 192);
  CHECK(util::align_down(std::size_t{256}, std::size_t{64}) == 256);  // already aligned
  CHECK(util::align_down(std::size_t{63}, std::size_t{64}) == 0);     // value < alignment
  CHECK(util::align_down(std::size_t{0}, std::size_t{64}) == 0);
}

TEST_CASE("nexenne::utility::align_up / align_down are mutually consistent") {
  // For every power-of-two alignment and many values, the down value is at most
  // the value, the up value is at least it, and both are aligned multiples.
  for (std::size_t k{0}; k < 12; ++k) {
    auto const a{std::size_t{1} << k};
    for (std::size_t v{0}; v < 300; ++v) {
      auto const down{util::align_down(v, a)};
      auto const up{util::align_up(v, a)};
      CHECK(down <= v);
      CHECK(up >= v);
      CHECK(util::is_aligned(down, a));
      CHECK(util::is_aligned(up, a));
      CHECK(down % a == 0);
      CHECK(up % a == 0);
      // up - down is 0 (aligned) or exactly one alignment step.
      CHECK((up == down || up == down + a));
      if (util::is_aligned(v, a)) {
        CHECK(up == v);
        CHECK(down == v);
      }
    }
  }
}

TEST_CASE("nexenne::utility::is_aligned classifies by modulo") {
  for (std::size_t k{0}; k < 8; ++k) {
    auto const a{std::size_t{1} << k};
    for (std::size_t v{0}; v < 256; ++v) {
      CHECK(util::is_aligned(v, a) == (v % a == 0));
    }
  }
}

TEST_CASE("nexenne::utility::align_up aligns a pointer") {
  alignas(64) std::byte arena[128]{};
  auto* const unaligned{static_cast<void*>(arena + 3)};

  auto* const up{util::align_up(unaligned, std::size_t{64})};
  CHECK(util::is_aligned(up, std::size_t{64}));

  auto* const down{util::align_down(unaligned, std::size_t{64})};
  CHECK(util::is_aligned(down, std::size_t{64}));
  CHECK(down == static_cast<void*>(arena));

  CHECK(up == static_cast<void*>(arena + 64));  // exact rounded-up address

  auto* const aligned{static_cast<void*>(arena)};
  CHECK(util::align_up(aligned, std::size_t{64}) == aligned);  // already aligned: unchanged
  CHECK_FALSE(util::is_aligned(static_cast<void*>(arena + 1), std::size_t{64}));
}

TEST_CASE("nexenne::utility pointer overloads sweep an arena") {
  alignas(64) std::byte arena[256]{};
  auto* const base{static_cast<void*>(arena)};

  for (std::size_t off{0}; off < 64; ++off) {
    auto* const p{static_cast<void*>(arena + off)};
    auto* const up{util::align_up(p, std::size_t{64})};
    auto* const down{util::align_down(p, std::size_t{64})};

    CHECK(util::is_aligned(up, std::size_t{64}));
    CHECK(util::is_aligned(down, std::size_t{64}));

    if (off == 0) {
      CHECK(up == base);
      CHECK(down == base);
    } else {
      CHECK(up == static_cast<void*>(arena + 64));
      CHECK(down == base);
    }
  }
}

TEST_CASE("nexenne::utility pointer overloads work on a typed pointer and void") {
  alignas(16) int data[8]{};
  int* const p{&data[0]};
  CHECK(util::is_aligned(p, std::size_t{16}));       // alignas guarantees this
  CHECK(util::align_down(p, std::size_t{16}) == p);  // already aligned
  CHECK(util::align_up(p, std::size_t{16}) == p);

  // Typed align_up returns a T* with the rounded address.
  int* const offset{reinterpret_cast<int*>(reinterpret_cast<std::byte*>(p) + 4)};
  CHECK_FALSE(util::is_aligned(offset, std::size_t{16}));
  auto* const rounded{util::align_up(offset, std::size_t{16})};
  CHECK(util::is_aligned(rounded, std::size_t{16}));

  void* const vp{static_cast<void*>(p)};
  CHECK(util::align_up(vp, std::size_t{16}) == vp);
}

TEST_CASE("nexenne::utility pointer is_aligned accepts const pointers") {
  alignas(32) std::byte arena[64]{};
  std::byte const* const cp{arena};
  CHECK(util::is_aligned(cp, std::size_t{32}));
  CHECK_FALSE(util::is_aligned(cp + 1, std::size_t{32}));
}

}  // namespace
