/**
 * @file
 * @brief mpmc_queue as a thread-pool work queue: many producers, many consumers.
 *
 * Producer threads enqueue work items; consumer threads dequeue and tally them.
 * No locks are taken; the conserved total proves nothing was lost or duplicated
 * across the concurrent enqueue and dequeue.
 */

#include <atomic>
#include <cstdint>
#include <print>
#include <thread>
#include <vector>

#include <nexenne/container/mpmc_queue.hpp>

namespace {

namespace cn = nexenne::container;

}  // namespace

auto main() -> int {
  constexpr int producers{4};
  constexpr int consumers{4};
  constexpr int per_producer{1000};
  constexpr int total{producers * per_producer};
  cn::mpmc_queue<int, 256> q;

  std::atomic<int> consumed{0};
  std::atomic<std::int64_t> sum{0};

  std::vector<std::jthread> threads;
  for (int p{0}; p < producers; ++p) {
    threads.emplace_back([&q] {
      for (int i{0}; i < per_producer; ++i) {
        while (!q.push(1).has_value()) {
          // full: wait for a consumer to free a slot
        }
      }
    });
  }
  for (int c{0}; c < consumers; ++c) {
    threads.emplace_back([&q, &consumed, &sum] {
      while (consumed.load(std::memory_order_relaxed) < total) {
        if (auto v{q.try_pop()}) {
          sum.fetch_add(*v, std::memory_order_relaxed);
          consumed.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  threads.clear();  // join all jthreads

  std::println("{} producers, {} consumers", producers, consumers);
  std::println("items handled: {}", consumed.load());
  std::println("summed total (each item = 1): {}", sum.load());
  // 4 producers, 4 consumers
  // items handled: 4000
  // summed total (each item = 1): 4000
  return 0;
}
