/**
 * @file
 * @brief Tests for nexenne::container::heap.
 */

#include <doctest/doctest.h>

#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/heap.hpp>

namespace {

namespace cn = nexenne::container;
using max_heap = cn::heap<int>;                     // default less -> max-heap
using min_heap = cn::heap<int, std::greater<int>>;  // min-heap

// heap is usable in a constant expression (the standard heap algorithms are
// constexpr since C++20).
static_assert([] {
  max_heap h{3, 1, 4, 1, 5};
  auto const* const top{h.top()};
  return top != nullptr && *top == 5;
}());

TEST_CASE("nexenne::container::heap max-heap pops in descending order") {
  max_heap h;
  h.push(3);
  h.push(1);
  h.push(4);
  h.push(1);
  h.push(5);
  CHECK(h.size() == 5);
  REQUIRE(h.top() != nullptr);
  CHECK(*h.top() == 5);

  std::vector<int> out;
  while (!h.empty()) {
    auto const value{h.pop()};
    REQUIRE(value.has_value());
    out.push_back(*value);
  }
  CHECK(out == std::vector{5, 4, 3, 1, 1});
}

TEST_CASE("nexenne::container::heap min-heap with std::greater") {
  min_heap h{5, 3, 8, 1};
  REQUIRE(h.top() != nullptr);
  CHECK(*h.top() == 1);
  auto const value{h.pop()};
  REQUIRE(value.has_value());
  CHECK(*value == 1);
}

TEST_CASE("nexenne::container::heap heapify ctor, range ctor, and CTAD") {
  max_heap h{10, 40, 20, 30};
  CHECK(h.size() == 4);
  CHECK(*h.top() == 40);

  std::array<int, 3> const src{7, 9, 8};
  cn::heap h2(src.begin(), src.end());  // CTAD deduces heap<int>
  CHECK(*h2.top() == 9);
}

TEST_CASE("nexenne::container::heap emplace and pop-empty error") {
  max_heap h;
  h.emplace(42);
  REQUIRE(h.top() != nullptr);
  CHECK(*h.top() == 42);
  CHECK(h.pop().has_value());
  CHECK(h.empty());
  CHECK(h.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::heap custom comparator instance") {
  cn::heap<int, std::greater<int>> h{std::greater<int>{}};  // min-heap
  h.push(5);
  h.push(2);
  h.push(8);
  CHECK(*h.top() == 2);
  CHECK(h.key_comp()(2, 1));  // the stored comparator is std::greater: 2 > 1
}

TEST_CASE("nexenne::container::heap reserve, capacity, clear, shrink, swap") {
  max_heap h;
  h.reserve(50);
  CHECK(h.capacity() >= 50);
  h.push(1);
  h.push(2);
  CHECK(h.size() == 2);

  max_heap other{9, 8, 7};
  swap(h, other);
  CHECK(h.size() == 3);
  CHECK(*h.top() == 9);
  CHECK(other.size() == 2);

  h.clear();
  CHECK(h.empty());
  h.shrink_to_fit();
}

TEST_CASE("nexenne::container::heap data and span are in heap order with the top first") {
  max_heap h{3, 1, 4, 1, 5};
  CHECK(h.span().size() == 5);
  REQUIRE(h.data() != nullptr);
  CHECK(*h.data() == 5);  // the top is at index 0
}

TEST_CASE("nexenne::container::heap rebuild restores the invariant") {
  max_heap h{1, 2, 3};
  CHECK(*h.top() == 3);
  h.rebuild();
  CHECK(*h.top() == 3);
}

TEST_CASE("nexenne::container::heap holds a move-only type") {
  cn::heap<std::unique_ptr<int>> h;  // less<unique_ptr> orders by pointer value
  h.push(std::make_unique<int>(1));
  h.emplace(std::make_unique<int>(2));
  h.push(std::make_unique<int>(3));
  CHECK(h.size() == 3);
  int popped{0};
  while (!h.empty()) {
    auto const value{h.pop()};
    REQUIRE(value.has_value());
    ++popped;
  }
  CHECK(popped == 3);
}

}  // namespace
