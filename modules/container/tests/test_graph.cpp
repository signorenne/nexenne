/**
 * @file
 * @brief Tests for nexenne::container::graph.
 */

#include <doctest/doctest.h>

#include <cstdint>
#include <ranges>
#include <string>
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

TEST_CASE("nexenne::container::graph the empty default-constructed graph") {
  cn::graph<> g;
  CHECK(g.empty());
  CHECK(g.vertex_count() == 0);
  CHECK(g.edge_count() == 0);
  CHECK_FALSE(g.contains(0));
  CHECK(g.edges_of(0).empty());
  CHECK(std::ranges::distance(g.neighbors(0)) == 0);
  CHECK(std::ranges::distance(g.vertices()) == 0);
  CHECK_FALSE(g.has_edge(0, 0));
  CHECK(g.out_degree(0).error() == cn::container_error::out_of_range);
  CHECK(g.add_edge(0, 0).error() == cn::container_error::out_of_range);
  CHECK(g.remove_edge(0, 0).error() == cn::container_error::out_of_range);
  CHECK(g.max_size() > 0);
}

TEST_CASE("nexenne::container::graph reserve_vertices and shrink_to_fit preserve contents") {
  cn::graph<int> g;
  g.reserve_vertices(16);
  auto const a{g.add_vertex()};
  auto const b{g.add_vertex()};
  CHECK(g.add_edge(a, b, 5).has_value());
  g.shrink_to_fit();
  CHECK(g.vertex_count() == 2);
  CHECK(g.edge_count() == 1);
  CHECK(g.has_edge(a, b));
  REQUIRE(g.edges_of(a).size() == 1);
  CHECK(g.edges_of(a)[0].data == 5);
}

TEST_CASE("nexenne::container::graph directed edges are one-way; undirected needs both directions"
) {
  cn::graph<> g{2};
  static_cast<void>(g.add_edge(0, 1));  // directed 0 -> 1 only
  CHECK(g.has_edge(0, 1));
  CHECK_FALSE(g.has_edge(1, 0));        // not symmetric
  static_cast<void>(g.add_edge(1, 0));  // add the reverse to model undirected
  CHECK(g.has_edge(1, 0));
  CHECK(g.edge_count() == 2);
  CHECK(*g.out_degree(0) == 1);
  CHECK(*g.out_degree(1) == 1);
}

TEST_CASE("nexenne::container::graph remove_edge drops only the first of parallel edges") {
  cn::graph<int> g{2};
  static_cast<void>(g.add_edge(0, 1, 10));
  static_cast<void>(g.add_edge(0, 1, 20));
  static_cast<void>(g.add_edge(0, 1, 30));
  CHECK(g.edge_count() == 3);
  auto const removed{g.remove_edge(0, 1)};  // one call removes exactly one edge
  REQUIRE(removed.has_value());
  CHECK(*removed == true);
  CHECK(g.edge_count() == 2);
  CHECK(g.has_edge(0, 1));  // parallels remain
  // the first inserted (weight 10) went; 20 then 30 survive in order
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].data == 20);
  CHECK(out[1].data == 30);
}

TEST_CASE("nexenne::container::graph remove a self-loop") {
  cn::graph<> g{1};
  static_cast<void>(g.add_edge(0, 0));
  static_cast<void>(g.add_edge(0, 0));
  auto const removed{g.remove_edge(0, 0)};  // one call removes exactly one self-loop
  REQUIRE(removed.has_value());
  CHECK(*removed == true);
  CHECK(g.edge_count() == 1);
  CHECK(g.has_edge(0, 0));  // one parallel self-loop remains
}

TEST_CASE("nexenne::container::graph payload-free edges_of and neighbors") {
  cn::graph<> g{3};
  static_cast<void>(g.add_edge(0, 2));
  static_cast<void>(g.add_edge(0, 1));
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].target == 2);
  CHECK(out[1].target == 1);
  std::vector<std::uint32_t> seen;
  for (auto const n : g.neighbors(0)) {
    seen.push_back(n);
  }
  CHECK(seen == std::vector<std::uint32_t>{2, 1});
}

TEST_CASE("nexenne::container::graph carries a non-trivial std::string payload") {
  cn::graph<std::string> g{2};
  CHECK(g.add_edge(0, 1, std::string("highway")).has_value());
  CHECK(g.add_edge(0, 1, std::string("backroad")).has_value());
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].data == "highway");
  CHECK(out[1].data == "backroad");
  cn::graph<std::string> clone{g};  // exercise copy under LSan
  CHECK(clone == g);
  REQUIRE(clone.remove_edge(0, 1).has_value());
  CHECK(clone != g);
}

TEST_CASE("nexenne::container::graph equality is positional on per-vertex insertion order") {
  cn::graph<int> a{2};
  static_cast<void>(a.add_edge(0, 1, 1));
  static_cast<void>(a.add_edge(0, 1, 2));
  cn::graph<int> b{2};
  static_cast<void>(b.add_edge(0, 1, 2));  // reversed insertion order
  static_cast<void>(b.add_edge(0, 1, 1));
  CHECK(a != b);  // same multiset of edges, different order

  cn::graph<int> c{3};  // different vertex count
  CHECK(a != c);
}

TEST_CASE("nexenne::container::graph add_vertex grows an initially-sized graph") {
  cn::graph<> g{2};
  CHECK(g.vertex_count() == 2);
  auto const v{g.add_vertex()};
  CHECK(v == 2);
  CHECK(g.contains(2));
  CHECK(g.add_edge(2, 0).has_value());
  CHECK(*g.out_degree(2) == 1);
}

}  // namespace
