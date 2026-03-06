/**
 * @file
 * @brief Tests for nexenne::container::sparse_set.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <random>
#include <set>
#include <type_traits>
#include <vector>

#include <nexenne/container/sparse_set.hpp>

namespace {

namespace cn = nexenne::container;
using set_t = cn::sparse_set_u32;

static_assert(std::is_same_v<cn::sparse_set_u32, cn::sparse_set<std::uint32_t>>);

// sparse_set is usable in a constant expression.
static_assert([] {
  set_t s;
  s.insert(5);
  s.insert(2);
  s.insert(9);
  bool ok{s.size() == 3 && s.contains(5) && !s.contains(3)};
  ok = ok && s.erase(2) && s.size() == 2 && !s.contains(2);
  return ok;
}());

TEST_CASE("nexenne::container::sparse_set insert and contains") {
  set_t s;
  CHECK(s.empty());
  CHECK(s.insert(10));
  CHECK(s.insert(20));
  CHECK_FALSE(s.insert(10));  // already present
  CHECK(s.size() == 2);
  CHECK(s.contains(10));
  CHECK(s.contains(20));
  CHECK_FALSE(s.contains(15));
}

TEST_CASE("nexenne::container::sparse_set erase via swap-pop") {
  set_t s;
  s.insert(1);
  s.insert(2);
  s.insert(3);  // dense [1, 2, 3]
  CHECK(s.erase(1));
  CHECK(s.size() == 2);
  CHECK_FALSE(s.contains(1));
  CHECK(s.contains(2));
  CHECK(s.contains(3));
  CHECK_FALSE(s.erase(99));  // absent
}

TEST_CASE("nexenne::container::sparse_set index_of, find, count") {
  set_t s;
  s.insert(5);
  s.insert(7);
  REQUIRE(s.index_of(5).has_value());
  CHECK(*s.index_of(5) == 0);
  CHECK(*s.index_of(7) == 1);
  CHECK_FALSE(s.index_of(99).has_value());
  REQUIRE(s.find(5) != s.end());
  CHECK(*s.find(5) == 5);
  CHECK(s.find(99) == s.end());
  CHECK(s.count(5) == 1);
  CHECK(s.count(99) == 0);
}

TEST_CASE("nexenne::container::sparse_set iterates the dense keys") {
  set_t s;
  s.insert(3);
  s.insert(1);
  s.insert(2);  // dense [3, 1, 2]
  std::vector<std::uint32_t> const ks(s.begin(), s.end());
  CHECK(ks == std::vector<std::uint32_t>{3, 1, 2});
  CHECK(s.keys().size() == 3);

  std::uint32_t sum{0};
  for (auto const k : s.keys()) {
    sum += k;
  }
  CHECK(sum == 6);
}

TEST_CASE("nexenne::container::sparse_set clear, reserve, key_capacity") {
  set_t s;
  s.reserve(100, 50);
  CHECK(s.key_capacity() >= 100);
  s.insert(5);
  s.insert(10);
  CHECK(s.size() == 2);
  s.clear();
  CHECK(s.empty());
  CHECK(s.key_capacity() >= 100);  // sparse capacity retained
  CHECK_FALSE(s.contains(5));
}

TEST_CASE("nexenne::container::sparse_set grows the sparse array for large keys") {
  set_t s;
  s.insert(1000);
  CHECK(s.contains(1000));
  CHECK(s.key_capacity() >= 1001);
  CHECK(s.size() == 1);
}

TEST_CASE("nexenne::container::sparse_set is ABA-safe after erase and reinsert") {
  set_t s;
  s.insert(5);  // sparse[5] = 0, dense[0] = 5
  s.erase(5);   // sparse[5] = invalid
  CHECK_FALSE(s.contains(5));
  s.insert(7);  // reuses dense slot 0: dense[0] = 7, sparse[7] = 0
  CHECK(s.contains(7));
  CHECK_FALSE(s.contains(5));  // a stale sparse entry does not read as present
}

TEST_CASE("nexenne::container::sparse_set swap") {
  set_t a;
  a.insert(1);
  a.insert(2);
  set_t b;
  b.insert(9);
  swap(a, b);
  CHECK(a.size() == 1);
  CHECK(a.contains(9));
  CHECK(b.size() == 2);
  CHECK(b.contains(1));
}

TEST_CASE("nexenne::container::sparse_set works with 16-bit keys") {
  cn::sparse_set_u16 s;
  s.insert(300);
  CHECK(s.contains(300));
  CHECK(s.size() == 1);
}

TEST_CASE("nexenne::container::sparse_set erasing the dense tail takes the no-swap path") {
  set_t s;
  s.insert(10);
  s.insert(20);
  s.insert(30);        // dense [10, 20, 30]
  CHECK(s.erase(30));  // last element: pop, no swap needed
  CHECK(s.size() == 2);
  std::vector<std::uint32_t> const ks(s.begin(), s.end());
  CHECK(ks == std::vector<std::uint32_t>{10, 20});  // order of survivors preserved
  CHECK(s.contains(10));
  CHECK(s.contains(20));
  CHECK_FALSE(s.contains(30));
}

TEST_CASE("nexenne::container::sparse_set interior erase fixes the moved key's sparse slot") {
  set_t s;
  s.insert(10);
  s.insert(20);
  s.insert(30);        // dense [10, 20, 30], sparse[30] = 2
  CHECK(s.erase(10));  // swap-pop: 30 moves into slot 0
  CHECK(s.size() == 2);
  // index_of must now report 30 at its NEW dense position, not its stale one.
  REQUIRE(s.index_of(30).has_value());
  CHECK(*s.index_of(30) == 0);
  REQUIRE(s.index_of(20).has_value());
  CHECK(*s.index_of(20) == 1);
  REQUIRE(s.find(30) != s.end());
  CHECK(*s.find(30) == 30);  // find points at the relocated key
  CHECK(s.contains(30));
  CHECK(s.contains(20));
}

TEST_CASE("nexenne::container::sparse_set key zero is a valid key") {
  set_t s;
  CHECK(s.insert(0));
  CHECK(s.contains(0));
  CHECK(s.size() == 1);
  CHECK_FALSE(s.contains(1));
  REQUIRE(s.index_of(0).has_value());
  CHECK(*s.index_of(0) == 0);
  CHECK(s.erase(0));
  CHECK_FALSE(s.contains(0));
  CHECK(s.empty());
}

TEST_CASE("nexenne::container::sparse_set erase on an empty set returns false") {
  set_t s;
  CHECK_FALSE(s.erase(0));
  CHECK_FALSE(s.erase(1000));
  CHECK(s.empty());
  CHECK(s.count(5) == 0);
}

TEST_CASE("nexenne::container::sparse_set reinsert after clear and shrink_to_fit") {
  set_t s;
  for (std::uint32_t k{0}; k < 50; ++k) {
    s.insert(k);
  }
  CHECK(s.size() == 50);
  s.clear();
  CHECK(s.empty());
  CHECK_FALSE(s.contains(0));
  CHECK_FALSE(s.contains(49));
  s.shrink_to_fit();   // releasing capacity must not corrupt the state
  CHECK(s.insert(7));  // usable after clear + shrink
  CHECK(s.contains(7));
  CHECK(s.size() == 1);
  CHECK(s.max_size() > 0);
}

TEST_CASE("nexenne::container::sparse_set handles a very large 64-bit key") {
  cn::sparse_set_u64 s;
  std::uint64_t const big{1u << 20};  // 1,048,576: forces a large sparse array
  CHECK(s.insert(big));
  CHECK(s.contains(big));
  CHECK(s.key_capacity() >= big + 1);
  CHECK(s.size() == 1);
  CHECK(s.erase(big));
  CHECK_FALSE(s.contains(big));
}

TEST_CASE("nexenne::container::sparse_set differential against std::set under randomized ops") {
  std::mt19937 rng{2024};
  std::set<std::uint32_t> model;
  set_t subject;
  for (int step{0}; step < 5000; ++step) {
    auto const k{static_cast<std::uint32_t>(rng() % 200)};
    if (rng() % 2 == 0) {
      auto const inserted{model.insert(k).second};
      CHECK(subject.insert(k) == inserted);
    } else {
      auto const erased{model.erase(k) != 0};
      CHECK(subject.erase(k) == erased);
    }
    REQUIRE(subject.size() == model.size());
    CHECK(subject.contains(k) == (model.count(k) != 0));
  }
  // Membership must agree across the whole key space.
  for (std::uint32_t k{0}; k < 200; ++k) {
    REQUIRE(subject.contains(k) == (model.count(k) != 0));
  }
  // The dense view, sorted, must equal the model exactly (no duplicates, no loss).
  std::vector<std::uint32_t> dense(subject.begin(), subject.end());
  std::sort(dense.begin(), dense.end());
  std::vector<std::uint32_t> const expected(model.begin(), model.end());
  CHECK(dense == expected);
}

TEST_CASE("nexenne::container::sparse_set rejects an unrepresentable maximum key") {
  // A key equal to size_type's maximum would need size_type + 1 sparse slots, an
  // unrepresentable capacity; it must be rejected cleanly rather than wrap the
  // capacity to zero and write out of bounds. Only reachable for the 64-bit key
  // alias (a 32-bit key can never equal a 64-bit size_type's maximum).
  auto s{cn::sparse_set_u64{}};
  auto const max_key{std::numeric_limits<std::uint64_t>::max()};
  CHECK_FALSE(s.insert(max_key));  // rejected, no out-of-bounds write
  CHECK_FALSE(s.contains(max_key));
  CHECK(s.size() == 0);
  // A large but representable key still works.
  CHECK(s.insert(std::uint64_t{1000}));
  CHECK(s.contains(std::uint64_t{1000}));
  CHECK(s.size() == 1);
}

}  // namespace
