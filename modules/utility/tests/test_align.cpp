/**
 * @file
 * @brief Tests for the nexenne::utility alignment helpers.
 */

#include <doctest/doctest.h>

#include <cstddef>

#include <nexenne/utility/align.hpp>

namespace {

namespace util = nexenne::utility;

static_assert(util::align_up(std::size_t{200}, std::size_t{64}) == 256);
static_assert(util::align_up(std::size_t{256}, std::size_t{64}) == 256);
static_assert(util::align_down(std::size_t{200}, std::size_t{64}) == 192);
static_assert(util::is_aligned(std::size_t{256}, std::size_t{64}));
static_assert(!util::is_aligned(std::size_t{200}, std::size_t{64}));

static_assert(util::align_up(std::size_t{0}, std::size_t{64}) == 0);  // zero
static_assert(util::is_aligned(std::size_t{0}, std::size_t{64}));
static_assert(util::align_up(std::size_t{5}, std::size_t{1}) == 5);  // alignment 1: identity
static_assert(util::is_aligned(std::size_t{5}, std::size_t{1}));
static_assert(util::align_down(std::size_t{50}, std::size_t{64}) == 0);     // value < alignment
static_assert(util::align_down(std::size_t{256}, std::size_t{64}) == 256);  // already aligned
static_assert(util::align_up(std::size_t{1}, std::size_t{1} << 20) == (std::size_t{1} << 20));

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

}  // namespace
