/**
 * @file
 * @brief linear_arena as per-frame scratch: allocate freely, reset at the
 *        boundary.
 *
 * Each frame bump-allocates some scratch storage in O(1) and releases all of it
 * at once with reset(), with no heap allocation anywhere.
 */

#include <print>

#include <nexenne/container/linear_arena.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::linear_arena<1024> arena;  // 1 KB inline, no heap

  for (int frame{0}; frame < 3; ++frame) {
    auto const scratch{arena.allocate<int>(10)};  // 40 bytes of scratch
    auto const tag{arena.emplace<double>(0.5 * frame)};
    if (scratch.has_value() && tag.has_value()) {
      for (int i{0}; i < 10; ++i) {
        (*scratch)[i] = i;
      }
      std::println(
        "frame {}: used {} bytes (scratch[9]={}, tag={})",
        frame,
        arena.bytes_used(),
        (*scratch)[9],
        **tag
      );
    }
    arena.reset();  // release the whole frame in O(1)
  }

  std::println("peak across frames: {} bytes", arena.high_water_mark());
  // frame 0: used 48 bytes (scratch[9]=9, tag=0)
  // frame 1: used 48 bytes (scratch[9]=9, tag=0.5)
  // frame 2: used 48 bytes (scratch[9]=9, tag=1)
  // peak across frames: 48 bytes
  return 0;
}
