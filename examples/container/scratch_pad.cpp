/**
 * @file
 * @brief scratch_pad as a nested checkpoint inside a long-lived arena.
 *
 * The arena keeps a long-lived allocation; a scratch_pad block allocates
 * temporaries on top and releases exactly them when it goes out of scope, so the
 * long-lived data survives.
 */

#include <print>

#include <nexenne/container/linear_arena.hpp>
#include <nexenne/container/scratch_pad.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  cn::linear_arena<1024> arena;

  auto const persistent{arena.allocate<int>(8)};  // long-lived: 32 bytes
  if (persistent.has_value()) {
    (*persistent)[0] = 1;
  }
  std::println("after persistent: {} bytes used", arena.bytes_used());

  {
    cn::scratch_pad scratch{arena};                 // checkpoint here
    auto const temp{scratch.allocate<double>(64)};  // 512 bytes, temporary
    if (temp.has_value()) {
      (*temp)[0] = 3.14;
    }
    std::println("inside scratch:  {} bytes used", arena.bytes_used());
  }  // scratch rewinds the arena to the checkpoint

  std::println("after scratch:   {} bytes used", arena.bytes_used());
  // after persistent: 32 bytes used
  // inside scratch:  544 bytes used
  // after scratch:   32 bytes used
  return 0;
}
