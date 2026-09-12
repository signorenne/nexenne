/**
 * @file
 * @brief deque as a sliding window: push new readings at the back, drop old
 *        ones from the front.
 *
 * The window keeps the most recent N values; each new reading goes on the back
 * and, once the window is full, the oldest is popped from the front, both O(1).
 *
 * Expected output:
 *
 * \code
 * dropped 10
 * dropped 20
 * window: 30 40 50 60
 * \endcode
 */

#include <cstddef>
#include <print>

#include <nexenne/container/deque.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  constexpr std::size_t window{4};
  cn::deque<int> recent;

  for (int const reading : {10, 20, 30, 40, 50, 60}) {
    recent.push_back(reading);
    if (recent.size() > window) {
      auto const dropped{recent.pop_front()};
      std::println("dropped {}", *dropped);
    }
  }

  std::print("window:");
  for (std::size_t i{0}; i < recent.size(); ++i) {
    std::print(" {}", recent[i]);
  }
  std::println("");
  return 0;
}
