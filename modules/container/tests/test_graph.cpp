/**
 * @file
 * @brief Tests for nexenne::container::graph.
 */

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <ranges>
#include <string>
#include <type_traits>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace cn = nexenne::container;

template <typename G>
constexpr auto add_vertices(G& g, std::size_t const n) -> bool {
  for (std::size_t i{0}; i < n; ++i) {
    if (!g.add_vertex().has_value()) {
      return false;
    }
  }
  return true;
}

TEST_CASE("nexenne::container::graph add_vertex hands out dense stable ids") {
  cn::graph<> g;
  auto const a{g.add_vertex()};
  auto const b{g.add_vertex()};
  auto const c{g.add_vertex()};
  CHECK(a == 0U);
  CHECK(b == 1U);
  CHECK(c == 2U);
  CHECK(g.vertex_count() == 3);
  CHECK(g.contains(2));
  CHECK_FALSE(g.contains(3));
}

TEST_CASE("nexenne::container::graph add_edge bounds-checks endpoints, tracks count") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 3));
  CHECK(g.add_edge(0, 1).has_value());
  CHECK(g.add_edge(0, 2).has_value());
  CHECK(g.add_edge(1, 2).has_value());
  CHECK(g.edge_count() == 3);
  CHECK(g.add_edge(0, 9).error() == cn::container_error::out_of_range);
  CHECK(g.add_edge(9, 0).error() == cn::container_error::out_of_range);
  CHECK(g.edge_count() == 3);
}

TEST_CASE("nexenne::container::graph has_edge and out_degree") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(0, 2));
  CHECK(g.has_edge(0, 1));
  CHECK(g.has_edge(0, 2));
  CHECK_FALSE(g.has_edge(0, 0));
  CHECK_FALSE(g.has_edge(9, 0));
  REQUIRE(g.out_degree(0).has_value());
  CHECK(*g.out_degree(0) == 2);
  CHECK(*g.out_degree(1) == 0);
  CHECK(g.out_degree(9).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::graph remove_edge") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(0, 2));
  REQUIRE(g.remove_edge(0, 1).has_value());
  CHECK(*g.remove_edge(0, 1) == false);
  CHECK_FALSE(g.has_edge(0, 1));
  CHECK(g.has_edge(0, 2));
  CHECK(g.edge_count() == 1);
  CHECK(g.remove_edge(9, 0).error() == cn::container_error::out_of_range);
}

TEST_CASE("nexenne::container::graph edges_of yields a payload span") {
  cn::graph<int> g;
  REQUIRE(add_vertices(g, 2));
  CHECK(g.add_edge(0, 1, 42).has_value());
  CHECK(g.add_edge(0, 1, 7).has_value());
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].target == 1);
  CHECK(out[0].data == 42);
  CHECK(out[1].data == 7);
  CHECK(g.edges_of(9).empty());
}

TEST_CASE("nexenne::container::graph neighbors view yields target ids") {
  cn::graph<int> g;
  REQUIRE(add_vertices(g, 4));
  nexenne::utility::ignore(g.add_edge(0, 1, 1));
  nexenne::utility::ignore(g.add_edge(0, 3, 1));
  nexenne::utility::ignore(g.add_edge(0, 2, 1));
  std::vector<std::uint32_t> seen;
  for (auto const n : g.neighbors(0)) {
    seen.push_back(n);
  }
  CHECK(seen == std::vector<std::uint32_t>{1, 3, 2});
  CHECK(std::ranges::distance(g.neighbors(9)) == 0);
}

TEST_CASE("nexenne::container::graph vertices is a lazy ascending id range") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 4));
  std::vector<std::uint32_t> ids;
  for (auto const v : g.vertices()) {
    ids.push_back(v);
  }
  CHECK(ids == std::vector<std::uint32_t>{0, 1, 2, 3});
}

TEST_CASE("nexenne::container::graph self-loops and parallel edges are allowed") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 1));
  CHECK(g.add_edge(0, 0).has_value());
  CHECK(g.add_edge(0, 0).has_value());
  CHECK(g.edge_count() == 2);
  CHECK(g.has_edge(0, 0));
  CHECK(*g.out_degree(0) == 2);
}

TEST_CASE("nexenne::container::graph clear and swap") {
  cn::graph<> a;
  REQUIRE(add_vertices(a, 2));
  nexenne::utility::ignore(a.add_edge(0, 1));
  cn::graph<> b;
  REQUIRE(add_vertices(b, 3));
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
  cn::graph<int> a;
  REQUIRE(add_vertices(a, 2));
  nexenne::utility::ignore(a.add_edge(0, 1, 5));
  cn::graph<int> b;
  REQUIRE(add_vertices(b, 2));
  nexenne::utility::ignore(b.add_edge(0, 1, 5));
  cn::graph<int> c;
  REQUIRE(add_vertices(c, 2));
  nexenne::utility::ignore(c.add_edge(0, 1, 6));
  CHECK(a == b);
  CHECK(a != c);

  cn::graph<> d;

  REQUIRE(add_vertices(d, 2));
  nexenne::utility::ignore(d.add_edge(0, 1));
  cn::graph<> e;
  REQUIRE(add_vertices(e, 2));
  CHECK(d != e);
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
  auto const a{*g.add_vertex()};
  auto const b{*g.add_vertex()};
  CHECK(g.add_edge(a, b, 5).has_value());
  g.shrink_to_fit();
  CHECK(g.vertex_count() == 2);
  CHECK(g.edge_count() == 1);
  CHECK(g.has_edge(a, b));
  REQUIRE(g.edges_of(a).size() == 1);
  CHECK(g.edges_of(a)[0].data == 5);
}

TEST_CASE(
  "nexenne::container::graph directed edges are one-way; undirected needs both directions"
) {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 2));
  nexenne::utility::ignore(g.add_edge(0, 1));
  CHECK(g.has_edge(0, 1));
  CHECK_FALSE(g.has_edge(1, 0));
  nexenne::utility::ignore(g.add_edge(1, 0));
  CHECK(g.has_edge(1, 0));
  CHECK(g.edge_count() == 2);
  CHECK(*g.out_degree(0) == 1);
  CHECK(*g.out_degree(1) == 1);
}

TEST_CASE("nexenne::container::graph remove_edge drops only the first of parallel edges") {
  cn::graph<int> g;
  REQUIRE(add_vertices(g, 2));
  nexenne::utility::ignore(g.add_edge(0, 1, 10));
  nexenne::utility::ignore(g.add_edge(0, 1, 20));
  nexenne::utility::ignore(g.add_edge(0, 1, 30));
  CHECK(g.edge_count() == 3);
  auto const removed{g.remove_edge(0, 1)};
  REQUIRE(removed.has_value());
  CHECK(*removed == true);
  CHECK(g.edge_count() == 2);
  CHECK(g.has_edge(0, 1));
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].data == 20);
  CHECK(out[1].data == 30);
}

TEST_CASE("nexenne::container::graph remove a self-loop") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 1));
  nexenne::utility::ignore(g.add_edge(0, 0));
  nexenne::utility::ignore(g.add_edge(0, 0));
  auto const removed{g.remove_edge(0, 0)};
  REQUIRE(removed.has_value());
  CHECK(*removed == true);
  CHECK(g.edge_count() == 1);
  CHECK(g.has_edge(0, 0));
}

TEST_CASE("nexenne::container::graph payload-free edges_of and neighbors") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 2));
  nexenne::utility::ignore(g.add_edge(0, 1));
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
  cn::graph<std::string> g;
  REQUIRE(add_vertices(g, 2));
  CHECK(g.add_edge(0, 1, std::string("highway")).has_value());
  CHECK(g.add_edge(0, 1, std::string("backroad")).has_value());
  auto const out{g.edges_of(0)};
  REQUIRE(out.size() == 2);
  CHECK(out[0].data == "highway");
  CHECK(out[1].data == "backroad");
  cn::graph<std::string> clone{g};
  CHECK(clone == g);
  REQUIRE(clone.remove_edge(0, 1).has_value());
  CHECK(clone != g);
}

TEST_CASE("nexenne::container::graph equality is positional on per-vertex insertion order") {
  cn::graph<int> a;
  REQUIRE(add_vertices(a, 2));
  nexenne::utility::ignore(a.add_edge(0, 1, 1));
  nexenne::utility::ignore(a.add_edge(0, 1, 2));
  cn::graph<int> b;
  REQUIRE(add_vertices(b, 2));
  nexenne::utility::ignore(b.add_edge(0, 1, 2));
  nexenne::utility::ignore(b.add_edge(0, 1, 1));
  CHECK(a != b);

  cn::graph<int> c;

  REQUIRE(add_vertices(c, 3));
  CHECK(a != c);
}

TEST_CASE("nexenne::container::graph add_vertex grows an initially-sized graph") {
  cn::graph<> g;
  REQUIRE(add_vertices(g, 2));
  CHECK(g.vertex_count() == 2);
  auto const v{g.add_vertex()};
  CHECK(v == 2U);
  CHECK(g.contains(2));
  CHECK(g.add_edge(2, 0).has_value());
  CHECK(*g.out_degree(2) == 1);
}

TEST_CASE("nexenne::container::graph over a small Vertex type keeps ids in range") {
  cn::graph<void, std::uint8_t> g;
  constexpr int count{200};
  for (int i{0}; i < count; ++i) {
    auto const id{g.add_vertex()};
    REQUIRE(id.has_value());
    CHECK(static_cast<int>(*id) == i);
  }
  CHECK(g.vertex_count() == static_cast<std::size_t>(count));
  CHECK(g.contains(static_cast<std::uint8_t>(count - 1)));
  CHECK_FALSE(g.contains(static_cast<std::uint8_t>(count)));

  int enumerated{0};
  std::uint8_t expected{0};
  for (auto const v : g.vertices()) {
    CHECK(v == expected);
    ++expected;
    ++enumerated;
  }
  CHECK(enumerated == count);
}

consteval auto consteval_graph_probe() -> bool {
  cn::graph<int> g;
  if (!add_vertices(g, 3) || !g.add_edge(0, 1, 7).has_value()) {
    return false;
  }
  if (!g.add_edge(0, 2, 8).has_value()) {
    return false;
  }
  bool const edge{g.has_edge(0, 1)};
  auto const od{g.out_degree(0)};
  if (!od.has_value() || *od != 2) {
    return false;
  }
  if (!g.remove_edge(0, 2).has_value()) {
    return false;
  }
  cn::graph<int> h;
  if (!add_vertices(h, 3) || !h.add_edge(0, 1, 7).has_value()) {
    return false;
  }
  return edge && g == h;
}

static_assert(consteval_graph_probe());

TEST_CASE("nexenne::container::graph holds at most max() vertices") {
  cn::graph<void, std::uint8_t> g;
  for (int i{0}; i < 255; ++i) {
    auto const id{g.add_vertex()};
    REQUIRE(id.has_value());
    CHECK(static_cast<int>(*id) == i);
  }
  auto const refused{g.add_vertex()};
  REQUIRE_FALSE(refused.has_value());
  CHECK(refused.error() == cn::container_error::full);
  CHECK(g.vertex_count() == 255);
  auto enumerated{0};
  for (auto const v : g.vertices()) {
    CHECK(static_cast<int>(v) == enumerated);
    ++enumerated;
  }
  CHECK(enumerated == 255);
}

static_assert(!std::is_constructible_v<cn::graph<>, std::size_t>);
static_assert(!std::is_constructible_v<cn::graph<int, std::uint8_t>, int>);

}  // namespace
