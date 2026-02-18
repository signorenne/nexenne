/**
 * @file
 * @brief Tests for nexenne::container::union_find.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <random>
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

TEST_CASE("nexenne::container::union_find uniting a node with itself is a no-op") {
  uf u{4};
  auto const same{u.unite(2, 2)};
  REQUIRE(same.has_value());
  CHECK_FALSE(*same);  // already in the same (singleton) set
  CHECK(u.count() == 4);
  auto const conn{u.connected(2, 2)};
  REQUIRE(conn.has_value());
  CHECK(*conn);
}

TEST_CASE("nexenne::container::union_find union by size hangs the smaller tree under the larger") {
  uf u{5};
  static_cast<void>(u.unite(0, 1));  // {0, 1} size 2
  static_cast<void>(u.unite(0, 2));  // {0, 1, 2} size 3
  // 3 is a singleton; uniting it with the size-3 set must hang 3 under that set's
  // root, not the reverse, so 3's root becomes the larger set's root.
  auto const big_root{*u.find(0)};
  static_cast<void>(u.unite(3, 0));
  CHECK(*u.find(3) == big_root);  // 3 was hung under the bigger tree's root
  CHECK(*u.size_of(3) == 4);
  CHECK(u.count() == 2);  // {0,1,2,3} and {4}
}

TEST_CASE("nexenne::container::union_find find flattens the parent chain via path halving") {
  uf u{6};
  // Build a deliberately deep-ish chain by uniting equal-size sets so the tree is
  // not pre-flattened, then confirm find rewrites parents toward the root.
  static_cast<void>(u.unite(0, 1));
  static_cast<void>(u.unite(2, 3));
  static_cast<void>(u.unite(0, 2));  // a two-level tree forms
  static_cast<void>(u.unite(4, 5));
  static_cast<void>(u.unite(0, 4));  // join again, deepening some chains
  auto const root{*u.find(5)};       // a find that triggers compression
  // Every node resolves to the one root.
  for (std::uint32_t i{0}; i < u.size(); ++i) {
    CHECK(*u.find(i) == root);
  }
  // Path compression is observable and convergent: repeated find-all passes drive
  // every node's direct parent to the root (the array is fully flattened).
  for (int pass{0}; pass < 3; ++pass) {
    for (std::uint32_t i{0}; i < u.size(); ++i) {
      static_cast<void>(u.find(i));
    }
  }
  auto const parents{u.parents()};
  for (std::uint32_t i{0}; i < u.size(); ++i) {
    CHECK(parents[i] == root);  // direct parent is the root after full compression
  }
}

TEST_CASE("nexenne::container::union_find set_sizes records the size on the root only") {
  uf u{4};
  static_cast<void>(u.unite(0, 1));
  static_cast<void>(u.unite(0, 2));  // {0, 1, 2}, {3}
  auto const root{*u.find(0)};
  auto const sizes{u.set_sizes()};
  CHECK(sizes[root] == 3);
  std::size_t total{0};
  std::size_t nonzero{0};
  for (std::uint32_t i{0}; i < u.size(); ++i) {
    total += sizes[i];
    if (sizes[i] != 0) {
      ++nonzero;
    }
  }
  CHECK(total == 4);    // sizes sum to the node count
  CHECK(nonzero == 2);  // exactly one nonzero entry per set (2 sets here)
}

TEST_CASE("nexenne::container::union_find make_set continues an existing partition") {
  uf u{2};
  static_cast<void>(u.unite(0, 1));  // {0, 1}
  CHECK(u.count() == 1);
  auto const n{u.make_set()};  // append node 2 as a singleton
  CHECK(n == 2);
  CHECK(u.size() == 3);
  CHECK(u.count() == 2);
  CHECK_FALSE(*u.connected(0, 2));
  static_cast<void>(u.unite(1, 2));  // merge the new node in
  CHECK(*u.connected(0, 2));
  CHECK(u.count() == 1);
  CHECK(*u.size_of(2) == 3);
}

TEST_CASE("nexenne::container::union_find reserve and shrink_to_fit keep the partition") {
  uf u{3};
  static_cast<void>(u.unite(0, 1));
  u.reserve(100);
  CHECK(u.size() == 3);
  CHECK(u.count() == 2);
  CHECK(*u.connected(0, 1));
  u.shrink_to_fit();
  CHECK(u.size() == 3);
  CHECK(*u.connected(0, 1));
}

TEST_CASE("nexenne::container::union_find connected-components count matches a reference model") {
  // A naive reference: full find with no compression, recomputing the component
  // count from scratch. The subject must agree on count() after each merge.
  std::mt19937 rng{909};
  std::size_t const n{200};
  uf subject{n};
  std::vector<std::uint32_t> ref(n);
  for (std::uint32_t i{0}; i < n; ++i) {
    ref[i] = i;
  }
  auto ref_root{[&ref](std::uint32_t i) {
    while (ref[i] != i) {
      i = ref[i];
    }
    return i;
  }};
  auto ref_count{[&] {
    std::size_t c{0};
    for (std::uint32_t i{0}; i < n; ++i) {
      if (ref_root(i) == i) {
        ++c;
      }
    }
    return c;
  }};

  for (int step{0}; step < 2000; ++step) {
    auto const a{static_cast<std::uint32_t>(rng() % n)};
    auto const b{static_cast<std::uint32_t>(rng() % n)};
    auto const ra{ref_root(a)};
    auto const rb{ref_root(b)};
    if (ra != rb) {
      ref[ra] = rb;  // arbitrary link; only the partition matters
    }
    auto const merged{subject.unite(a, b)};
    REQUIRE(merged.has_value());
    CHECK(*merged == (ra != rb));  // subject reports a merge iff the model did
    REQUIRE(subject.count() == ref_count());
    // Connectivity must agree for this pair.
    auto const conn{subject.connected(a, b)};
    REQUIRE(conn.has_value());
    CHECK(*conn);
  }

  // Final cross-check: every pair's connectivity agrees with the reference.
  for (std::uint32_t i{0}; i < n; i += 17) {
    for (std::uint32_t j{0}; j < n; j += 23) {
      auto const conn{subject.connected(i, j)};
      REQUIRE(conn.has_value());
      CHECK(*conn == (ref_root(i) == ref_root(j)));
    }
  }
}

TEST_CASE("nexenne::container::union_find find on an empty structure is out of range") {
  uf u;
  CHECK(u.empty());
  CHECK(u.find(0).error() == cn::container_error::out_of_range);
  CHECK(u.connected(0, 0).error() == cn::container_error::out_of_range);
  CHECK(u.size_of(0).error() == cn::container_error::out_of_range);
  CHECK(u.unite(0, 0).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::union_find works with 64-bit indices") {
  cn::union_find_u64 u{4};
  static_cast<void>(u.unite(0, 3));
  static_cast<void>(u.unite(1, 2));
  CHECK(u.count() == 2);
  CHECK(*u.connected(0, 3));
  CHECK_FALSE(*u.connected(0, 1));
  CHECK(*u.size_of(0) == 2);
}

}  // namespace
