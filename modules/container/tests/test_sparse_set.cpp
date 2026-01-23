/**
 * @file
 * @brief Tests for nexenne::container::sparse_set.
 */

#include <doctest/doctest.h>

#include <cstdint>
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

}  // namespace
