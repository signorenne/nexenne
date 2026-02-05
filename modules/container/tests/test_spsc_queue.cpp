/**
 * @file
 * @brief Tests for nexenne::container::spsc_queue.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <memory>
#include <thread>
#include <utility>

#include <nexenne/container/error.hpp>
#include <nexenne/container/spsc_queue.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::spsc_queue fills to capacity then reports full") {
  cn::spsc_queue<int, 4> q;  // capacity 3
  CHECK(q.capacity() == 3);
  CHECK(q.empty_approx());
  CHECK(q.push(1).has_value());
  CHECK(q.push(2).has_value());
  CHECK(q.push(3).has_value());
  CHECK(q.full_approx());
  CHECK(q.push(4).error() == cn::container_error::full);
  CHECK(q.size_approx() == 3);
}

TEST_CASE("nexenne::container::spsc_queue pops in FIFO order") {
  cn::spsc_queue<int, 8> q;
  for (int i{0}; i < 5; ++i) {
    CHECK(q.push(i).has_value());
  }
  for (int i{0}; i < 5; ++i) {
    auto v{q.pop()};
    REQUIRE(v.has_value());
    CHECK(*v == i);
  }
  CHECK(q.pop().error() == cn::container_error::empty);
  CHECK(q.empty_approx());
}

TEST_CASE("nexenne::container::spsc_queue wraps around the ring") {
  cn::spsc_queue<int, 4> q;  // capacity 3
  for (int round{0}; round < 10; ++round) {
    CHECK(q.push(round).has_value());
    auto v{q.try_pop()};
    REQUIRE(v.has_value());
    CHECK(*v == round);  // push/pop one at a time wraps cleanly
  }
}

TEST_CASE("nexenne::container::spsc_queue emplace and try_pop") {
  cn::spsc_queue<std::pair<int, int>, 4> q;
  CHECK(q.emplace(1, 2).has_value());
  auto v{q.try_pop()};
  REQUIRE(v.has_value());
  CHECK(v->first == 1);
  CHECK(v->second == 2);
  CHECK_FALSE(q.try_pop().has_value());
}

TEST_CASE("nexenne::container::spsc_queue holds a move-only type") {
  cn::spsc_queue<std::unique_ptr<int>, 4> q;
  CHECK(q.push(std::make_unique<int>(7)).has_value());
  auto v{q.pop()};
  REQUIRE(v.has_value());
  CHECK(**v == 7);
}

TEST_CASE("nexenne::container::spsc_queue one producer and one consumer move every item") {
  constexpr int total{200000};
  cn::spsc_queue<int, 1024> q;
  std::int64_t consumed_sum{0};
  int consumed_count{0};

  {
    std::jthread producer{[&q] {
      for (int i{0}; i < total; ++i) {
        while (!q.push(i).has_value()) {
          // queue full: spin until the consumer drains a slot
        }
      }
    }};
    std::jthread consumer{[&q, &consumed_sum, &consumed_count] {
      while (consumed_count < total) {
        if (auto v{q.try_pop()}) {
          consumed_sum += *v;
          ++consumed_count;
        }
      }
    }};
  }  // jthreads join here

  CHECK(consumed_count == total);
  // sum of 0..total-1 must be conserved exactly (no lost or duplicated items)
  auto const expected{static_cast<std::int64_t>(total) * (total - 1) / 2};
  CHECK(consumed_sum == expected);
}

}  // namespace
