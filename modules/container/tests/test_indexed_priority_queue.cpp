/**
 * @file
 * @brief Tests for nexenne::container::indexed_priority_queue.
 */

#include <doctest/doctest.h>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>

namespace {

namespace cn = nexenne::container;
using max_pq = cn::indexed_priority_queue<int>;
using min_pq = cn::indexed_priority_queue<int, std::greater<int>>;

TEST_CASE("nexenne::container::indexed_priority_queue max-heap pops descending") {
  max_pq q;
  q.push(3);
  q.push(1);
  q.push(4);
  q.push(1);
  q.push(5);
  CHECK(q.size() == 5);
  REQUIRE(q.top() != nullptr);
  CHECK(*q.top() == 5);
  std::vector<int> drained;
  while (!q.empty()) {
    drained.push_back(*q.pop());
  }
  CHECK(drained == std::vector{5, 4, 3, 1, 1});
  CHECK(q.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::indexed_priority_queue min-heap pops ascending") {
  min_pq q;
  for (int v : {3, 1, 4, 1, 5, 9, 2, 6}) {
    q.push(v);
  }
  std::vector<int> drained;
  while (!q.empty()) {
    drained.push_back(*q.pop());
  }
  CHECK(drained == std::vector{1, 1, 2, 3, 4, 5, 6, 9});
}

TEST_CASE("nexenne::container::indexed_priority_queue value_at and top_handle") {
  max_pq q;
  auto const h3{q.push(3)};
  auto const h7{q.push(7)};
  REQUIRE(q.value_at(h3).has_value());
  CHECK(*q.value_at(h3).value() == 3);
  CHECK(*q.value_at(h7).value() == 7);
  REQUIRE(q.top_handle().has_value());
  CHECK(q.top_handle().value() == h7);  // 7 is on top of a max-heap
  CHECK(q.value_at(max_pq::invalid_handle).error() == cn::container_error::not_found);
}

TEST_CASE("nexenne::container::indexed_priority_queue update raises a priority") {
  max_pq q;
  auto const ha{q.push(1)};
  q.push(2);
  q.push(3);
  CHECK(*q.top() == 3);
  REQUIRE(q.update(ha, 10).has_value());  // raise 1 -> 10
  CHECK(*q.top() == 10);
  CHECK(q.top_handle().value() == ha);
}

TEST_CASE("nexenne::container::indexed_priority_queue update lowers a priority") {
  max_pq q;
  auto const ha{q.push(10)};
  q.push(2);
  q.push(3);
  CHECK(*q.top() == 10);
  REQUIRE(q.update(ha, 1).has_value());  // lower 10 -> 1
  CHECK(*q.top() == 3);                  // 3 now on top
  CHECK(*q.value_at(ha).value() == 1);
  CHECK(q.update(max_pq::invalid_handle, 0).error() == cn::container_error::not_found);
}

TEST_CASE("nexenne::container::indexed_priority_queue erase by handle keeps the heap valid") {
  max_pq q;
  q.push(5);
  auto const h3{q.push(3)};
  q.push(8);
  q.push(1);
  q.push(7);
  REQUIRE(q.erase(h3).has_value());
  CHECK_FALSE(q.contains(h3));
  CHECK(q.size() == 4);
  CHECK(q.erase(h3).error() == cn::container_error::not_found);  // already gone
  std::vector<int> drained;
  while (!q.empty()) {
    drained.push_back(*q.pop());
  }
  CHECK(drained == std::vector{8, 7, 5, 1});  // 3 absent, rest sorted descending
}

TEST_CASE("nexenne::container::indexed_priority_queue handles stay valid across mutations") {
  max_pq q;
  auto const h1{q.push(1)};
  auto const h2{q.push(2)};
  q.push(3);
  q.pop();  // removes 3
  // h1 and h2 still address their values after the pop reorganised the heap
  CHECK(*q.value_at(h1).value() == 1);
  CHECK(*q.value_at(h2).value() == 2);
  CHECK(q.contains(h1));
  CHECK(q.contains(h2));
}

TEST_CASE("nexenne::container::indexed_priority_queue emplace, clear, swap") {
  max_pq a;
  a.emplace(5);
  a.emplace(9);
  CHECK(*a.top() == 9);
  max_pq b;
  b.push(1);
  swap(a, b);
  CHECK(*a.top() == 1);
  CHECK(*b.top() == 9);
  b.clear();
  CHECK(b.empty());
  CHECK(b.top() == nullptr);
}

TEST_CASE("nexenne::container::indexed_priority_queue holds a move-only value") {
  cn::indexed_priority_queue<std::unique_ptr<int>> q;  // ordered by pointer (max)
  auto const h{q.push(std::make_unique<int>(1))};
  q.emplace(std::make_unique<int>(2));
  REQUIRE(q.value_at(h).has_value());
  CHECK(**q.value_at(h).value() == 1);
  auto const popped{q.pop()};
  REQUIRE(popped.has_value());  // pops the larger pointer; both are live ptrs
  CHECK(q.size() == 1);
}

}  // namespace
