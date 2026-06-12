/**
 * @file
 * @brief Tests for nexenne::container::gap_buffer.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <iterator>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/gap_buffer.hpp>

namespace {

namespace cn = nexenne::container;
using gb = cn::gap_buffer<int>;

static_assert(std::random_access_iterator<gb::iterator>);
static_assert(std::random_access_iterator<gb::const_iterator>);

// gap_buffer is usable in a constant expression.
static_assert([] {
  gb b;
  b.insert(10);
  b.insert(30);  // [10, 30], cursor at 2
  if (!b.move_cursor_to(1).has_value()) {
    return false;
  }
  b.insert(20);  // [10, 20, 30]
  return b.size() == 3 && b[0] == 10 && b[1] == 20 && b[2] == 30;
}());

TEST_CASE("nexenne::container::gap_buffer default and initializer list") {
  gb b;
  CHECK(b.empty());
  CHECK(b.cursor() == 0);

  gb c{1, 2, 3};
  CHECK(c.size() == 3);
  CHECK(c.cursor() == 3);  // cursor lands at the end
  CHECK(c[0] == 1);
  CHECK(c[2] == 3);
}

TEST_CASE("nexenne::container::gap_buffer insert at a moved cursor") {
  gb b{1, 2, 4, 5};
  REQUIRE(b.move_cursor_to(2).has_value());  // between 2 and 4
  CHECK(b.cursor() == 2);
  b.insert(3);  // [1, 2, 3, 4, 5]
  CHECK(b.size() == 5);
  for (int i{0}; i < 5; ++i) {
    CHECK(b[static_cast<std::size_t>(i)] == i + 1);
  }
}

TEST_CASE("nexenne::container::gap_buffer erase forward and backward") {
  gb b{1, 2, 3, 4};
  REQUIRE(b.move_cursor_to(2).has_value());  // between 2 and 3
  REQUIRE(b.erase_forward().has_value());    // remove 3 -> [1, 2, 4]
  CHECK(b.size() == 3);
  CHECK(b[2] == 4);
  REQUIRE(b.erase_backward().has_value());  // remove 2 -> [1, 4]
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
  CHECK(b[1] == 4);
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer erase at the edges errors") {
  gb b{1};
  REQUIRE(b.move_cursor_to(1).has_value());  // cursor at the end
  CHECK(b.erase_forward().error() == cn::container_error::empty);
  REQUIRE(b.move_cursor_to(0).has_value());  // cursor at the start
  CHECK(b.erase_backward().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::gap_buffer cursor moves and bounds") {
  gb b{1, 2, 3};
  CHECK(b.move_cursor_to(10).error() == cn::container_error::out_of_range);
  CHECK(b.move_cursor_by(-100).error() == cn::container_error::out_of_range);
  REQUIRE(b.move_cursor_by(-2).has_value());  // 3 -> 1
  CHECK(b.cursor() == 1);
}

TEST_CASE("nexenne::container::gap_buffer at, front, back") {
  gb b{10, 20, 30};
  REQUIRE(b.at(1) != nullptr);
  CHECK(*b.at(1) == 20);
  CHECK(b.at(3) == nullptr);  // out of range
  REQUIRE(b.front() != nullptr);
  CHECK(*b.front() == 10);
  REQUIRE(b.back() != nullptr);
  CHECK(*b.back() == 30);

  gb e;
  CHECK(e.front() == nullptr);
  CHECK(e.back() == nullptr);
}

TEST_CASE("nexenne::container::gap_buffer iteration regardless of gap position") {
  gb b{1, 2, 3, 4};
  REQUIRE(b.move_cursor_to(2).has_value());  // gap bisects the sequence

  std::vector<int> const forward(b.begin(), b.end());
  CHECK(forward == std::vector{1, 2, 3, 4});
  std::vector<int> const reverse(b.rbegin(), b.rend());
  CHECK(reverse == std::vector{4, 3, 2, 1});

  *b.begin() = 99;  // mutate through the iterator
  CHECK(b[0] == 99);
  CHECK(*(b.begin() + 3) == 4);     // random access
  CHECK(b.end() - b.begin() == 4);  // difference
}

TEST_CASE("nexenne::container::gap_buffer equality and ordering ignore the gap") {
  gb a{1, 2, 3};
  gb b{1, 2, 3};
  gb c{1, 2, 4};
  CHECK(a == b);
  CHECK(a != c);
  CHECK(a < c);
  REQUIRE(b.move_cursor_to(1).has_value());  // gap position must not matter
  CHECK(a == b);
}

TEST_CASE("nexenne::container::gap_buffer growth preserves order") {
  gb b;
  for (int i{0}; i < 100; ++i) {
    b.insert(i);  // forces grow_gap several times
  }
  CHECK(b.size() == 100);
  for (int i{0}; i < 100; ++i) {
    CHECK(b[static_cast<std::size_t>(i)] == i);
  }
}

TEST_CASE("nexenne::container::gap_buffer reserve, shrink, clear") {
  gb b{1, 2, 3};
  b.reserve(200);
  CHECK(b.capacity() >= 200);
  CHECK(b.size() == 3);
  b.shrink_to_fit();
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  CHECK(b.cursor() == 3);  // shrink closes the gap at the end
  b.clear();
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::gap_buffer holds a move-only type") {
  cn::gap_buffer<std::unique_ptr<int>> b;
  b.insert(std::make_unique<int>(1));
  b.emplace(std::make_unique<int>(2));
  CHECK(b.size() == 2);
  CHECK(*b[0] == 1);
  CHECK(*b[1] == 2);
  REQUIRE(b.move_cursor_to(1).has_value());
  REQUIRE(b.erase_backward().has_value());  // remove the 1
  CHECK(b.size() == 1);
  CHECK(*b[0] == 2);
}

}  // namespace
