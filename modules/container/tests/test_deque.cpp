/**
 * @file
 * @brief Tests for nexenne::container::deque.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <memory>
#include <type_traits>
#include <utility>

#include <nexenne/container/deque.hpp>

namespace {

namespace cn = nexenne::container;
using dq = cn::deque<int>;

TEST_CASE("nexenne::container::deque default and capacity constructors") {
  dq d;
  CHECK(d.empty());
  CHECK(d.size() == 0);
  CHECK(d.capacity() == 0);
  CHECK(d.front() == nullptr);
  CHECK(d.back() == nullptr);

  dq reserved(10);
  CHECK(reserved.empty());
  CHECK(reserved.capacity() >= 10);
  CHECK((reserved.capacity() & (reserved.capacity() - 1)) == 0);  // power of two
}

TEST_CASE("nexenne::container::deque initializer list preserves order") {
  dq d{1, 2, 3, 4};
  CHECK(d.size() == 4);
  CHECK(*d.front() == 1);
  CHECK(*d.back() == 4);
  CHECK(d[0] == 1);
  CHECK(d[3] == 4);
}

TEST_CASE("nexenne::container::deque pushes and pops at both ends") {
  dq d;
  d.push_back(2);
  d.push_back(3);
  d.push_front(1);
  d.push_front(0);  // [0, 1, 2, 3]
  CHECK(d.size() == 4);
  CHECK(d[0] == 0);
  CHECK(d[1] == 1);
  CHECK(d[2] == 2);
  CHECK(d[3] == 3);

  auto const front{d.pop_front()};
  REQUIRE(front.has_value());
  CHECK(*front == 0);
  auto const back{d.pop_back()};
  REQUIRE(back.has_value());
  CHECK(*back == 3);
  CHECK(d.size() == 2);  // [1, 2]
  CHECK(d[0] == 1);
  CHECK(d[1] == 2);
}

TEST_CASE("nexenne::container::deque pop from empty returns error") {
  dq d;
  CHECK(d.pop_back().error() == cn::container_error::empty);
  CHECK(d.pop_front().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::deque emplace returns a reference") {
  dq d;
  auto& back{d.emplace_back(7)};
  CHECK(back == 7);
  back = 8;  // the returned reference aliases the stored element
  REQUIRE(d.back() != nullptr);
  CHECK(*d.back() == 8);

  auto& front{d.emplace_front(1)};
  CHECK(front == 1);
  CHECK(*d.front() == 1);
}

TEST_CASE("nexenne::container::deque growth preserves order") {
  dq d;
  for (int i{0}; i < 16; ++i) {
    d.push_back(i);  // grows 8 -> 16, re-packing the ring
  }
  CHECK(d.size() == 16);
  for (int i{0}; i < 16; ++i) {
    CHECK(d[static_cast<std::size_t>(i)] == i);
  }
}

TEST_CASE("nexenne::container::deque front pushes wrap the ring") {
  dq d(8);  // fixed cap, no growth
  for (int i{0}; i < 8; ++i) {
    d.push_front(i);  // head wraps backward across the buffer
  }
  CHECK(d.size() == 8);
  for (int i{0}; i < 8; ++i) {
    CHECK(d[static_cast<std::size_t>(i)] == 7 - i);  // last pushed is the front
  }
}

TEST_CASE("nexenne::container::deque reserve and clear") {
  dq d{1, 2, 3};
  d.reserve(100);
  CHECK(d.capacity() >= 100);
  CHECK(d.size() == 3);  // reserve preserves elements
  CHECK(d[0] == 1);

  d.clear();
  CHECK(d.empty());
  CHECK(d.capacity() >= 100);  // capacity retained
}

TEST_CASE("nexenne::container::deque copy is deep and independent") {
  dq a{1, 2, 3};
  dq b{a};
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  a[0] = 99;
  CHECK(b[0] == 1);  // independent
}

TEST_CASE("nexenne::container::deque move steals the storage") {
  dq a{1, 2, 3};
  dq b{std::move(a)};
  CHECK(b.size() == 3);
  CHECK(b[1] == 2);
  CHECK(a.empty());
  CHECK(a.capacity() == 0);
}

TEST_CASE("nexenne::container::deque copy-and-swap assignment (copy and move)") {
  dq a{1, 2, 3};
  dq b;
  b = a;  // copy-assign
  CHECK(b.size() == 3);
  CHECK(b[2] == 3);
  a[0] = 99;
  CHECK(b[0] == 1);  // independent

  dq c;
  c = std::move(b);  // move-assign
  CHECK(c.size() == 3);
  CHECK(b.empty());
}

TEST_CASE("nexenne::container::deque swap") {
  dq a{1, 2};
  dq b{7, 8, 9};
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);
  CHECK(a[0] == 7);
}

TEST_CASE("nexenne::container::deque holds a move-only type") {
  static_assert(!std::is_copy_constructible_v<cn::deque<std::unique_ptr<int>>>);
  cn::deque<std::unique_ptr<int>> d;
  d.push_back(std::make_unique<int>(1));
  d.emplace_front(std::make_unique<int>(0));
  CHECK(d.size() == 2);
  CHECK(*d[0] == 0);
  CHECK(*d[1] == 1);

  auto popped{d.pop_back()};
  REQUIRE(popped.has_value());
  CHECK(**popped == 1);

  cn::deque<std::unique_ptr<int>> moved{std::move(d)};
  CHECK(moved.size() == 1);
}

namespace {
struct move_counter {
  int* copies;
  int* moves;

  move_counter(int* c, int* m) noexcept : copies{c}, moves{m} {}

  move_counter(move_counter const& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*copies;
  }

  move_counter(move_counter&& other) noexcept : copies{other.copies}, moves{other.moves} {
    ++*moves;
  }
};
}  // namespace

TEST_CASE("nexenne::container::deque push_back picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::deque<move_counter> d;
  d.reserve(4);  // avoid a growth move confounding the counts
  move_counter c{&copies, &moves};

  d.push_back(c);  // lvalue: one copy
  CHECK(copies == 1);
  CHECK(moves == 0);

  d.push_back(std::move(c));  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

}  // namespace
