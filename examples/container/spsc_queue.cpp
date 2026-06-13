/**
 * @file
 * @brief spsc_queue handing samples from a producer thread to a consumer.
 *
 * One producer thread enqueues a run of numbers; the main thread (the single
 * consumer) drains them. The summed total is deterministic regardless of how the
 * two threads interleave, since the lock-free ring loses and duplicates nothing.
 */

#include <cstdint>
#include <print>
#include <thread>

#include <nexenne/container/spsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  constexpr int count{100};
  cn::spsc_queue<int, 64> q;

  std::jthread producer{[&q] {
    for (int i{1}; i <= count; ++i) {
      while (!q.push(i).has_value()) {
        // ring full: wait for the consumer to free a slot
      }
    }
  }};

  std::int64_t sum{0};
  int got{0};
  while (got < count) {
    if (auto v{q.try_pop()}) {
      sum += *v;
      ++got;
    }
  }

  std::println("consumed {} items", got);
  std::println("sum of 1..{}: {}", count, sum);
  // consumed 100 items
  // sum of 1..100: 5050
  return 0;
}
