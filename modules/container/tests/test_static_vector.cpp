/**
 * @file
 * @brief Tests for nexenne::container::static_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
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

// max_size() mirrors capacity() and is a constant expression.
static_assert(vec::max_size() == 4);

// clear, swap, and the checked accessors all work at compile time.
static_assert([] {
  vec v{1, 2, 3};
  v.clear();
  return v.empty() && v.size() == 0 && v.front() == nullptr && v.back() == nullptr
         && v.at(0) == nullptr;
}());
static_assert([] {
  vec a{1, 2};
  vec b{3, 4, 5};
  a.swap(b);
  return a.size() == 3 && a[0] == 3 && b.size() == 2 && b[1] == 2;
}());

// Self copy-assignment and self move-assignment are well-behaved at compile time.
static_assert([] {
  vec v{1, 2, 3};
  auto* const self{&v};
  v = *self;  // self-assignment through a pointer; not flagged by the compiler
  return v.size() == 3 && v[0] == 1 && v[2] == 3;
}());

// Self-aliasing push_back/emplace_back keep a valid value at compile time.
static_assert([] {
  vec v{1, 2};
  v.push_back(v[0]);
  v.emplace_back(*v.front());
  return v.size() == 4 && v[2] == 1 && v[3] == 1;
}());

// The full three-way ordering surface is constexpr.
static_assert([] {
  vec const a{1, 2, 3};
  vec const b{1, 2, 4};
  vec const c{1, 2, 3};
  return a < b && b > a && a <= c && a >= c && (a <=> c) == std::strong_ordering::equal;
}());

// A move-only element type is fully usable in a constant expression in C++23.
static_assert([] {
  cn::static_vector<std::unique_ptr<int>, 3> v;
  v.emplace_back(std::make_unique<int>(7));
  v.push_back(std::make_unique<int>(8));
  auto const ok{*v[0] == 7 && *v[1] == 8 && v.size() == 2};
  v.pop_back();
  return ok && v.size() == 1;
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

TEST_CASE("nexenne::container::static_vector single-element boundary") {
  vec v;
  CHECK(v.push_back(42).has_value());
  CHECK(v.size() == 1);
  CHECK_FALSE(v.empty());
  CHECK_FALSE(v.full());
  CHECK(*v.front() == 42);
  CHECK(*v.back() == 42);
  CHECK(v.front() == v.back());  // the only element is both ends
  CHECK(v.pop_back().has_value());
  CHECK(v.empty());
  CHECK(v.front() == nullptr);
}

TEST_CASE("nexenne::container::static_vector self-aliasing push_back and emplace_back") {
  vec v{1, 2, 3};
  CHECK(v.push_back(v[0]).has_value());  // append an element that aliases live storage
  CHECK(v.size() == 4);
  CHECK(v[3] == 1);
  CHECK(v[0] == 1);  // source slot untouched

  vec w{5, 6};
  CHECK(w.emplace_back(*w.front()).has_value());
  CHECK(w.size() == 3);
  CHECK(w[2] == 5);
  CHECK(w[0] == 5);
}

TEST_CASE("nexenne::container::static_vector self copy-assignment is a no-op") {
  vec v{1, 2, 3};
  auto* const self{&v};
  v = *self;  // self-assignment through a pointer; not flagged by the compiler
  CHECK(v.size() == 3);
  CHECK(v[0] == 1);
  CHECK(v[1] == 2);
  CHECK(v[2] == 3);
}

TEST_CASE("nexenne::container::static_vector self move-assignment leaves a valid value") {
  vec v{1, 2, 3};
  auto* const self{&v};
  v = std::move(*self);  // self move-assign, laundered past -Wself-move
  // Pass-by-value operator= means the rhs is a temporary copy, then swapped back:
  // the object survives intact rather than self-clobbering.
  CHECK(v.size() == 3);
  CHECK(v[0] == 1);
  CHECK(v[2] == 3);
}

TEST_CASE("nexenne::container::static_vector self swap is a no-op") {
  vec v{1, 2, 3};
  v.swap(v);
  CHECK(v.size() == 3);
  CHECK(v[0] == 1);
  CHECK(v[2] == 3);
  swap(v, v);
  CHECK(v.size() == 3);
  CHECK(v[1] == 2);
}

TEST_CASE("nexenne::container::static_vector moved-from is valid and reusable") {
  vec a{1, 2, 3};
  vec const b{std::move(a)};
  CHECK(b.size() == 3);
  CHECK(a.empty());  // moved-from is emptied

  // Reuse the moved-from object: refill it and operate normally.
  CHECK(a.push_back(7).has_value());
  CHECK(a.push_back(8).has_value());
  CHECK(a.size() == 2);
  CHECK(a[0] == 7);
  CHECK(a[1] == 8);

  // Same for a move-assignment source.
  vec c{4, 5};
  vec d;
  d = std::move(c);
  CHECK(c.empty());
  CHECK(c.emplace_back(99).has_value());
  CHECK(c.size() == 1);
  CHECK(c[0] == 99);
}

TEST_CASE("nexenne::container::static_vector const accessors and iterators") {
  vec const v{1, 2, 3};
  CHECK(v[0] == 1);      // const operator[]
  CHECK(*v.at(2) == 3);  // const at
  CHECK(v.at(3) == nullptr);
  CHECK(*v.front() == 1);       // const front
  CHECK(*v.back() == 3);        // const back
  CHECK(v.data()[1] == 2);      // const data
  CHECK(v.span().size() == 3);  // const span

  // const begin/end and cbegin/cend yield const_iterators over the live range.
  static_assert(std::is_same_v<decltype(v.begin()), vec::const_iterator>);
  static_assert(std::is_same_v<decltype(v.cbegin()), vec::const_iterator>);
  int sum{0};
  for (auto it{v.cbegin()}; it != v.cend(); ++it) {
    sum += *it;
  }
  CHECK(sum == 6);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));

  // const reverse iterators, including the crbegin/crend aliases.
  std::vector<int> rev;
  for (auto it{v.crbegin()}; it != v.crend(); ++it) {
    rev.push_back(*it);
  }
  CHECK(rev == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::container::static_vector data and span over live range only") {
  vec v{1, 2, 3, 4};
  v.pop_back();
  CHECK(v.span().size() == 3);  // span covers size(), not capacity
  v.span()[0] = 100;            // writes through the span
  CHECK(v[0] == 100);
  CHECK(v.data() == &v[0]);
  CHECK(v.data() + v.size() == v.end());

  vec empty;
  CHECK(empty.span().empty());
  CHECK(empty.begin() == empty.end());
}

TEST_CASE("nexenne::container::static_vector full three-way ordering surface") {
  CHECK(vec{1, 2, 4} > vec{1, 2, 3});
  CHECK(vec{1, 2, 3} <= vec{1, 2, 3});
  CHECK(vec{1, 2, 3} >= vec{1, 2, 3});
  CHECK(vec{1, 2, 3} <= vec{1, 2, 4});
  CHECK(vec{1, 2, 3, 4} >= vec{1, 2, 3});  // longer with equal prefix is greater
  CHECK((vec{1, 2, 3} <=> vec{1, 2, 3}) == std::strong_ordering::equal);
  CHECK((vec{1, 2} <=> vec{1, 2, 3}) == std::strong_ordering::less);

  vec const e1;
  vec const e2;
  CHECK(e1 == e2);  // two empties compare equal
  CHECK_FALSE(e1 != e2);
  CHECK_FALSE(e1 < e2);
  CHECK(e1 <= e2);
}

TEST_CASE("nexenne::container::static_vector at exactly capacity then one past") {
  vec v{1, 2, 3, 4};
  REQUIRE(v.full());
  CHECK(v.size() == v.capacity());
  CHECK(v.push_back(5).error() == cn::container_error::full);     // one past via copy
  CHECK(v.push_back(6).error() == cn::container_error::full);     // one past via move
  CHECK(v.emplace_back(7).error() == cn::container_error::full);  // one past via emplace
  CHECK(v.size() == 4);  // every rejection left the vector unchanged
  CHECK(v[3] == 4);
}

TEST_CASE("nexenne::container::static_vector holds a non-trivial std::string element") {
  cn::static_vector<std::string, 3> v;
  std::string const s{"this string is long enough to force a heap allocation"};
  CHECK(v.push_back(s).has_value());  // copy a heap-owning value in
  CHECK(v[0] == s);
  CHECK(s.size() > 0);  // source survived the copy
  CHECK(v.emplace_back("emplaced").has_value());
  CHECK(v.push_back(std::string{"moved"}).has_value());  // move a temporary in
  CHECK(v.full());
  CHECK(v.push_back("rejected").error() == cn::container_error::full);
  CHECK(v[2] == "moved");

  // Self-aliasing append of a heap-owning element must not leave a dangling copy.
  v.clear();
  CHECK(v.push_back(std::string{"alias-me"}).has_value());
  CHECK(v.push_back(v[0]).has_value());
  CHECK(v[0] == "alias-me");
  CHECK(v[1] == "alias-me");

  // Copy and move of a string-holding vector exercise per-element copy/move.
  cn::static_vector<std::string, 3> const copy{v};
  CHECK(copy[1] == "alias-me");
  cn::static_vector<std::string, 3> moved{v};
  cn::static_vector<std::string, 3> sink{std::move(moved)};
  CHECK(sink[0] == "alias-me");
  CHECK(moved.empty());
}

TEST_CASE("nexenne::container::static_vector move-only element supports move-assign and swap") {
  cn::static_vector<std::unique_ptr<int>, 3> a;
  a.emplace_back(std::make_unique<int>(1));
  a.emplace_back(std::make_unique<int>(2));

  cn::static_vector<std::unique_ptr<int>, 3> b;
  b.emplace_back(std::make_unique<int>(9));

  a.swap(b);
  CHECK(a.size() == 1);
  CHECK(*a[0] == 9);
  CHECK(b.size() == 2);
  CHECK(*b[0] == 1);
  CHECK(*b[1] == 2);

  cn::static_vector<std::unique_ptr<int>, 3> c;
  c = std::move(b);  // move-assign a move-only payload
  CHECK(c.size() == 2);
  CHECK(*c[1] == 2);
  CHECK(b.empty());
}

}  // namespace
