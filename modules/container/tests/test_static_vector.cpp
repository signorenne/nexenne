/**
 * @file
 * @brief Tests for nexenne::container::static_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/static_vector.hpp>

namespace {

namespace cn = nexenne::container;
using vec = cn::static_vector<int, 4>;

static_assert(vec::capacity() == 4);
static_assert(cn::static_vector<int, 0>::capacity() == 0);  // N==0 instantiates cleanly

// static_vector is usable in a constant expression end to end: build, mutate,
// access, and compare all at compile time.
static_assert([] {
  vec v;
  v.push_back(1);
  v.emplace_back(2);
  v.push_back(3);
  return v.size() == 3 && v[0] == 1 && v[2] == 3 && !v.full();
}());
static_assert([] {
  vec v{1, 2, 3};
  v.pop_back();
  return v.size() == 2 && *v.back() == 2;
}());
static_assert([] {
  vec a{1, 2};
  vec const b{a};             // copy constructor
  vec const c{std::move(a)};  // move constructor empties the source
  return b.size() == 2 && c.size() == 2 && a.empty();
}());
static_assert([] {
  vec const a{1, 2, 3};
  vec const b{1, 2, 3};
  vec const c{3, 2, 1};
  vec const prefix{1, 2};
  return a == b && a != c && prefix < a;
}());

TEST_CASE("nexenne::container::static_vector push/pop and the capacity boundary") {
  vec v;
  CHECK(v.empty());
  CHECK(v.push_back(1).has_value());
  CHECK(v.emplace_back(2).has_value());
  CHECK(v.push_back(3).has_value());
  CHECK(v.push_back(4).has_value());
  CHECK(v.full());
  CHECK(v.push_back(5).error() == cn::container_error::full);  // rejected, not grown
  CHECK(v.size() == 4);
  CHECK(*v.front() == 1);
  CHECK(*v.back() == 4);

  CHECK(v.pop_back().has_value());
  CHECK(v.size() == 3);
  CHECK(*v.back() == 3);
}

TEST_CASE("nexenne::container::static_vector pop_back on empty reports empty") {
  vec v;
  CHECK(v.pop_back().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::static_vector initializer list truncates to capacity") {
  vec v{1, 2, 3, 4, 5, 6};
  CHECK(v.size() == 4);
  CHECK(v[0] == 1);
  CHECK(v[3] == 4);
}

TEST_CASE("nexenne::container::static_vector at/front/back are bounds-checked") {
  vec v{10, 20};
  CHECK(*v.at(1) == 20);
  CHECK(v.at(2) == nullptr);

  vec empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
  CHECK(empty.at(0) == nullptr);
}

TEST_CASE("nexenne::container::static_vector operator[] writes through; iteration and span") {
  vec v{1, 2, 3};
  v[0] = 99;
  CHECK(v[0] == 99);

  int sum{0};
  for (auto const x : v) {
    sum += x;
  }
  CHECK(sum == 104);
  CHECK(std::ranges::equal(v, std::array{99, 2, 3}));
  CHECK(v.span().size() == 3);

  std::vector<int> reversed;
  for (auto it{v.rbegin()}; it != v.rend(); ++it) {
    reversed.push_back(*it);
  }
  CHECK(reversed == std::vector{3, 2, 99});
}

TEST_CASE("nexenne::container::static_vector copy and move semantics") {
  vec a{1, 2, 3};
  vec const b{a};  // copy ctor
  CHECK(b == a);

  vec c{std::move(a)};  // move ctor
  CHECK(c.size() == 3);
  CHECK(a.empty());  // moved-from emptied

  vec d;
  d = b;  // copy-and-swap, copy path
  CHECK(d == b);

  vec e;
  e = std::move(c);  // copy-and-swap, move path
  CHECK(e.size() == 3);
  CHECK(c.empty());
}

TEST_CASE("nexenne::container::static_vector swap handles unequal sizes") {
  vec a{1, 2};
  vec b{7, 8, 9, 0};
  swap(a, b);
  CHECK(a.size() == 4);
  CHECK(a[3] == 0);
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::static_vector comparison") {
  CHECK(vec{1, 2, 3} == vec{1, 2, 3});
  CHECK(vec{1, 2} != vec{1, 2, 3});
  CHECK(vec{1, 2, 3} < vec{1, 2, 4});
  CHECK(vec{1, 2} < vec{1, 2, 3});  // a proper prefix is smaller
}

TEST_CASE("nexenne::container::static_vector holds a move-only element type") {
  cn::static_vector<std::unique_ptr<int>, 2> v;
  CHECK(v.emplace_back(std::make_unique<int>(5)).has_value());
  CHECK(*v[0] == 5);
  CHECK(v.push_back(std::make_unique<int>(7)).has_value());
  CHECK(v.push_back(std::make_unique<int>(9)).error() == cn::container_error::full);
  CHECK(v.size() == 2);
}

TEST_CASE("nexenne::container::static_vector destroys its elements") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::static_vector<std::shared_ptr<int>, 4> v;
    v.push_back(tracker);
    v.push_back(tracker);
    CHECK(tracker.use_count() == 3);  // tracker + two stored copies
    v.clear();
    CHECK(tracker.use_count() == 1);  // clear destroyed both
    v.push_back(tracker);
  }
  CHECK(tracker.use_count() == 1);  // destructor destroyed the last
}

TEST_CASE("nexenne::container::static_vector with zero capacity is always full") {
  cn::static_vector<int, 0> z;
  CHECK(z.empty());
  CHECK(z.full());
  CHECK(z.push_back(1).error() == cn::container_error::full);
}

namespace {
// Counts how it is constructed, to prove push_back picks the cheapest overload.
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

TEST_CASE("nexenne::container::static_vector push_back picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::static_vector<move_counter, 4> v;
  move_counter c{&copies, &moves};

  CHECK(v.push_back(c).has_value());  // lvalue: exactly one copy, no move
  CHECK(copies == 1);
  CHECK(moves == 0);

  CHECK(v.push_back(std::move(c)).has_value());  // rvalue: exactly one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

}  // namespace
