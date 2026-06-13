/**
 * @file
 * @brief Tests for nexenne::container::small_vector.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include <nexenne/container/small_vector.hpp>

namespace {

namespace cn = nexenne::container;
using sv = cn::small_vector<int, 4>;

static_assert(sv::inline_capacity() == 4);

TEST_CASE("nexenne::container::small_vector push_back of an existing element survives regrow") {
  cn::small_vector<std::string, 1> v;
  std::string const seed{"a string long enough to force a heap allocation, well past SSO"};
  v.push_back(seed);
  for (int i{0}; i < 8; ++i) {
    v.push_back(v[0]);  // aliases element 0; later iterations trigger heap->heap regrow
  }
  CHECK(v.size() == 9);
  for (auto const& s : v) {
    CHECK(s == seed);
  }
}

TEST_CASE("nexenne::container::small_vector stays inline up to N, then heap") {
  sv v;
  CHECK(v.is_inline());
  CHECK(v.capacity() == 4);
  for (int i{0}; i < 4; ++i) {
    v.push_back(i);
  }
  CHECK(v.is_inline());  // still inline at exactly N
  CHECK(v.size() == 4);

  v.push_back(4);  // grows past N
  CHECK_FALSE(v.is_inline());
  CHECK(v.capacity() >= 5);
  CHECK(v.size() == 5);
  CHECK(v[4] == 4);
}

TEST_CASE("nexenne::container::small_vector pop_back and empty error") {
  sv v{1, 2};
  CHECK(v.pop_back().has_value());
  CHECK(v.pop_back().has_value());
  CHECK(v.pop_back().error() == cn::container_error::empty);
}

TEST_CASE("nexenne::container::small_vector emplace_back returns the new element") {
  sv v;
  auto& slot{v.emplace_back(42)};
  CHECK(slot == 42);
  slot = 7;
  CHECK(v[0] == 7);
}

TEST_CASE("nexenne::container::small_vector at/front/back bounds-checked") {
  sv v{10, 20};
  CHECK(*v.at(1) == 20);
  CHECK(v.at(2) == nullptr);
  CHECK(*v.front() == 10);
  CHECK(*v.back() == 20);

  sv empty;
  CHECK(empty.front() == nullptr);
  CHECK(empty.back() == nullptr);
}

TEST_CASE("nexenne::container::small_vector iteration and span") {
  sv v{1, 2, 3};
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
  CHECK(v.span().size() == 3);
  int sum{0};
  for (int const x : v) {
    sum += x;
  }
  CHECK(sum == 6);
}

TEST_CASE("nexenne::container::small_vector copy is independent (heap)") {
  sv a{1, 2, 3, 4, 5};  // 5 > 4 so on the heap
  CHECK_FALSE(a.is_inline());
  sv b{a};
  CHECK(b == a);
  a[0] = 99;
  CHECK(b[0] == 1);  // deep copy
}

TEST_CASE("nexenne::container::small_vector move steals the heap allocation") {
  sv a{1, 2, 3, 4, 5};  // heap
  auto const* const data_before{a.data()};
  sv b{std::move(a)};
  CHECK(b.size() == 5);
  CHECK(b.data() == data_before);  // stole the block, no reallocation
  CHECK(a.empty());
  CHECK(a.is_inline());  // source reset to inline
}

TEST_CASE("nexenne::container::small_vector move of inline contents") {
  sv a{1, 2};  // inline
  sv b{std::move(a)};
  CHECK(b.size() == 2);
  CHECK(b[1] == 2);
  CHECK(a.empty());
}

TEST_CASE("nexenne::container::small_vector copy and move assignment") {
  sv const a{1, 2, 3, 4, 5};
  sv b;
  b = a;
  CHECK(b == a);

  sv source{1, 2, 3, 4, 5};
  sv c;
  c = std::move(source);
  CHECK(c.size() == 5);
  CHECK(source.empty());
}

TEST_CASE("nexenne::container::small_vector swap mixes inline and heap") {
  sv a{1, 2};              // inline
  sv b{1, 2, 3, 4, 5, 6};  // heap
  swap(a, b);
  CHECK(a.size() == 6);
  CHECK_FALSE(a.is_inline());
  CHECK(b.size() == 2);
  CHECK(b[0] == 1);
}

TEST_CASE("nexenne::container::small_vector reserve then shrink_to_fit back to inline") {
  sv v{1, 2, 3};
  v.reserve(100);
  CHECK(v.capacity() >= 100);
  CHECK_FALSE(v.is_inline());

  v.shrink_to_fit();  // 3 <= N, migrates back inline
  CHECK(v.is_inline());
  CHECK(v.capacity() == 4);
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
}

TEST_CASE("nexenne::container::small_vector shrink_to_fit to exact heap size") {
  sv v;
  for (int i{0}; i < 10; ++i) {
    v.push_back(i);
  }
  v.reserve(64);
  CHECK(v.capacity() >= 64);
  v.shrink_to_fit();
  CHECK(v.capacity() == 10);  // exact, still heap (> N)
  CHECK_FALSE(v.is_inline());
}

TEST_CASE("nexenne::container::small_vector assign overloads") {
  sv v{9, 9};
  v.assign({1, 2, 3});
  CHECK(std::ranges::equal(v, std::array{1, 2, 3}));
  v.assign(std::size_t{4}, 7);
  CHECK(v.size() == 4);
  CHECK(v[3] == 7);
  std::array<int, 2> const src{5, 6};
  v.assign(src.begin(), src.end());
  CHECK(std::ranges::equal(v, std::array{5, 6}));
}

TEST_CASE("nexenne::container::small_vector comparison") {
  CHECK(sv{1, 2, 3} == sv{1, 2, 3});
  CHECK(sv{1, 2} != sv{1, 2, 3});
  CHECK(sv{1, 2, 3} < sv{1, 2, 4});
}

TEST_CASE("nexenne::container::small_vector holds a move-only type across growth") {
  cn::small_vector<std::unique_ptr<int>, 2> v;
  for (int i{0}; i < 5; ++i) {
    v.push_back(std::make_unique<int>(i));  // grows to heap, moving elements
  }
  CHECK(v.size() == 5);
  CHECK(*v[4] == 4);
}

TEST_CASE("nexenne::container::small_vector destroys elements and frees heap") {
  auto tracker{std::make_shared<int>(0)};
  {
    cn::small_vector<std::shared_ptr<int>, 2> v;
    v.push_back(tracker);
    v.push_back(tracker);
    v.push_back(tracker);             // grows to heap, moving the first two
    CHECK(tracker.use_count() == 4);  // tracker + three stored
  }
  CHECK(tracker.use_count() == 1);  // destructor freed heap and destroyed all
}

namespace {
// Counts construction to prove push_back picks the cheapest overload.
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

TEST_CASE("nexenne::container::small_vector push_back picks copy vs move (no extra move)") {
  int copies{0};
  int moves{0};
  cn::small_vector<move_counter, 4> v;
  move_counter c{&copies, &moves};

  v.push_back(c);  // lvalue: one copy, no move
  CHECK(copies == 1);
  CHECK(moves == 0);

  v.push_back(std::move(c));  // rvalue: one move
  CHECK(copies == 1);
  CHECK(moves == 1);
}

TEST_CASE("nexenne::container::small_vector with zero inline capacity uses heap at once") {
  cn::small_vector<int, 0> v;
  CHECK(v.capacity() == 0);
  v.push_back(1);
  CHECK_FALSE(v.is_inline());
  CHECK(v[0] == 1);
  CHECK(v.size() == 1);
}

}  // namespace
