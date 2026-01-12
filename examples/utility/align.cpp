/**
 * @file
 * @brief A bump allocator handing out aligned slices of a byte arena.
 *
 * Each allocation advances a cursor rounded up with the integral align_up, then
 * confirms the returned pointers sit on their boundaries with the pointer
 * is_aligned.
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
    cursor = util::align_up(cursor, alignment);
    auto* const block{arena.data() + cursor};
    cursor += size;
    return block;
  }};

  auto* const a{allocate(10, 8)};
  auto* const b{allocate(4, 16)};

  std::println("offset a = {}", static_cast<std::size_t>(a - arena.data()));
  std::println("offset b = {}", static_cast<std::size_t>(b - arena.data()));
  std::println("a 8-aligned: {}", util::is_aligned(a, 8));
  std::println("b 16-aligned: {}", util::is_aligned(b, 16));

  static_assert(util::align_up(std::size_t{17}, std::size_t{8}) == 24);
  static_assert(util::align_down(std::size_t{17}, std::size_t{8}) == 16);
  static_assert(util::is_aligned(std::size_t{32}, std::size_t{16}));
  return 0;
}
