/**
 * @file
 * @brief Tests for nexenne::container::ring_buffer.
 */

#include <doctest/doctest.h>

#include <concepts>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/ring_buffer.hpp>

namespace {

namespace cn = nexenne::container;
using rb = cn::ring_buffer<int, 3>;

static_assert(rb::capacity() == 3);
static_assert(std::forward_iterator<rb::iterator>);
static_assert(std::forward_iterator<rb::const_iterator>);

// Drive a ring_buffer entirely at compile time: push to full, pop, wrap.
static_assert([] {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);
  bool ok{r.full() && r[0] == 1 && r[2] == 3};
  auto const first{r.pop()};
  ok = ok && first.has_value() && *first == 1 && r.size() == 2;
  r.push(4);  // tail wraps into the freed slot
  return ok && r[0] == 2 && r[1] == 3 && r[2] == 4;
}());

TEST_CASE("nexenne::container::ring_buffer push, full, pop FIFO, empty") {
  rb r;
  CHECK(r.empty());
  CHECK(r.push(1).has_value());
  CHECK(r.push(2).has_value());
  CHECK(r.push(3).has_value());
  CHECK(r.full());
  CHECK(r.push(4).error() == cn::container_error::full);
  CHECK(*r.front() == 1);
  CHECK(*r.back() == 3);
  CHECK(*r.pop() == 1);  // FIFO
  CHECK(*r.pop() == 2);
  CHECK(*r.pop() == 3);
  CHECK(r.pop().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::ring_buffer wraps around preserving order") {
  rb r;
  r.push(1);
  r.push(2);
  r.push(3);
  CHECK(*r.pop() == 1);  // head advances
  r.push(4);             // tail wraps into the freed slot
  CHECK(r.size() == 3);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);

  std::vector<int> seen;
  for (int const x : r) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector{2, 3, 4});  // iteration is FIFO
}

TEST_CASE("nexenne::container::ring_buffer power-of-two capacity wraps (mask path)") {
  cn::ring_buffer<int, 4> r;  // power of two uses the mask wrap
  for (int i{0}; i < 4; ++i) {
    r.push(i);  // [0, 1, 2, 3]
  }
  CHECK(r.pop().has_value());  // drop 0
  CHECK(r.pop().has_value());  // drop 1
  r.push(4);
  r.push(5);  // tail wraps via the mask
  CHECK(r.size() == 4);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);
  CHECK(r[3] == 5);
}

TEST_CASE("nexenne::container::ring_buffer push_overwrite keeps the most recent N") {
  rb r;
  for (int i{0}; i < 5; ++i) {
    r.push_overwrite(i);  // 0,1,2,3,4 -> keeps 2,3,4
  }
  CHECK(r.size() == 3);
  CHECK(r[0] == 2);
  CHECK(r[1] == 3);
  CHECK(r[2] == 4);
}

TEST_CASE("nexenne::container::ring_buffer emplace constructs in place") {
  cn::ring_buffer<std::pair<int, int>, 2> r;
  CHECK(r.emplace(1, 2).has_value());
  CHECK(r.front()->first == 1);
  CHECK(r.emplace(3, 4).has_value());
  CHECK(r.emplace(5, 6).error() == cn::container_error::full);
}

TEST_CASE("nexenne::container::ring_buffer at is bounds-checked") {
  rb r;
  r.push(10);
  r.push(20);
  CHECK(*r.at(1) == 20);
  CHECK(r.at(2) == nullptr);

  rb empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::ring_buffer copy and move preserve FIFO order across a wrap") {
  rb a;
  a.push(1);
  a.push(2);
  CHECK(a.pop().has_value());  // head now at index 1
  a.push(3);
  a.push(4);  // logical order [2, 3, 4] with head != 0

  rb const b{a};  // copy canonicalises head to 0
  CHECK(b.size() == 3);
  CHECK(b[0] == 2);
  CHECK(b[1] == 3);
  CHECK(b[2] == 4);

  rb c{std::move(a)};
  CHECK(c.size() == 3);
  CHECK(c[0] == 2);
  CHECK(c[2] == 4);
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::ring_buffer swap") {
  rb a;
  a.push(1);
  a.push(2);
  rb b;
  b.push(7);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a[0] == 7);
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::ring_buffer holds a move-only type") {
  cn::ring_buffer<std::unique_ptr<int>, 2> r;
  CHECK(r.push(std::make_unique<int>(5)).has_value());
  CHECK(r.emplace(std::make_unique<int>(6)).has_value());
  CHECK(r.push(std::make_unique<int>(7)).error() == cn::container_error::full);
  auto const popped{r.pop()};
  CHECK(popped.has_value());
  CHECK(**popped == 5);
}

TEST_CASE("nexenne::container::ring_buffer destroys its elements") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::ring_buffer<std::shared_ptr<int>, 3> r;
    r.push(tracker);
    r.push(tracker);
    CHECK(tracker.use_count() == 3);  // tracker + two stored
    r.push_overwrite(tracker);
    r.push_overwrite(tracker);  // evicts oldest, net still tracker + three
    CHECK(tracker.use_count() == 4);
  }
  CHECK(tracker.use_count() == 1);  // destructor destroyed all stored
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

TEST_CASE("nexenne::container::ring_buffer push picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::ring_buffer<move_counter, 4> r;
  move_counter c{&copies, &moves};

  CHECK(r.push(c).has_value());  // lvalue: one copy, no move
  CHECK(copies == 1);
  CHECK(moves == 0);

  CHECK(r.push(std::move(c)).has_value());  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

}  // namespace
