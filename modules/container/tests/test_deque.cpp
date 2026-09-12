/**
 * @file
 * @brief Tests for nexenne::container::deque.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <iterator>
#include <limits>
#include <memory>
#include <random>
#include <ranges>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

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
  CHECK((reserved.capacity() & (reserved.capacity() - 1)) == 0);
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
  d.push_front(0);
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
  CHECK(d.size() == 2);
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
  back = 8;
  REQUIRE(d.back() != nullptr);
  CHECK(*d.back() == 8);

  auto& front{d.emplace_front(1)};
  CHECK(front == 1);
  CHECK(*d.front() == 1);
}

TEST_CASE("nexenne::container::deque growth preserves order") {
  dq d;
  for (int i{0}; i < 16; ++i) {
    d.push_back(i);
  }
  CHECK(d.size() == 16);
  for (int i{0}; i < 16; ++i) {
    CHECK(d[static_cast<std::size_t>(i)] == i);
  }
}

TEST_CASE("nexenne::container::deque front pushes wrap the ring") {
  dq d(8);
  for (int i{0}; i < 8; ++i) {
    d.push_front(i);
  }
  CHECK(d.size() == 8);
  for (int i{0}; i < 8; ++i) {
    CHECK(d[static_cast<std::size_t>(i)] == 7 - i);
  }
}

TEST_CASE("nexenne::container::deque reserve and clear") {
  dq d{1, 2, 3};
  d.reserve(100);
  CHECK(d.capacity() >= 100);
  CHECK(d.size() == 3);
  CHECK(d[0] == 1);

  d.clear();
  CHECK(d.empty());
  CHECK(d.capacity() >= 100);
}

TEST_CASE("nexenne::container::deque copy is deep and independent") {
  dq a{1, 2, 3};
  dq b{a};
  CHECK(b.size() == 3);
  CHECK(b[0] == 1);
  a[0] = 99;
  CHECK(b[0] == 1);
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
  b = a;
  CHECK(b.size() == 3);
  CHECK(b[2] == 3);
  a[0] = 99;
  CHECK(b[0] == 1);

  dq c;
  c = std::move(b);
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
  d.reserve(4);
  move_counter c{&copies, &moves};

  d.push_back(c);
  CHECK(copies == 1);
  CHECK(moves == 0);

  d.push_back(std::move(c));
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::deque self-referential push at capacity stays valid") {
  cn::deque<int> back_d;
  for (int i = 0; i < 8; ++i) {
    back_d.push_back(i);
  }
  REQUIRE(back_d.size() == back_d.capacity());
  back_d.push_back(back_d[0]);
  CHECK(*back_d.back() == 0);

  cn::deque<int> front_d;
  for (int i = 0; i < 8; ++i) {
    front_d.push_back(i);
  }
  REQUIRE(front_d.size() == front_d.capacity());
  front_d.push_front(front_d[front_d.size() - 1]);
  CHECK(*front_d.front() == 7);
}

TEST_CASE("nexenne::container::deque single-element life cycle at both ends") {
  dq d;
  d.push_back(1);
  CHECK(d.size() == 1);
  REQUIRE(d.front() != nullptr);
  REQUIRE(d.back() != nullptr);
  CHECK(d.front() == d.back());
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
  CHECK(d.size() == 12);
  CHECK(d[0] == -5);
  CHECK(d[11] == 5);
  while (!d.empty()) {
    REQUIRE(d.pop_front().has_value());
    if (!d.empty()) {
      REQUIRE(d.pop_back().has_value());
    }
  }
  CHECK(d.empty());
  CHECK(d.capacity() > 0);
  d.push_back(42);
  CHECK(d.size() == 1);
  CHECK(*d.front() == 42);
}

TEST_CASE("nexenne::container::deque grows correctly when the ring wraps before reallocation") {
  dq d(8);
  for (int i{0}; i < 4; ++i) {
    d.push_back(i);
  }
  for (int i{0}; i < 4; ++i) {
    REQUIRE(d.pop_front().has_value());
  }
  for (int i{0}; i < 8; ++i) {
    d.push_back(i + 10);
  }
  REQUIRE(d.size() == d.capacity());
  d.push_back(99);
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
  d = alias;
  CHECK(d.size() == 3);
  CHECK(d[0] == 1);
  CHECK(d[2] == 3);

  d = std::move(alias);
  CHECK(d.size() == 3);
  CHECK(d[1] == 2);
}

TEST_CASE("nexenne::container::deque holds a non-trivial std::string element") {
  cn::deque<std::string> d;
  d.push_back(std::string(64, 'x'));
  std::string lvalue(48, 'y');
  d.push_front(lvalue);
  CHECK(lvalue.size() == 48);
  d.emplace_back(std::string(32, 'z'));
  CHECK(d.size() == 3);
  CHECK(d[0].size() == 48);
  CHECK(d[1].size() == 64);
  CHECK(d[2].size() == 32);

  for (int i{0}; i < 32; ++i) {
    d.push_back(std::string(8, static_cast<char>('a' + (i % 26))));
  }
  auto const popped{d.pop_front()};
  REQUIRE(popped.has_value());
  CHECK(popped->size() == 48);

  cn::deque<std::string> copy{d};
  CHECK(copy.size() == d.size());
  copy.clear();
  CHECK(copy.empty());
}

TEST_CASE("nexenne::container::deque self-aliasing push of a std::string at capacity") {
  cn::deque<std::string> d;
  for (int i{0}; i < 8; ++i) {
    d.push_back(std::string(20, static_cast<char>('a' + i)));
  }
  REQUIRE(d.size() == d.capacity());
  d.push_back(d[0]);
  REQUIRE(d.back() != nullptr);
  CHECK(*d.back() == std::string(20, 'a'));
  CHECK(d[0] == std::string(20, 'a'));
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

TEST_CASE("nexenne::container::deque emplace cold path matches construct_at") {
  cn::deque<std::vector<int>> cold;
  auto& a{cold.emplace_back(3, 5)};
  CHECK(a.size() == 3);
  CHECK(a[0] == 5);

  cn::deque<std::vector<int>> hot(8);
  auto& b{hot.emplace_back(3, 5)};
  CHECK(b.size() == 3);
  CHECK(a == b);

  cn::deque<std::vector<int>> cold_front;
  auto& c{cold_front.emplace_front(3, 5)};
  CHECK(c.size() == 3);
  CHECK(c[0] == 5);
}

TEST_CASE("nexenne::container::deque iteration, equality, and checked at") {
  static_assert(std::random_access_iterator<cn::deque<int>::iterator>);
  static_assert(std::random_access_iterator<cn::deque<int>::const_iterator>);

  cn::deque<int> d;
  d.push_back(1);
  d.push_back(2);
  d.push_front(0);

  std::vector<int> seen;
  for (auto const x : d) {
    seen.push_back(x);
  }
  CHECK(seen == std::vector<int>{0, 1, 2});
  CHECK(std::ranges::equal(d, std::vector<int>{0, 1, 2}));

  cn::deque<int> e;
  e.push_back(0);
  e.push_back(1);
  e.push_back(2);
  CHECK(d == e);
  e.push_back(3);
  CHECK(d != e);
  CHECK(d < e);

  auto const in{d.at(1)};
  REQUIRE(in.has_value());
  CHECK(**in == 1);
  auto const out{d.at(3)};
  REQUIRE(!out.has_value());
  CHECK(out.error() == cn::container_error::out_of_range);

  cn::deque<int> const& cd{d};
  auto const cin{cd.at(0)};
  REQUIRE(cin.has_value());
  CHECK(**cin == 0);
}

TEST_CASE("nexenne::container::deque max_size is a non-wrapping bound") {
  constexpr auto max_index{std::numeric_limits<std::size_t>::max()};
  constexpr auto power_of_two_cap{std::size_t{1} << 63};
  // sizeof == 1: the power-of-two capacity limit wins.
  CHECK(cn::deque<std::uint8_t>::max_size() == power_of_two_cap);
  // sizeof == 8: the byte-count limit (SIZE_MAX / 8) wins and is below 2^63.
  CHECK(cn::deque<std::uint64_t>::max_size() == max_index / sizeof(std::uint64_t));
}

}  // namespace
