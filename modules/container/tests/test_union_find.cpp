/**
 * @file
 * @brief Tests for nexenne::container::union_find.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <random>
#include <type_traits>
#include <vector>

#include <nexenne/container/union_find.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;
using uf = cn::union_find_u32;

template <typename U>
constexpr auto make_sets(U& u, std::size_t const n) -> bool {
  for (std::size_t i{0}; i < n; ++i) {
    if (!u.make_set().has_value()) {
      return false;
    }
  }
  return true;
}

static_assert(std::is_same_v<cn::union_find_u32, cn::union_find<std::uint32_t>>);

static_assert([] {
  uf u;
  bool ok{make_sets(u, 5) && u.count() == 5 && u.size() == 5};
  ok = ok && *u.unite(0, 1) && *u.unite(2, 3);
  ok = ok && u.count() == 3;
  ok = ok && *u.connected(0, 1) && !*u.connected(0, 2);
  return ok;
}());

TEST_CASE("nexenne::container::union_find singletons start separate") {
  uf u;
  REQUIRE(make_sets(u, 5));
  CHECK(u.size() == 5);
  CHECK(u.count() == 5);
  CHECK_FALSE(u.empty());
  REQUIRE(u.find(0).has_value());
  CHECK(*u.find(0) == 0);
}

TEST_CASE("nexenne::container::union_find unite merges and drops the count") {
  uf u;
  REQUIRE(make_sets(u, 5));
  auto const first{u.unite(0, 1)};
  REQUIRE(first.has_value());
  CHECK(*first);
  CHECK(u.count() == 4);

  auto const again{u.unite(0, 1)};
  REQUIRE(again.has_value());
  CHECK_FALSE(*again);
  CHECK(u.count() == 4);
}

TEST_CASE("nexenne::container::union_find connected") {
  uf u;
  REQUIRE(make_sets(u, 4));
  CHECK_FALSE(*u.connected(0, 3));
  nexenne::utility::ignore(u.unite(0, 1));
  nexenne::utility::ignore(u.unite(1, 3));
  CHECK(*u.connected(0, 3));
  CHECK_FALSE(*u.connected(0, 2));
}

TEST_CASE("nexenne::container::union_find size_of grows on union by size") {
  uf u;
  REQUIRE(make_sets(u, 4));
  nexenne::utility::ignore(u.unite(0, 1));  // {0, 1}
  nexenne::utility::ignore(u.unite(2, 3));  // {2, 3}
  CHECK(*u.size_of(0) == 2);
  nexenne::utility::ignore(u.unite(0, 2));
  CHECK(*u.size_of(0) == 4);
  CHECK(*u.size_of(3) == 4);
}

TEST_CASE("nexenne::container::union_find rejects out-of-range indices") {
  uf u;
  REQUIRE(make_sets(u, 3));
  CHECK(u.find(5).error() == cn::container_error::out_of_range);
  CHECK(u.unite(0, 5).error() == cn::container_error::out_of_range);
  CHECK(u.connected(5, 0).error() == cn::container_error::out_of_range);
  CHECK(u.size_of(9).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::union_find make_set, clear, grow again") {
  uf u;
  CHECK(u.empty());
  auto const a{*u.make_set()};
  auto const b{*u.make_set()};
  CHECK(a == 0);
  CHECK(b == 1);
  CHECK(u.size() == 2);
  CHECK(u.count() == 2);
  nexenne::utility::ignore(u.unite(a, b));
  CHECK(u.count() == 1);

  u.clear();
  REQUIRE(make_sets(u, 3));
  CHECK(u.size() == 3);
  CHECK(u.count() == 3);

  u.clear();
  CHECK(u.empty());
  CHECK(u.count() == 0);
}

TEST_CASE("nexenne::container::union_find root_of is const and consistent") {
  uf u;
  REQUIRE(make_sets(u, 4));
  nexenne::utility::ignore(u.unite(0, 1));
  nexenne::utility::ignore(u.unite(2, 3));
  nexenne::utility::ignore(u.unite(0, 2));
  uf const& view{u};
  auto const root{view.root_of(3)};
  CHECK(view.root_of(0) == root);
  CHECK(view.root_of(1) == root);
  CHECK(view.root_of(2) == root);
}

TEST_CASE("nexenne::container::union_find parents, set_sizes, nodes views") {
  uf u;
  REQUIRE(make_sets(u, 3));
  CHECK(u.parents().size() == 3);
  CHECK(u.set_sizes().size() == 3);
  std::vector<std::uint32_t> const ns(u.nodes().begin(), u.nodes().end());
  CHECK(ns == std::vector<std::uint32_t>{0, 1, 2});
}

TEST_CASE("nexenne::container::union_find same_partition ignores history and order") {
  uf a;
  REQUIRE(make_sets(a, 4));
  uf b;
  REQUIRE(make_sets(b, 4));
  CHECK(a.same_partition(b));  // both all singletons

  nexenne::utility::ignore(a.unite(0, 1));
  CHECK_FALSE(a.same_partition(b));
  nexenne::utility::ignore(b.unite(1, 0));
  CHECK(a.same_partition(b));

  uf c;

  REQUIRE(make_sets(c, 5));
  CHECK_FALSE(a.same_partition(c));  // different node count
}

TEST_CASE("nexenne::container::union_find works with 16-bit indices") {
  cn::union_find_u16 u;
  REQUIRE(make_sets(u, 3));
  nexenne::utility::ignore(u.unite(0, 2));
  CHECK(*u.connected(0, 2));
  CHECK(u.count() == 2);
}

TEST_CASE("nexenne::container::union_find uniting a node with itself is a no-op") {
  uf u;
  REQUIRE(make_sets(u, 4));
  auto const same{u.unite(2, 2)};
  REQUIRE(same.has_value());
  CHECK_FALSE(*same);
  CHECK(u.count() == 4);
  auto const conn{u.connected(2, 2)};
  REQUIRE(conn.has_value());
  CHECK(*conn);
}

TEST_CASE("nexenne::container::union_find union by size hangs the smaller tree under the larger") {
  uf u;
  REQUIRE(make_sets(u, 5));
  nexenne::utility::ignore(u.unite(0, 1));  // {0, 1} size 2
  nexenne::utility::ignore(u.unite(0, 2));  // {0, 1, 2} size 3
  // 3 is a singleton; uniting it with the size-3 set must hang 3 under that set's
  // root, not the reverse, so 3's root becomes the larger set's root.
  auto const big_root{*u.find(0)};
  nexenne::utility::ignore(u.unite(3, 0));
  CHECK(*u.find(3) == big_root);
  CHECK(*u.size_of(3) == 4);
  CHECK(u.count() == 2);
}

TEST_CASE("nexenne::container::union_find find flattens the parent chain via path halving") {
  uf u;
  REQUIRE(make_sets(u, 6));
  // Build a deliberately deep-ish chain by uniting equal-size sets so the tree is
  // not pre-flattened, then confirm find rewrites parents toward the root.
  nexenne::utility::ignore(u.unite(0, 1));
  nexenne::utility::ignore(u.unite(2, 3));
  nexenne::utility::ignore(u.unite(0, 2));
  nexenne::utility::ignore(u.unite(4, 5));
  nexenne::utility::ignore(u.unite(0, 4));  // join again, deepening some chains
  auto const root{*u.find(5)};               // a find that triggers compression
  // Every node resolves to the one root.
  for (std::uint32_t i{0}; i < u.size(); ++i) {
    CHECK(*u.find(i) == root);
  }
  for (int pass{0}; pass < 3; ++pass) {
    for (std::uint32_t i{0}; i < u.size(); ++i) {
      nexenne::utility::ignore(u.find(i));
    }
  }
  auto const parents{u.parents()};
  for (std::uint32_t i{0}; i < u.size(); ++i) {
    CHECK(parents[i] == root);
  }
}

TEST_CASE("nexenne::container::union_find set_sizes records the size on the root only") {
  uf u;
  REQUIRE(make_sets(u, 4));
  nexenne::utility::ignore(u.unite(0, 1));
  nexenne::utility::ignore(u.unite(0, 2));
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
  CHECK(total == 4);
  CHECK(nonzero == 2);
}

TEST_CASE("nexenne::container::union_find make_set continues an existing partition") {
  uf u;
  REQUIRE(make_sets(u, 2));
  nexenne::utility::ignore(u.unite(0, 1));  // {0, 1}
  CHECK(u.count() == 1);
  auto const n{u.make_set()};  // append node 2 as a singleton
  CHECK(n == 2U);
  CHECK(u.size() == 3);
  CHECK(u.count() == 2);
  CHECK_FALSE(*u.connected(0, 2));
  nexenne::utility::ignore(u.unite(1, 2));
  CHECK(*u.connected(0, 2));
  CHECK(u.count() == 1);
  CHECK(*u.size_of(2) == 3);
}

TEST_CASE("nexenne::container::union_find reserve and shrink_to_fit keep the partition") {
  uf u;
  REQUIRE(make_sets(u, 3));
  nexenne::utility::ignore(u.unite(0, 1));
  u.reserve(100);
  CHECK(u.size() == 3);
  CHECK(u.count() == 2);
  CHECK(*u.connected(0, 1));
  u.shrink_to_fit();
  CHECK(u.size() == 3);
  CHECK(*u.connected(0, 1));
}

TEST_CASE("nexenne::container::union_find connected-components count matches a reference model") {
  std::mt19937 rng{909};
  std::size_t const n{200};
  uf subject;
  REQUIRE(make_sets(subject, n));
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
      ref[ra] = rb;
    }
    auto const merged{subject.unite(a, b)};
    REQUIRE(merged.has_value());
    CHECK(*merged == (ra != rb));
    REQUIRE(subject.count() == ref_count());
    auto const conn{subject.connected(a, b)};
    REQUIRE(conn.has_value());
    CHECK(*conn);
  }

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
  cn::union_find_u64 u;
  REQUIRE(make_sets(u, 4));
  nexenne::utility::ignore(u.unite(0, 3));
  nexenne::utility::ignore(u.unite(1, 2));
  CHECK(u.count() == 2);
  CHECK(*u.connected(0, 3));
  CHECK_FALSE(*u.connected(0, 1));
  CHECK(*u.size_of(0) == 2);
}

TEST_CASE("nexenne::container::union_find over a small Index keeps node ids in range") {
  // M7: past 2^N nodes the index cast used to wrap (aliasing low indices and
  // breaking the partition invariants). Within the representable range a small
  // Index type must track nodes and merge them correctly. Growth past the id
  // space reports full.
  cn::union_find<std::uint8_t> u;  // well inside uint8_t, no wrap
  REQUIRE(make_sets(u, 200));
  CHECK(u.size() == 200);
  CHECK(u.count() == 200);

  for (int i{1}; i < 200; ++i) {
    nexenne::utility::ignore(u.unite(0, static_cast<std::uint8_t>(i)));
  }
  CHECK(u.count() == 1);
  REQUIRE(u.size_of(0).has_value());
  CHECK(*u.size_of(0) == 200);
  REQUIRE(u.connected(0, 199).has_value());
  CHECK(*u.connected(0, 199));
  CHECK(u.parents().size() == 200);

  auto const grown{u.make_set()};  // append within range
  REQUIRE(grown.has_value());
  CHECK(static_cast<int>(*grown) == 200);
  CHECK(u.size() == 201);
}

TEST_CASE("nexenne::container::union_find tracks at most max() nodes (container-06)") {
  // Like std::vector and its size_type: a uint8 structure tracks 255 nodes
  // with indices 0..254, and growth past that reports full instead of handing
  // out 255.
  cn::union_find<std::uint8_t> u;
  for (int i{0}; i < 255; ++i) {
    auto const node{u.make_set()};
    REQUIRE(node.has_value());
    CHECK(static_cast<int>(*node) == i);
  }
  auto const refused{u.make_set()};
  REQUIRE_FALSE(refused.has_value());
  CHECK(refused.error() == cn::container_error::full);
  CHECK(u.size() == 255);
  CHECK(u.count() == 255);
  auto enumerated{0};
  for (auto const i : u.nodes()) {
    CHECK(static_cast<int>(i) == enumerated);
    ++enumerated;
  }
  CHECK(enumerated == 255);
  for (int i{1}; i < 255; ++i) {
    nexenne::utility::ignore(u.unite(0, static_cast<std::uint8_t>(i)));
  }
  auto copy{u};
  CHECK(u.same_partition(copy));
  CHECK(copy.count() == 1);
}

template <typename U>
concept resettable_to_n = requires(U u) { u.reset(std::size_t{3}); };
static_assert(!std::is_constructible_v<cn::union_find<>, std::size_t>);
static_assert(!std::is_constructible_v<cn::union_find<std::uint8_t>, int>);
static_assert(!resettable_to_n<cn::union_find<>>);

}  // namespace
