/**
 * @file
 * @brief Tests for nexenne::container::bag.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <nexenne/container/bag.hpp>

namespace {

namespace cn = nexenne::container;
using bag_t = cn::bag<int>;

// bag is usable in a constant expression.
static_assert([] {
  bag_t b;
  b.insert(1);
  b.insert(2);
  b.insert(3);
  auto const r{b.erase_at(0)};  // swap-pop: 3 fills slot 0
  return r.has_value() && b.size() == 2;
}());

TEST_CASE("nexenne::container::bag insert, emplace, size") {
  bag_t b;
  CHECK(b.empty());
  b.insert(1);
  b.emplace(2);
  b.insert(3);
  CHECK(b.size() == 3);
  CHECK_FALSE(b.empty());
}

TEST_CASE("nexenne::container::bag erase_at swap-pop and out_of_range") {
  bag_t b{10, 20, 30, 40};
  CHECK(b.erase_at(1).has_value());  // remove 20; 40 fills the slot
  CHECK(b.size() == 3);
  CHECK(std::ranges::find(b, 20) == b.end());  // gone
  CHECK(std::ranges::find(b, 40) != b.end());  // still present
  CHECK(b.erase_at(10).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::bag erase_first") {
  bag_t b{1, 2, 2, 3};
  CHECK(b.erase_first(2));
  CHECK(b.size() == 3);
  CHECK_FALSE(b.erase_first(99));  // absent
}

TEST_CASE("nexenne::container::bag erase_all removes every occurrence") {
  bag_t b{1, 2, 1, 3, 1};
  CHECK(b.erase_all(1) == 3);
  CHECK(b.size() == 2);
  CHECK(std::ranges::find(b, 1) == b.end());
  CHECK(b.erase_all(99) == 0);
}

TEST_CASE("nexenne::container::bag indexed access, data, span") {
  bag_t b{5, 6, 7};
  b[0] = 50;
  CHECK(b[0] == 50);
  CHECK(b.data()[1] == 6);
  CHECK(b.span().size() == 3);
}

TEST_CASE("nexenne::container::bag iteration forward and reverse") {
  bag_t b{1, 2, 3};
  int sum{0};
  for (int const x : b) {
    sum += x;
  }
  CHECK(sum == 6);
  std::vector<int> const reversed(b.rbegin(), b.rend());
  CHECK(reversed == std::vector{3, 2, 1});
}

TEST_CASE("nexenne::container::bag copy is deep, move works (rule of zero)") {
  bag_t a{1, 2, 3};
  bag_t b{a};
  CHECK(b.size() == 3);
  a[0] = 99;
  CHECK(b[0] == 1);  // independent

  bag_t c{std::move(a)};
  CHECK(c.size() == 3);
}

TEST_CASE("nexenne::container::bag swap and assign") {
  bag_t a{1, 2};
  bag_t b{7, 8, 9};
  swap(a, b);
  CHECK(a.size() == 3);
  CHECK(b.size() == 2);

  a.assign({1});
  CHECK(a.size() == 1);
  a.assign(std::size_t{3}, 5);
  CHECK(a.size() == 3);
  CHECK(a[2] == 5);
}

TEST_CASE("nexenne::container::bag CTAD from an iterator pair") {
  std::array<int, 3> const src{1, 2, 3};
  cn::bag b(src.begin(), src.end());  // deduces bag<int>
  CHECK(b.size() == 3);
}

TEST_CASE("nexenne::container::bag holds a move-only type") {
  cn::bag<std::unique_ptr<int>> b;
  b.insert(std::make_unique<int>(5));
  b.emplace(std::make_unique<int>(6));
  CHECK(b.size() == 2);
  CHECK(*b[0] == 5);
  CHECK(b.erase_at(0).has_value());  // swap-pop moves, works for move-only
  CHECK(b.size() == 1);
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

TEST_CASE("nexenne::container::bag insert picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::bag<move_counter> b;
  b.reserve(4);  // avoid a reallocation move confounding the counts
  move_counter c{&copies, &moves};

  b.insert(c);  // lvalue: one copy
  CHECK(copies == 1);
  CHECK(moves == 0);

  b.insert(std::move(c));  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::bag erase the actual last element (swap_pop no-move branch)") {
  bag_t single{42};
  CHECK(single.erase_at(0).has_value());  // index == last: pop without a self-move
  CHECK(single.empty());

  bag_t tail{1, 2, 3};
  CHECK(tail.erase_at(2).has_value());  // remove the real last element
  CHECK(tail.size() == 2);

  bag_t same{7, 7, 7};
  CHECK(same.erase_all(7) == 3);  // every iteration removes the current last
  CHECK(same.empty());
}

}  // namespace
