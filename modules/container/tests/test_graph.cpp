/**
 * @file
 * @brief Tests for nexenne::container::graph.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <ranges>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>

namespace {

namespace cn = nexenne::container;

TEST_CASE("nexenne::container::graph add_vertex hands out dense stable ids") {
  cn::graph<> g;  // void payload
  auto const a{g.add_vertex()};
  auto const b{g.add_vertex()};
  auto const c{g.add_vertex()};
  CHECK(a == 0);
  CHECK(b == 1);
  CHECK(c == 2);
  CHECK(g.vertex_count() == 3);
  CHECK(g.contains(2));
  CHECK_FALSE(g.contains(3));
}

TEST_CASE("nexenne::container::graph add_edge bounds-checks endpoints, tracks count") {
  cn::graph<> g{3};  // vertices 0,1,2
  CHECK(g.add_edge(0, 1).has_value());
  CHECK(g.add_edge(0, 2).has_value());
  CHECK(g.add_edge(1, 2).has_value());
  CHECK(g.edge_count() == 3);
  CHECK(g.add_edge(0, 9).error() == cn::container_error::out_of_range);
  CHECK(g.add_edge(9, 0).error() == cn::container_error::out_of_range);
  CHECK(g.edge_count() == 3);  // failed adds change nothing
}

TEST_CASE("nexenne::container::graph has_edge and out_degree") {
  cn::graph<> g{3};
  static_cast<void>(g.add_edge(0, 1));
  static_cast<void>(g.add_edge(0, 2));
  CHECK(g.has_edge(0, 1));
  CHECK(g.has_edge(0, 2));
  CHECK_FALSE(g.has_edge(0, 0));
  CHECK_FALSE(g.has_edge(9, 0));  // invalid from
  REQUIRE(g.out_degree(0).has_value());
  CHECK(*g.out_degree(0) == 2);
  CHECK(*g.out_degree(1) == 0);
  CHECK(g.out_degree(9).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::graph remove_edge") {
  cn::graph<> g{3};
  static_cast<void>(g.add_edge(0, 1));
  static_cast<void>(g.add_edge(0, 2));
  REQUIRE(g.remove_edge(0, 1).has_value());
  CHECK(*g.remove_edge(0, 1) == false);  // already removed, second call is false
  CHECK_FALSE(g.has_edge(0, 1));
  CHECK(g.has_edge(0, 2));
  CHECK(g.edge_count() == 1);
  CHECK(g.remove_edge(9, 0).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::graph edges_of yields a payload span") {
  cn::graph<int> g{2};  // int edge payload (e.g. weight)
  CHECK(g.add_edge(0, 1, 42).has_value());
  CHECK(g.add_edge(0, 1, 7).has_value());  // parallel edge with a different weight
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].target == 1);
  CHECK(out[0].data == 42);
  CHECK(out[1].data == 7);
  CHECK(g.edges_of(9).empty());  // invalid vertex -> empty span
}

TEST_CASE("nexenne::container::graph neighbors view yields target ids") {
  cn::graph<int> g{4};
  static_cast<void>(g.add_edge(0, 1, 1));
  static_cast<void>(g.add_edge(0, 3, 1));
  static_cast<void>(g.add_edge(0, 2, 1));
  std::vector<std::uint32_t> seen;
  for (auto const n : g.neighbors(0)) {
    seen.push_back(n);
  }
  CHECK(seen == std::vector<std::uint32_t>{1, 3, 2});  // insertion order
  CHECK(std::ranges::distance(g.neighbors(9)) == 0);   // invalid vertex -> empty
}

TEST_CASE("nexenne::container::graph vertices is a lazy ascending id range") {
  cn::graph<> g{4};
  std::vector<std::uint32_t> ids;
  for (auto const v : g.vertices()) {
    ids.push_back(v);
  }
  CHECK(ids == std::vector<std::uint32_t>{0, 1, 2, 3});
}

TEST_CASE("nexenne::container::graph self-loops and parallel edges are allowed") {
  cn::graph<> g{1};
  CHECK(g.add_edge(0, 0).has_value());  // self-loop
  CHECK(g.add_edge(0, 0).has_value());  // parallel
  CHECK(g.edge_count() == 2);
  CHECK(g.has_edge(0, 0));
  CHECK(*g.out_degree(0) == 2);
}

TEST_CASE("nexenne::container::graph clear and swap") {
  cn::graph<> a{2};
  static_cast<void>(a.add_edge(0, 1));
  cn::graph<> b{3};
  swap(a, b);
  CHECK(a.vertex_count() == 3);
  CHECK(a.edge_count() == 0);
  CHECK(b.vertex_count() == 2);
  CHECK(b.edge_count() == 1);
  b.clear();
  CHECK(b.empty());
  CHECK(b.edge_count() == 0);
}

TEST_CASE("nexenne::container::graph equality compares structure and payload") {
  cn::graph<int> a{2};
  static_cast<void>(a.add_edge(0, 1, 5));
  cn::graph<int> b{2};
  static_cast<void>(b.add_edge(0, 1, 5));
  cn::graph<int> c{2};
  static_cast<void>(c.add_edge(0, 1, 6));  // different payload
  CHECK(a == b);
  CHECK(a != c);

  cn::graph<> d{2};
  static_cast<void>(d.add_edge(0, 1));
  cn::graph<> e{2};
  CHECK(d != e);  // e has no edge
}

}  // namespace
