/**
 * @file
 * @brief A bump allocator handing out aligned slices of a byte arena.
 *
 * Each allocation rounds the cursor up with the integral align_up, checks the
 * padded request still fits, and confirms the returned pointers sit on their
 * boundaries with the pointer is_aligned. align_down flushes the used region
 * back to whole cache lines. align_up asserts in debug that the padded value
 * does not overflow, so a cursor near the top of size_t aborts instead of
 * wrapping to a small offset; here the arena is tiny and the fit check keeps
 * the cursor far from that boundary.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <print>

#include <nexenne/utility/align.hpp>

namespace util = nexenne::utility;

auto main() -> int {
  alignas(64) auto arena{std::array<std::byte, 256>{}};
  auto cursor{std::size_t{0}};

  auto const allocate{[&](std::size_t size, std::size_t alignment) -> std::byte* {
    auto const aligned{util::align_up(cursor, alignment)};
    if (aligned > arena.size() || size > arena.size() - aligned) {
      return nullptr;  // the padded request no longer fits
    }
    cursor = aligned + size;
    return arena.data() + aligned;
  }};

  auto* const a{allocate(10, 8)};
  auto* const b{allocate(4, 16)};

  std::println("offset a = {}", static_cast<std::size_t>(a - arena.data()));
  std::println("offset b = {}", static_cast<std::size_t>(b - arena.data()));
  std::println("a 8-aligned: {}", util::is_aligned(a, 8));
  std::println("b 16-aligned: {}", util::is_aligned(b, 16));
  // offset a = 0, offset b = 16: align_up(10, 16) padded the cursor to the
  // next 16-byte boundary before handing out b.

  // align_down answers the mirror question: how many whole 64-byte cache
  // lines does the used region cover, for a partial flush or prefetch.
  auto const whole_lines{util::align_down(cursor, std::size_t{64}) / 64};
  std::println("used = {} bytes, whole cache lines = {}", cursor, whole_lines);

  // The integral flavour is constexpr, so layouts can be computed at compile time.
  static_assert(util::align_up(std::size_t{17}, std::size_t{8}) == 24);
  static_assert(util::align_down(std::size_t{17}, std::size_t{8}) == 16);
  static_assert(util::is_aligned(std::size_t{32}, std::size_t{16}));
  return 0;
}
