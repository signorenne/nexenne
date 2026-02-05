/**
 * @file
 * @brief Tests for nexenne::container::mpmc_queue.
 */

#include <doctest/doctest.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/mpmc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::mpmc_queue single-threaded fill, drain, full, empty") {
  cn::mpmc_queue<int, 4> q;
  CHECK(q.capacity() == 4);
  CHECK(q.empty_approx());
  for (int i{0}; i < 4; ++i) {
    CHECK(q.push(i).has_value());
  }
  CHECK(q.full_approx());
  CHECK(q.push(99).error() == cn::container_error::full);
  for (int i{0}; i < 4; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);  // FIFO under single-threaded use
  }
  CHECK(q.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::mpmc_queue emplace and try_pop") {
  cn::mpmc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(5, 6).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 5);
  CHECK(v->second == 6);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpmc_queue holds a move-only type") {
  cn::mpmc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(9)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 9);
}

TEST_CASE("nexenne::container::mpmc_queue many producers and consumers conserve every item") {
  constexpr int producers{4};
  constexpr int consumers{4};
  constexpr int per_producer{50000};
  constexpr int total{producers * per_producer};
  cn::mpmc_queue<int, 1024> q;

  std::atomic<int> produced{0};
  std::atomic<int> consumed{0};
  std::atomic<std::int64_t> consumed_sum{0};

  {
    std::vector<std::jthread> threads;
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q, &produced] {
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(1).has_value()) {
            // full: spin until a consumer frees a slot
          }
          produced.fetch_add(1, std::memory_order_relaxed);
        }
      });
    }
    for (int c{0}; c < consumers; ++c) {
      threads.emplace_back([&q, &consumed, &consumed_sum] {
        while (consumed.load(std::memory_order_relaxed) < total) {
          if (auto v{q.try_pop()}) {
            consumed_sum.fetch_add(*v, std::memory_order_relaxed);
            consumed.fetch_add(1, std::memory_order_relaxed);
          }
        }
      });
    }
  }  // join all

  CHECK(produced.load() == total);
  CHECK(consumed.load() == total);
  CHECK(consumed_sum.load() == total);  // every item carried value 1, none lost or duplicated
}

}  // namespace
