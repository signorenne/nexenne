/**
 * @file
 * @brief mpsc_queue collecting results from several worker threads.
 *
 * Three producer threads each enqueue a run of numbers; the main thread (the
 * single consumer) drains them all. The total is deterministic: the lock-free
 * sequence protocol serialises the producers' reservations without a mutex.
 */

#include <cstdint>
#include <print>
#include <thread>
#include <vector>

#include <nexenne/container/mpsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  constexpr int producers{3};
  constexpr int per_producer{100};
  constexpr int total{producers * per_producer};
  cn::mpsc_queue<int, 256> q;

  std::vector<std::jthread> workers;
  for (int p{0}; p < producers; ++p) {
    workers.emplace_back([&q] {
      for (int i{1}; i <= per_producer; ++i) {
        while (!q.push(i).has_value()) {
          // full: wait for the consumer to drain a slot
        }
      }
    });
  }

  std::int64_t sum{0};
  int got{0};
  while (got < total) {
    if (auto v{q.try_pop()}) {
      sum += *v;
      ++got;
    }
  }

  std::println("consumed {} items from {} producers", got, producers);
  std::println("total sum: {}", sum);  // 3 * (1..100) = 3 * 5050
  // consumed 300 items from 3 producers
  // total sum: 15150
  return 0;
}
