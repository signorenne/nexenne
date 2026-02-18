/**
 * @file
 * @brief Tests for nexenne::container::indexed_priority_queue.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <functional>
#include <memory>
#include <queue>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>

namespace {

namespace cn = nexenne::container;
using max_pq = cn::indexed_priority_queue<int>;
using min_pq = cn::indexed_priority_queue<int, std::greater<int>>;

// Verify the max-heap invariant directly over the diagnostic entries() span:
// every parent must be >= its children under std::less.
template <typename Queue>
auto is_max_heap(Queue const& q) -> bool {
  auto const e{q.entries()};
  for (std::size_t i{1}; i < e.size(); ++i) {
    auto const parent{(i - 1) / 2};
    if (e[parent].value < e[i].value) {
      return false;
    }
  }
  return true;
}

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

TEST_CASE("nexenne::container::indexed_priority_queue the empty queue") {
  max_pq q;
  CHECK(q.empty());
  CHECK(q.size() == 0);
  CHECK(q.top() == nullptr);
  CHECK(q.pop().error() == cn::container_error::empty);
  CHECK(q.top_handle().error() == cn::container_error::empty);
  CHECK_FALSE(q.contains(0));
  CHECK_FALSE(q.contains(max_pq::invalid_handle));
  CHECK(q.value_at(0).error() == cn::container_error::not_found);
  CHECK(q.update(0, 1).error() == cn::container_error::not_found);
  CHECK(q.erase(0).error() == cn::container_error::not_found);
  CHECK(q.entries().empty());
  CHECK(q.max_size() > 0);
}

TEST_CASE("nexenne::container::indexed_priority_queue single element") {
  max_pq q;
  auto const h{q.push(5)};
  CHECK(q.size() == 1);
  REQUIRE(q.top() != nullptr);
  CHECK(*q.top() == 5);
  CHECK(q.top_handle().value() == h);
  CHECK(*q.value_at(h).value() == 5);
  auto const popped{q.pop()};
  REQUIRE(popped.has_value());
  CHECK(*popped == 5);
  CHECK(q.empty());
  CHECK_FALSE(q.contains(h));  // handle invalidated by the pop
}

TEST_CASE("nexenne::container::indexed_priority_queue reserve and capacity") {
  max_pq q;
  q.reserve(64);
  CHECK(q.capacity() >= 64);
  q.push(1);
  q.push(2);
  CHECK(q.size() == 2);
  q.shrink_to_fit();
  CHECK(q.size() == 2);  // contents preserved through shrink
  CHECK(*q.top() == 2);
}

TEST_CASE(
  "nexenne::container::indexed_priority_queue update on a buried node re-heapifies both ways"
) {
  max_pq q;
  std::vector<max_pq::handle_type> handles;
  for (int v : {50, 40, 30, 20, 10, 5}) {
    handles.push_back(q.push(v));
  }
  REQUIRE(is_max_heap(q));
  // raise a deep small node above the root
  REQUIRE(q.update(handles.back(), 100).has_value());  // 5 -> 100
  CHECK(*q.top() == 100);
  CHECK(is_max_heap(q));
  // lower the former root well below others
  REQUIRE(q.update(handles.front(), -1).has_value());  // 50 -> -1
  CHECK(*q.value_at(handles.front()).value() == -1);
  CHECK(is_max_heap(q));
}

TEST_CASE("nexenne::container::indexed_priority_queue erase the top, a leaf, and a middle node") {
  max_pq q;
  std::vector<max_pq::handle_type> h;
  for (int v : {9, 8, 7, 6, 5, 4, 3}) {
    h.push_back(q.push(v));
  }
  REQUIRE(is_max_heap(q));
  // erase the current top (handle of 9)
  REQUIRE(q.erase(h[0]).has_value());
  CHECK(*q.top() == 8);
  CHECK(is_max_heap(q));
  // erase a leaf-ish small value (3)
  REQUIRE(q.erase(h[6]).has_value());
  CHECK(is_max_heap(q));
  // erase a middle value (6)
  REQUIRE(q.erase(h[3]).has_value());
  CHECK(is_max_heap(q));
  CHECK(q.size() == 4);
}

TEST_CASE("nexenne::container::indexed_priority_queue recycles handles after removal") {
  max_pq q;
  auto const h0{q.push(1)};
  auto const h1{q.push(2)};
  static_cast<void>(q.pop());  // removes 2 (top), frees h1
  CHECK_FALSE(q.contains(h1));
  CHECK(q.contains(h0));
  auto const h2{q.push(3)};  // should reuse the freed handle slot
  CHECK(q.contains(h2));
  CHECK(*q.value_at(h2).value() == 3);
  // the freed handle, once reissued, must address the new element, not the old
  CHECK(q.value_at(h2).has_value());
}

TEST_CASE("nexenne::container::indexed_priority_queue of std::string values") {
  cn::indexed_priority_queue<std::string> q;  // max-heap, lexicographic
  auto const h{q.push(std::string("apple"))};
  q.push(std::string("zebra"));
  q.push(std::string("mango"));
  REQUIRE(q.top() != nullptr);
  CHECK(*q.top() == "zebra");
  CHECK(*q.value_at(h).value() == "apple");
  REQUIRE(q.update(h, std::string("zzz")).has_value());  // raise apple above zebra
  CHECK(*q.top() == "zzz");
  auto const popped{q.pop()};
  REQUIRE(popped.has_value());
  CHECK(*popped == "zzz");
}

TEST_CASE("nexenne::container::indexed_priority_queue heap invariant holds after many random ops") {
  std::mt19937 rng{2024};
  std::uniform_int_distribution<int> values{0, 1000};
  std::uniform_int_distribution<int> ops{0, 3};
  max_pq q;
  std::vector<max_pq::handle_type> live;
  for (int step{0}; step < 5000; ++step) {
    switch (ops(rng)) {
      case 0:
        live.push_back(q.push(values(rng)));
        break;
      case 1:
        if (!q.empty()) {
          auto const top{q.top_handle().value()};
          static_cast<void>(q.pop());
          std::erase(live, top);
        }
        break;
      case 2:
        if (!live.empty()) {
          std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
          auto const idx{pick(rng)};
          CHECK(q.update(live[idx], values(rng)).has_value());
        }
        break;
      default:
        if (!live.empty()) {
          std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
          auto const idx{pick(rng)};
          CHECK(q.erase(live[idx]).has_value());
          live.erase(live.begin() + static_cast<std::ptrdiff_t>(idx));
        }
        break;
    }
    REQUIRE(is_max_heap(q));
    CHECK(q.size() == live.size());
  }
  // every surviving handle still resolves
  for (auto const h : live) {
    CHECK(q.contains(h));
  }
}

TEST_CASE(
  "nexenne::container::indexed_priority_queue differential pop order against std::priority_queue"
) {
  std::mt19937 rng{55};
  std::uniform_int_distribution<int> values{-500, 500};
  max_pq q;
  std::priority_queue<int> ref;  // std::less, max on top, the same discipline
  for (int i{0}; i < 3000; ++i) {
    int const v{values(rng)};
    q.push(v);
    ref.push(v);
  }
  CHECK(q.size() == ref.size());
  // draining both must yield identical descending sequences
  while (!ref.empty()) {
    REQUIRE_FALSE(q.empty());
    auto const got{q.pop()};
    REQUIRE(got.has_value());
    CHECK(*got == ref.top());
    ref.pop();
  }
  CHECK(q.empty());
}

}  // namespace
