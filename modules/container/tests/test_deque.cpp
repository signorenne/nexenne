/**
 * @file
 * @brief Tests for nexenne::container::deque.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <deque>
#include <memory>
#include <random>
#include <string>
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

TEST_CASE("nexenne::container::deque self-referential push at capacity stays valid") {
  // A push that grows must materialize its argument before freeing the old
  // buffer; otherwise an argument aliasing an existing element dangles (asan
  // would trap, and the value would be garbage).
  cn::deque<int> back_d;
  for (int i = 0; i < 8; ++i) {
    back_d.push_back(i);
  }
  REQUIRE(back_d.size() == back_d.capacity());  // full: next push_back reallocates
  back_d.push_back(back_d[0]);                  // argument aliases element 0
  CHECK(*back_d.back() == 0);

  cn::deque<int> front_d;
  for (int i = 0; i < 8; ++i) {
    front_d.push_back(i);
  }
  REQUIRE(front_d.size() == front_d.capacity());    // full: next push_front reallocates
  front_d.push_front(front_d[front_d.size() - 1]);  // argument aliases the last element
  CHECK(*front_d.front() == 7);
}

TEST_CASE("nexenne::container::deque single-element life cycle at both ends") {
  dq d;
  d.push_back(1);
  CHECK(d.size() == 1);
  REQUIRE(d.front() != nullptr);
  REQUIRE(d.back() != nullptr);
  CHECK(d.front() == d.back());  // the lone element is both ends
  auto const back{d.pop_back()};
  REQUIRE(back.has_value());
  CHECK(*back == 1);
  CHECK(d.empty());
  CHECK(d.front() == nullptr);
  CHECK(d.back() == nullptr);

  d.push_front(2);
  auto const front{d.pop_front()};
  REQUIRE(front.has_value());
  CHECK(*front == 2);
  CHECK(d.empty());
}

TEST_CASE("nexenne::container::deque interleaved both-ends drain to empty and refill") {
  dq d;
  for (int i{0}; i < 6; ++i) {
    d.push_back(i);
    d.push_front(-i);
  }
  // [-5, -4, -3, -2, -1, 0, 0, 1, 2, 3, 4, 5]
  CHECK(d.size() == 12);
  CHECK(d[0] == -5);
  CHECK(d[11] == 5);
  while (!d.empty()) {  // alternately drain both ends to empty
    REQUIRE(d.pop_front().has_value());
    if (!d.empty()) {
      REQUIRE(d.pop_back().has_value());
    }
  }
  CHECK(d.empty());
  CHECK(d.capacity() > 0);  // capacity retained across full drain
  d.push_back(42);          // head/size reset, still usable
  CHECK(d.size() == 1);
  CHECK(*d.front() == 42);
}

TEST_CASE("nexenne::container::deque grows correctly when the ring wraps before reallocation") {
  // Fill to capacity with the live window straddling the physical wrap, then push
  // to force a grow that must re-pack from the (wrapped) front.
  dq d(8);
  for (int i{0}; i < 4; ++i) {
    d.push_back(i);  // [0, 1, 2, 3] at slots 0..3
  }
  for (int i{0}; i < 4; ++i) {
    REQUIRE(d.pop_front().has_value());  // head advances to slot 4
  }
  for (int i{0}; i < 8; ++i) {
    d.push_back(i + 10);  // wraps past the physical end, fills the ring
  }
  REQUIRE(d.size() == d.capacity());
  d.push_back(99);  // grow: re-pack a wrapped ring from logical front
  CHECK(d.size() == 9);
  for (int i{0}; i < 8; ++i) {
    CHECK(d[static_cast<std::size_t>(i)] == i + 10);
  }
  CHECK(d[8] == 99);
}

TEST_CASE("nexenne::container::deque const access is read-only and correct") {
  dq const d{10, 20, 30};
  CHECK(d.size() == 3);
  CHECK_FALSE(d.empty());
  CHECK(d[0] == 10);
  CHECK(d[2] == 30);
  REQUIRE(d.front() != nullptr);
  REQUIRE(d.back() != nullptr);
  CHECK(*d.front() == 10);
  CHECK(*d.back() == 30);
  static_assert(std::is_same_v<decltype(d.front()), int const*>);
  static_assert(std::is_same_v<decltype(d.back()), int const*>);
  static_assert(std::is_same_v<decltype(d[0]), int const&>);
}

TEST_CASE("nexenne::container::deque self copy- and move-assignment are safe") {
  dq d{1, 2, 3};
  dq& alias{d};
  d = alias;  // self copy-assign (by-value param copies first, then swaps)
  CHECK(d.size() == 3);
  CHECK(d[0] == 1);
  CHECK(d[2] == 3);

  d = std::move(alias);  // self move-assign
  CHECK(d.size() == 3);
  CHECK(d[1] == 2);
}

TEST_CASE("nexenne::container::deque holds a non-trivial std::string element") {
  cn::deque<std::string> d;
  d.push_back(std::string(64, 'x'));  // heap-backed, leak-detectable
  std::string lvalue(48, 'y');
  d.push_front(lvalue);  // copy an lvalue
  CHECK(lvalue.size() == 48);
  d.emplace_back(std::string(32, 'z'));  // emplace via a single string argument
  CHECK(d.size() == 3);
  CHECK(d[0].size() == 48);
  CHECK(d[1].size() == 64);
  CHECK(d[2].size() == 32);

  for (int i{0}; i < 32; ++i) {
    d.push_back(std::string(8, static_cast<char>('a' + (i % 26))));  // force a grow
  }
  auto const popped{d.pop_front()};
  REQUIRE(popped.has_value());
  CHECK(popped->size() == 48);

  cn::deque<std::string> copy{d};  // deep copy of heap strings
  CHECK(copy.size() == d.size());
  copy.clear();
  CHECK(copy.empty());
}

TEST_CASE("nexenne::container::deque self-aliasing push of a std::string at capacity") {
  // Same UAF guard as the int case, but with a heap element so a stale pointer
  // would surface as a use-after-free under the sanitizer, not just a garbage int.
  cn::deque<std::string> d;
  for (int i{0}; i < 8; ++i) {
    d.push_back(std::string(20, static_cast<char>('a' + i)));
  }
  REQUIRE(d.size() == d.capacity());
  d.push_back(d[0]);  // copy aliases element 0 across the reallocation
  REQUIRE(d.back() != nullptr);
  CHECK(*d.back() == std::string(20, 'a'));
  CHECK(d[0] == std::string(20, 'a'));  // the source survived intact
}

TEST_CASE("nexenne::container::deque differential against std::deque under randomized ops") {
  std::mt19937 rng{12345};
  std::deque<std::string> model;
  cn::deque<std::string> subject;
  auto make_string{[&rng] {
    return std::string(1 + (rng() % 30), static_cast<char>('a' + (rng() % 26)));
  }};

  for (int step{0}; step < 4000; ++step) {
    auto const op{rng() % 4};
    if (op == 0) {
      auto const s{make_string()};
      model.push_back(s);
      subject.push_back(s);
    } else if (op == 1) {
      auto const s{make_string()};
      model.push_front(s);
      subject.push_front(s);
    } else if (op == 2) {
      if (!model.empty()) {
        auto const expected{model.back()};
        model.pop_back();
        auto const got{subject.pop_back()};
        REQUIRE(got.has_value());
        CHECK(*got == expected);
      } else {
        CHECK(subject.pop_back().error() == cn::container_error::empty);
      }
    } else {
      if (!model.empty()) {
        auto const expected{model.front()};
        model.pop_front();
        auto const got{subject.pop_front()};
        REQUIRE(got.has_value());
        CHECK(*got == expected);
      } else {
        CHECK(subject.pop_front().error() == cn::container_error::empty);
      }
    }
    REQUIRE(subject.size() == model.size());
  }
  for (std::size_t i{0}; i < model.size(); ++i) {
    REQUIRE(subject[i] == model[i]);
  }
}

}  // namespace
