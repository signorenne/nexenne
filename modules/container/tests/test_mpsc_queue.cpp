/**
 * @file
 * @brief Tests for nexenne::container::mpsc_queue.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/mpsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::mpsc_queue single-threaded fill, drain, full, empty") {
  cn::mpsc_queue<int, 4> q;  // capacity 4 (Vyukov uses all slots)
  CHECK(q.capacity() == 4);
  CHECK(q.empty_approx());
  for (int i{0}; i < 4; ++i) {
    CHECK(q.push(i).has_value());
  }
  CHECK(q.push(99).error() == cn::container_error::full);
  for (int i{0}; i < 4; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);  // FIFO
  }
  CHECK(q.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::mpsc_queue emplace and try_pop") {
  cn::mpsc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(3, 4).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 3);
  CHECK(v->second == 4);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::mpsc_queue holds a move-only type") {
  cn::mpsc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(5)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 5);
}

TEST_CASE("nexenne::container::mpsc_queue many producers and one consumer conserve every item") {
  constexpr int producers{4};
  constexpr int per_producer{50000};
  constexpr int total{producers * per_producer};
  cn::mpsc_queue<int, 1024> q;
  std::int64_t consumed_sum{0};
  int consumed_count{0};

  {
    std::vector<std::jthread> threads;
    // Each producer pushes value 1 (so the total sum equals the item count),
    // which makes a lost or duplicated item show up as a count/sum mismatch.
    for (int p{0}; p < producers; ++p) {
      threads.emplace_back([&q] {
        for (int i{0}; i < per_producer; ++i) {
          while (!q.push(1).has_value()) {
            // queue full: spin until the consumer drains a slot
          }
        }
      });
    }
    std::jthread consumer{[&q, &consumed_sum, &consumed_count] {
      while (consumed_count < total) {
        if (auto v{q.try_pop()}) {
          consumed_sum += *v;
          ++consumed_count;
        }
      }
    }};
  }  // join all

  CHECK(consumed_count == total);
  CHECK(consumed_sum == total);  // each item carried value 1
}

}  // namespace
