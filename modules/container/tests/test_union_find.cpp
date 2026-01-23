/**
 * @file
 * @brief Tests for nexenne::container::union_find.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <type_traits>
#include <vector>

#include <nexenne/container/union_find.hpp>

namespace {

namespace cn = nexenne::container;
using uf = cn::union_find_u32;

static_assert(std::is_same_v<cn::union_find_u32, cn::union_find<std::uint32_t>>);

// union_find is usable in a constant expression.
static_assert([] {
  uf u{5};
  bool ok{u.count() == 5 && u.size() == 5};
  ok = ok && *u.unite(0, 1) && *u.unite(2, 3);
  ok = ok && u.count() == 3;
  ok = ok && *u.connected(0, 1) && !*u.connected(0, 2);
  return ok;
}());

TEST_CASE("nexenne::container::union_find singletons start separate") {
  uf u{5};
  CHECK(u.size() == 5);
  CHECK(u.count() == 5);
  CHECK_FALSE(u.empty());
  REQUIRE(u.find(0).has_value());
  CHECK(*u.find(0) == 0);  // each node is its own root
}

TEST_CASE("nexenne::container::union_find unite merges and drops the count") {
  uf u{5};
  auto const first{u.unite(0, 1)};
  REQUIRE(first.has_value());
  CHECK(*first);  // a real merge happened
  CHECK(u.count() == 4);

  auto const again{u.unite(0, 1)};  // already in the same set
  REQUIRE(again.has_value());
  CHECK_FALSE(*again);
  CHECK(u.count() == 4);
}

TEST_CASE("nexenne::container::union_find connected") {
  uf u{4};
  CHECK_FALSE(*u.connected(0, 3));
  static_cast<void>(u.unite(0, 1));
  static_cast<void>(u.unite(1, 3));  // 0, 1, 3 now connected
  CHECK(*u.connected(0, 3));
  CHECK_FALSE(*u.connected(0, 2));
}

TEST_CASE("nexenne::container::union_find size_of grows on union by size") {
  uf u{4};
  static_cast<void>(u.unite(0, 1));  // {0, 1}
  static_cast<void>(u.unite(2, 3));  // {2, 3}
  CHECK(*u.size_of(0) == 2);
  static_cast<void>(u.unite(0, 2));  // {0, 1, 2, 3}
  CHECK(*u.size_of(0) == 4);
  CHECK(*u.size_of(3) == 4);
}

TEST_CASE("nexenne::container::union_find rejects out-of-range indices") {
  uf u{3};
  CHECK(u.find(5).error() == cn::container_error::out_of_range);
  CHECK(u.unite(0, 5).error() == cn::container_error::out_of_range);
  CHECK(u.connected(5, 0).error() == cn::container_error::out_of_range);
  CHECK(u.size_of(9).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::union_find make_set, reset, clear") {
  uf u;
  CHECK(u.empty());
  auto const a{u.make_set()};
  auto const b{u.make_set()};
  CHECK(a == 0);
  CHECK(b == 1);
  CHECK(u.size() == 2);
  CHECK(u.count() == 2);
  static_cast<void>(u.unite(a, b));
  CHECK(u.count() == 1);

  u.reset(3);
  CHECK(u.size() == 3);
  CHECK(u.count() == 3);  // reset back to singletons

  u.clear();
  CHECK(u.empty());
  CHECK(u.count() == 0);
}

TEST_CASE("nexenne::container::union_find root_of is const and consistent") {
  uf u{4};
  static_cast<void>(u.unite(0, 1));
  static_cast<void>(u.unite(2, 3));
  static_cast<void>(u.unite(0, 2));  // all four together
  uf const& view{u};
  auto const root{view.root_of(3)};
  CHECK(view.root_of(0) == root);
  CHECK(view.root_of(1) == root);
  CHECK(view.root_of(2) == root);
}

TEST_CASE("nexenne::container::union_find parents, set_sizes, nodes views") {
  uf u{3};
  CHECK(u.parents().size() == 3);
  CHECK(u.set_sizes().size() == 3);
  std::vector<std::uint32_t> const ns(u.nodes().begin(), u.nodes().end());
  CHECK(ns == std::vector<std::uint32_t>{0, 1, 2});
}

TEST_CASE("nexenne::container::union_find same_partition ignores history and order") {
  uf a{4};
  uf b{4};
  CHECK(a.same_partition(b));  // both all singletons

  static_cast<void>(a.unite(0, 1));
  CHECK_FALSE(a.same_partition(b));
  static_cast<void>(b.unite(1, 0));  // same grouping, opposite argument order
  CHECK(a.same_partition(b));

  uf const c{5};
  CHECK_FALSE(a.same_partition(c));  // different node count
}

TEST_CASE("nexenne::container::union_find works with 16-bit indices") {
  cn::union_find_u16 u{3};
  static_cast<void>(u.unite(0, 2));
  CHECK(*u.connected(0, 2));
  CHECK(u.count() == 2);
}

}  // namespace
