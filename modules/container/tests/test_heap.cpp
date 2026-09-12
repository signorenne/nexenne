/**
 * @file
 * @brief Tests for nexenne::container::heap.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include <nexenne/container/heap.hpp>

namespace {

namespace cn = nexenne::container;
using max_heap = cn::heap<int>;
using min_heap = cn::heap<int, std::greater<int>>;

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
  cn::heap h2(src.begin(), src.end());
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
  cn::heap<int, std::greater<int>> h{std::greater<int>{}};
  h.push(5);
  h.push(2);
  h.push(8);
  CHECK(*h.top() == 2);
  CHECK(h.key_comp()(2, 1));
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
  CHECK(*h.data() == 5);
}

TEST_CASE("nexenne::container::heap rebuild restores the invariant") {
  max_heap h{1, 2, 3};
  CHECK(*h.top() == 3);
  h.rebuild();
  CHECK(*h.top() == 3);
}

TEST_CASE("nexenne::container::heap holds a move-only type") {
  cn::heap<std::unique_ptr<int>> h;
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

TEST_CASE("nexenne::container::heap empty heap inspection is null and zero") {
  max_heap h;
  CHECK(h.empty());
  CHECK(h.size() == 0);
  CHECK(h.top() == nullptr);
  CHECK(h.span().empty());
  CHECK(h.max_size() > 0);
  CHECK(h.key_comp()(1, 2));
  CHECK_FALSE(h.key_comp()(2, 1));
}

TEST_CASE("nexenne::container::heap single-element push then pop") {
  min_heap h;
  h.push(42);
  REQUIRE(h.top() != nullptr);
  CHECK(*h.top() == 42);
  CHECK(h.size() == 1);
  auto const v{h.pop()};
  REQUIRE(v.has_value());
  CHECK(*v == 42);
  CHECK(h.empty());
}

TEST_CASE("nexenne::container::heap range ctor from a std::vector heapifies") {
  std::vector<int> const src{5, 2, 9, 1, 7, 3};
  cn::heap h(src.begin(), src.end());
  CHECK(h.size() == 6);
  REQUIRE(h.top() != nullptr);
  CHECK(*h.top() == 9);
}

TEST_CASE("nexenne::container::heap a stateful custom comparator orders correctly") {
  struct by_abs {
    [[nodiscard]] auto operator()(int const a, int const b) const noexcept -> bool {
      auto const aa{a < 0 ? -a : a};
      auto const bb{b < 0 ? -b : b};
      return aa > bb;
    }
  };

  cn::heap<int, by_abs> h{by_abs{}};
  for (int const v : {-7, 3, -1, 9, -4, 2}) {
    h.push(v);
  }
  std::vector<int> magnitudes;
  while (!h.empty()) {
    auto const v{h.pop()};
    REQUIRE(v.has_value());
    magnitudes.push_back(*v < 0 ? -*v : *v);
  }
  CHECK(magnitudes == std::vector{1, 2, 3, 4, 7, 9});
}

TEST_CASE("nexenne::container::heap interleaved push and pop keeps the invariant") {
  min_heap h;
  h.push(5);
  h.push(3);
  CHECK(*h.top() == 3);
  CHECK(*h.pop() == 3);
  h.push(1);
  h.push(4);
  CHECK(*h.top() == 1);
  CHECK(*h.pop() == 1);
  CHECK(*h.pop() == 4);
  CHECK(*h.pop() == 5);
  CHECK(h.empty());
}

TEST_CASE("nexenne::container::heap swap exchanges stateful comparators too") {
  min_heap a{8, 6, 7};
  min_heap b{3, 1, 2};
  swap(a, b);
  REQUIRE(a.top() != nullptr);
  REQUIRE(b.top() != nullptr);
  CHECK(*a.top() == 1);
  CHECK(*b.top() == 6);
  CHECK(a.key_comp()(2, 1));
}

TEST_CASE("nexenne::container::heap rebuild after mutating the backing storage via span") {
  max_heap h{1, 2, 3, 4, 5};
  CHECK(*h.top() == 5);
  std::vector<int> sorted(h.span().begin(), h.span().end());
  std::sort(sorted.begin(), sorted.end());
  CHECK(sorted == std::vector{1, 2, 3, 4, 5});
  h.rebuild();
  CHECK(*h.top() == 5);
  CHECK(h.size() == 5);
}

TEST_CASE("nexenne::container::heap holds a non-trivial std::string element") {
  cn::heap<std::string> h;
  h.push(std::string(40, 'm'));
  std::string lvalue(40, 'z');
  h.push(lvalue);
  CHECK(lvalue.size() == 40);
  h.emplace(40, 'a');
  CHECK(h.size() == 3);
  REQUIRE(h.top() != nullptr);
  CHECK(h.top()->front() == 'z');

  std::vector<std::string> out;
  while (!h.empty()) {
    auto const v{h.pop()};
    REQUIRE(v.has_value());
    out.push_back(*v);
  }
  REQUIRE(out.size() == 3);
  CHECK(out[0].front() == 'z');
  CHECK(out[1].front() == 'm');
  CHECK(out[2].front() == 'a');
}

TEST_CASE("nexenne::container::heap pops in fully sorted order under randomized pushes") {
  std::mt19937 rng{777};
  std::vector<int> reference;
  max_heap h;
  for (int i{0}; i < 2000; ++i) {
    auto const v{static_cast<int>(rng() % 100000)};
    reference.push_back(v);
    h.push(v);
  }
  REQUIRE(h.size() == reference.size());
  std::sort(reference.begin(), reference.end(), std::greater<int>{});
  std::vector<int> popped;
  while (!h.empty()) {
    auto const v{h.pop()};
    REQUIRE(v.has_value());
    popped.push_back(*v);
  }
  CHECK(popped == reference);
}

static_assert(sizeof(cn::heap<int>) == sizeof(std::vector<int>));

}  // namespace
