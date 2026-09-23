/**
 * @file
 * @brief Tests for nexenne::algorithm graph algorithms.
 *
 * Traversals are checked against an independent transitive-closure reachability
 * reference and against each other; the shortest-path trio (Dijkstra,
 * Bellman-Ford, Floyd-Warshall) is cross-validated on random weighted graphs;
 * topological order, SCCs, connected components, the MST, A*, and binary-lifting
 * LCA are each verified on constructed graphs with known answers.
 */

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/bellman_ford.hpp>
#include <nexenne/algorithm/graph/bfs.hpp>
#include <nexenne/algorithm/graph/connected_components.hpp>
#include <nexenne/algorithm/graph/dfs.hpp>
#include <nexenne/algorithm/graph/dijkstra.hpp>
#include <nexenne/algorithm/graph/floyd_warshall.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/graph/lca.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/graph/topological_sort.hpp>
#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/utility/ignore.hpp>

namespace {

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;
using V = std::uint32_t;
using ugraph = nc::graph<void, V>;
using wgraph = nc::graph<double, V>;

template <typename G>
constexpr auto add_vertices(G& g, std::size_t const n) -> bool {
  for (std::size_t i{0}; i < n; ++i) {
    if (!g.add_vertex().has_value()) {
      return false;
    }
  }
  return true;
}

struct lcg {
  std::uint64_t state{0x243F6A8885A308D3ull};

  auto next() -> std::uint64_t {
    state = state * 6364136223846793005ull + 1442695040888963407ull;
    return state >> 11;
  }
};

[[nodiscard]] auto dist_eq(double const a, double const b) -> bool {
  if (std::isinf(a) && std::isinf(b)) {
    return true;
  }
  return std::abs(a - b) < 1e-9;
}

// Independent reachability reference: boolean transitive closure (Warshall).
template <typename G>
[[nodiscard]] auto reachable_from(G const& g, V const source) -> std::vector<char> {
  auto const n{g.vertex_count()};
  auto reach{std::vector<std::vector<char>>(n, std::vector<char>(n, 0))};
  for (auto u{V{0}}; u < n; ++u) {
    reach[u][u] = 1;
    for (auto const& e : g.edges_of(u)) {
      reach[u][e.target] = 1;
    }
  }
  for (auto k{std::size_t{0}}; k < n; ++k) {
    for (auto i{std::size_t{0}}; i < n; ++i) {
      for (auto j{std::size_t{0}}; j < n; ++j) {
        if (reach[i][k] && reach[k][j]) {
          reach[i][j] = 1;
        }
      }
    }
  }
  return reach[source];
}

[[nodiscard]] auto random_dag(lcg& gen, V const n, int const density) -> ugraph {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, n));
  for (auto u{V{0}}; u < n; ++u) {
    for (auto v{u + 1}; v < n; ++v) {
      if (static_cast<int>(gen.next() % 100) < density) {
        nexenne::utility::ignore(g.add_edge(u, v));  // only u < v: guaranteed acyclic
      }
    }
  }
  return g;
}

// bfs / dfs

TEST_CASE("nexenne::algorithm bfs and dfs visit exactly the reachable set") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 200; ++trial) {
    auto const n{static_cast<V>(2 + gen.next() % 12)};
    auto const g{random_dag(gen, n, 35)};
    auto const source{static_cast<V>(gen.next() % n)};
    auto const reach{reachable_from(g, source)};

    auto bfs_seen{std::vector<char>(n, 0)};
    REQUIRE(alg::bfs(g, source, [&](V const u) { bfs_seen[u] += 1; }).has_value());
    auto dfs_seen{std::vector<char>(n, 0)};
    REQUIRE(alg::dfs(g, source, [&](V const u) { dfs_seen[u] += 1; }).has_value());

    for (auto v{V{0}}; v < n; ++v) {
      CAPTURE(v);
      CHECK(bfs_seen[v] == reach[v]);  // visited iff reachable, exactly once
      CHECK(dfs_seen[v] == reach[v]);
    }
  }
}

TEST_CASE("nexenne::algorithm bfs early termination and invalid source") {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, 4));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(1, 2));
  nexenne::utility::ignore(g.add_edge(2, 3));
  auto count{0};
  REQUIRE(alg::bfs(g, V{0}, [&](V) {
            count += 1;
            return count < 2;  // stop after two
          }).has_value());
  CHECK(count == 2);
  CHECK(alg::bfs(g, V{99}, [](V) {}).error() == nc::container_error::out_of_range);
}

// topological_sort / is_acyclic

TEST_CASE("nexenne::algorithm topological_sort yields a valid order on DAGs") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 200; ++trial) {
    auto const n{static_cast<V>(2 + gen.next() % 14)};
    auto const g{random_dag(gen, n, 30)};
    auto const order{alg::topological_sort(g)};
    REQUIRE(order.has_value());
    REQUIRE(order->size() == n);
    auto pos{std::vector<std::size_t>(n, 0)};
    for (auto i{std::size_t{0}}; i < order->size(); ++i) {
      pos[(*order)[i]] = i;
    }
    for (auto u{V{0}}; u < n; ++u) {
      for (auto const& e : g.edges_of(u)) {
        CHECK(pos[u] < pos[e.target]);  // every edge points forward
      }
    }
    CHECK(alg::is_acyclic(g));
  }
}

TEST_CASE("nexenne::algorithm topological_sort detects cycles") {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(1, 2));
  nexenne::utility::ignore(g.add_edge(2, 0));  // cycle
  CHECK(alg::topological_sort(g).error() == nc::container_error::not_found);
  CHECK(!alg::is_acyclic(g));
}

// connected_components (weakly connected)

TEST_CASE("nexenne::algorithm connected_components groups by component") {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, 6));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(1, 2));  // {0,1,2}
  nexenne::utility::ignore(g.add_edge(3, 4));  // {3,4}
  // vertex 5 isolated
  auto const r{alg::connected_components(g)};
  CHECK(r.num_components == 3);
  CHECK(r.labels[0] == r.labels[1]);
  CHECK(r.labels[1] == r.labels[2]);
  CHECK(r.labels[3] == r.labels[4]);
  CHECK(r.labels[0] != r.labels[3]);
  CHECK(r.labels[5] != r.labels[0]);
}

// tarjan_scc

TEST_CASE("nexenne::algorithm::tarjan_scc finds strongly connected components") {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, 5));
  nexenne::utility::ignore(g.add_edge(0, 1));
  nexenne::utility::ignore(g.add_edge(1, 2));
  nexenne::utility::ignore(g.add_edge(2, 0));  // {0,1,2} is one SCC
  nexenne::utility::ignore(g.add_edge(2, 3));
  nexenne::utility::ignore(g.add_edge(3, 4));  // 3 and 4 are singletons
  auto const r{alg::tarjan_scc(g)};
  CHECK(r.num_components == 3);
  CHECK(r.labels[0] == r.labels[1]);
  CHECK(r.labels[1] == r.labels[2]);
  CHECK(r.labels[3] != r.labels[0]);
  CHECK(r.labels[4] != r.labels[3]);

  // A DAG has one SCC per vertex; a single cycle has one SCC overall.
  auto gen{lcg{}};
  auto const dag{random_dag(gen, 8, 40)};
  CHECK(alg::tarjan_scc(dag).num_components == 8);
}

// kruskal_mst

TEST_CASE("nexenne::algorithm::kruskal_mst builds a minimum spanning forest") {
  // Undirected: add each edge in both directions.
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 4));
  auto undirected{[&](V a, V b, double w) {
    nexenne::utility::ignore(g.add_edge(a, b, w));
    nexenne::utility::ignore(g.add_edge(b, a, w));
  }};
  undirected(0, 1, 1.0);
  undirected(1, 2, 2.0);
  undirected(2, 3, 3.0);
  undirected(0, 3, 4.0);
  undirected(0, 2, 5.0);
  auto const mst{alg::kruskal_mst(g)};
  CHECK(mst.size() == 3);  // n - 1 for a connected graph
  auto total{0.0};
  for (auto const& e : mst) {
    total += e.weight;
  }
  CHECK(dist_eq(total, 1.0 + 2.0 + 3.0));  // picks the three cheapest acyclic edges
}

// dijkstra / bellman_ford / floyd_warshall

[[nodiscard]] auto random_weighted(lcg& gen, V const n, int const density) -> wgraph {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, n));
  for (auto u{V{0}}; u < n; ++u) {
    for (auto v{V{0}}; v < n; ++v) {
      if (u != v && static_cast<int>(gen.next() % 100) < density) {
        nexenne::utility::ignore(g.add_edge(u, v, 1.0 + static_cast<double>(gen.next() % 20)));
      }
    }
  }
  return g;
}

TEST_CASE("nexenne::algorithm shortest-path trio agrees on random graphs") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 120; ++trial) {
    auto const n{static_cast<V>(2 + gen.next() % 10)};
    auto const g{random_weighted(gen, n, 40)};
    auto const fw{alg::floyd_warshall(g)};
    REQUIRE(fw.has_value());
    for (auto src{V{0}}; src < n; ++src) {
      auto const dij{alg::dijkstra(g, src)};
      auto const bf{alg::bellman_ford(g, src)};
      REQUIRE(dij.has_value());
      REQUIRE(bf.has_value());
      for (auto j{V{0}}; j < n; ++j) {
        CAPTURE(src);
        CAPTURE(j);
        CHECK(dist_eq((*dij)[j], (*bf)[j]));
        CHECK(dist_eq((*dij)[j], fw->at(src, j)));
      }
    }
  }
}

TEST_CASE("nexenne::algorithm::dijkstra known answer and unreachable handling") {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 4));
  nexenne::utility::ignore(g.add_edge(0, 1, 1.0));
  nexenne::utility::ignore(g.add_edge(1, 2, 2.0));
  nexenne::utility::ignore(g.add_edge(0, 2, 5.0));  // 0->1->2 (cost 3) beats 0->2 (cost 5)
  // vertex 3 unreachable
  auto const d{alg::dijkstra(g, V{0})};
  REQUIRE(d.has_value());
  CHECK(dist_eq((*d)[0], 0.0));
  CHECK(dist_eq((*d)[1], 1.0));
  CHECK(dist_eq((*d)[2], 3.0));
  CHECK(std::isinf((*d)[3]));
  CHECK(alg::dijkstra(g, V{9}).error() == nc::container_error::out_of_range);
}

TEST_CASE("nexenne::algorithm::bellman_ford detects negative cycles") {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 1, 1.0));
  nexenne::utility::ignore(g.add_edge(1, 2, -1.0));
  nexenne::utility::ignore(g.add_edge(2, 0, -1.0));  // total -1 around the cycle
  CHECK(!alg::bellman_ford(g, V{0}).has_value());
}

// a_star

TEST_CASE("nexenne::algorithm::a_star with zero heuristic matches dijkstra") {
  auto gen{lcg{}};
  for (auto trial{0}; trial < 60; ++trial) {
    auto const n{static_cast<V>(3 + gen.next() % 8)};
    auto const g{random_weighted(gen, n, 45)};
    auto const goal{static_cast<V>(gen.next() % n)};
    auto const dij{alg::dijkstra(g, V{0})};
    REQUIRE(dij.has_value());

    auto const r{alg::a_star<double, V, double>(g, V{0}, goal, [](V) { return 0.0; })};
    if (std::isinf((*dij)[goal])) {
      CHECK(!r.has_value());  // unreachable
    } else {
      REQUIRE(r.has_value());
      CHECK(dist_eq(r->cost, (*dij)[goal]));  // optimal cost matches
      CHECK(r->path.front() == V{0});
      CHECK(r->path.back() == goal);
      // The path is a real walk whose weights sum to the cost.
      auto sum{0.0};
      for (auto i{std::size_t{1}}; i < r->path.size(); ++i) {
        auto const from{r->path[i - 1]};
        auto const to{r->path[i]};
        auto found{false};
        for (auto const& e : g.edges_of(from)) {
          if (e.target == to) {
            sum += e.data;
            found = true;
            break;
          }
        }
        CHECK(found);
      }
      CHECK(dist_eq(sum, r->cost));
    }
  }
}

// degenerate graphs

TEST_CASE("nexenne::algorithm graph algorithms handle empty and single-vertex graphs") {
  // Empty graph: source-based algorithms reject any source; whole-graph ones
  // return empty results.
  auto const empty{ugraph{}};
  CHECK(alg::bfs(empty, V{0}, [](V) {}).error() == nc::container_error::out_of_range);
  CHECK(alg::tarjan_scc(empty).num_components == 0);
  CHECK(alg::connected_components(empty).num_components == 0);
  CHECK(alg::topological_sort(empty)->empty());
  auto const wempty{wgraph{}};
  CHECK(alg::floyd_warshall(wempty)->n == 0);
  CHECK(alg::kruskal_mst(wempty).empty());

  // Single isolated vertex.
  auto one{ugraph{}};
  REQUIRE(add_vertices(one, 1));
  auto seen{0};
  REQUIRE(alg::bfs(one, V{0}, [&](V) { seen += 1; }).has_value());
  CHECK(seen == 1);
  CHECK(alg::tarjan_scc(one).num_components == 1);
  CHECK(alg::connected_components(one).num_components == 1);
  CHECK(alg::topological_sort(one).value() == std::vector<V>{0});
  auto wone{wgraph{}};
  REQUIRE(add_vertices(wone, 1));
  auto const d{alg::dijkstra(wone, V{0})};
  REQUIRE(d.has_value());
  CHECK(dist_eq((*d)[0], 0.0));
}

TEST_CASE("nexenne::algorithm self-loop is a one-vertex SCC and a cycle") {
  auto g{ugraph{}};
  REQUIRE(add_vertices(g, 2));
  nexenne::utility::ignore(g.add_edge(0, 0));  // self-loop
  nexenne::utility::ignore(g.add_edge(0, 1));
  CHECK(alg::tarjan_scc(g).num_components == 2);  // {0} and {1}
  CHECK(!alg::is_acyclic(g));                     // a self-loop is a cycle
  CHECK(alg::topological_sort(g).error() == nc::container_error::not_found);
}

TEST_CASE("nexenne::algorithm shortest paths pick the cheapest of parallel edges") {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 3));
  nexenne::utility::ignore(g.add_edge(0, 1, 9.0));
  nexenne::utility::ignore(g.add_edge(0, 1, 2.0));  // parallel edge, cheaper
  nexenne::utility::ignore(g.add_edge(1, 2, 1.0));
  auto const d{alg::dijkstra(g, V{0})};
  REQUIRE(d.has_value());
  CHECK(dist_eq((*d)[1], 2.0));  // relaxation takes the smaller weight
  CHECK(dist_eq((*d)[2], 3.0));
  auto const fw{alg::floyd_warshall(g)};
  REQUIRE(fw.has_value());
  CHECK(dist_eq(fw->at(0, 1), 2.0));  // seed keeps the smallest parallel edge
}

TEST_CASE("nexenne::algorithm::kruskal_mst on a disconnected graph is a forest") {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 5));
  auto undirected{[&](V a, V b, double w) {
    nexenne::utility::ignore(g.add_edge(a, b, w));
    nexenne::utility::ignore(g.add_edge(b, a, w));
  }};
  undirected(0, 1, 1.0);  // component {0, 1}
  undirected(2, 3, 2.0);  // component {2, 3}
  // vertex 4 isolated => 3 components total
  auto const mst{alg::kruskal_mst(g)};
  CHECK(mst.size() == 2);  // n - components == 5 - 3 edges in the forest
}

TEST_CASE("nexenne::algorithm::a_star evaluates the heuristic at most once per vertex") {
  auto g{wgraph{}};
  REQUIRE(add_vertices(g, 5));
  nexenne::utility::ignore(g.add_edge(0, 1, 1.0));
  nexenne::utility::ignore(g.add_edge(1, 2, 1.0));
  nexenne::utility::ignore(g.add_edge(2, 3, 1.0));
  nexenne::utility::ignore(
    g.add_edge(0, 3, 5.0)
  );  // a second, longer route into 3 (relaxes it twice)
  nexenne::utility::ignore(g.add_edge(3, 4, 1.0));
  auto calls{std::vector<int>(5, 0)};
  auto const r{alg::a_star<double, V, double>(g, V{0}, V{4}, [&](V const v) {
    calls[v] += 1;
    return 0.0;
  })};
  REQUIRE(r.has_value());
  for (auto const c : calls) {
    CHECK(c <= 1);  // cached: never recomputed per relaxation
  }
}

// lca

TEST_CASE("nexenne::algorithm::lca answers ancestor queries") {
  // Tree: 0 is the root; its children are 1 and 2; 1 has children 3 and 4;
  // 2 has child 5.
  auto const parent{std::vector<std::int32_t>{0, 0, 0, 1, 1, 2}};
  auto tree{alg::lca<std::int32_t>{}};
  tree.build(std::span<std::int32_t const>{parent}, 0);
  CHECK(tree.query(3, 4) == 1);
  CHECK(tree.query(3, 5) == 0);
  CHECK(tree.query(4, 2) == 0);
  CHECK(tree.query(5, 2) == 2);
  CHECK(tree.query(3, 3) == 3);
  CHECK(tree.query(3, 1) == 1);  // ancestor of itself
  CHECK(tree.depth_of(0) == 0);
  CHECK(tree.depth_of(3) == 2);
  CHECK(tree.depth_of(5) == 2);
}

TEST_CASE("nexenne::algorithm::lca size reports the indexed node count") {
  auto tree{alg::lca<std::int32_t>{}};
  CHECK(tree.size() == 0);
  auto const parent{std::vector<std::int32_t>{0, 0, 0, 1, 1, 2}};
  tree.build(std::span<std::int32_t const>{parent}, 0);
  CHECK(tree.size() == 6);
  tree.build(std::span<std::int32_t const>{}, 0);
  CHECK(tree.size() == 0);
}

TEST_CASE("nexenne::algorithm::lca on degenerate trees (single node and a path)") {
  // A single-node tree: the only node is its own ancestor at depth 0.
  auto const just_root{std::vector<std::int32_t>{0}};
  auto one{alg::lca<std::int32_t>{}};
  one.build(std::span<std::int32_t const>{just_root}, 0);
  CHECK(one.query(0, 0) == 0);
  CHECK(one.depth_of(0) == 0);

  // A path 0-1-2-3-4 (each node's parent is the one before it): the LCA of any
  // two nodes is the shallower one, and depth equals the index.
  auto const chain{std::vector<std::int32_t>{0, 0, 1, 2, 3}};
  auto path{alg::lca<std::int32_t>{}};
  path.build(std::span<std::int32_t const>{chain}, 0);
  CHECK(path.query(4, 2) == 2);
  CHECK(path.query(3, 1) == 1);
  CHECK(path.query(4, 0) == 0);
  CHECK(path.query(2, 2) == 2);
  CHECK(path.depth_of(4) == 4);
}

TEST_CASE("nexenne::algorithm::tarjan_scc covers the full id space of a small V (C1)") {
  // Regression for C1: the outer scan used a V-typed counter that wrapped
  // max -> 0 and looped forever, and the DFS index sentinel (max(V)) collided
  // with a legitimate index. tarjan now drives the scan and the indices with
  // std::size_t. The graph holds at most max(V) vertices (ids 0..254 for
  // uint8), so a 255-vertex graph is the largest; if this test returns at all,
  // the hang is gone.
  using V8 = std::uint8_t;
  auto g{nc::graph<void, V8>{}};
  for (auto i{0}; i < 255; ++i) {
    REQUIRE(g.add_vertex().has_value());
  }
  CHECK(g.add_vertex().error() == nc::container_error::full);
  // One directed cycle over all 255 vertices: exactly one strongly connected
  // component, so every label must be identical.
  for (auto i{std::uint32_t{0}}; i < 255; ++i) {
    nexenne::utility::ignore(g.add_edge(static_cast<V8>(i), static_cast<V8>((i + 1) % 255)));
  }
  auto const r{alg::tarjan_scc(g)};
  REQUIRE(r.labels.size() == 255);
  CHECK(r.num_components == 1);
  for (auto const label : r.labels) {
    CHECK(label == r.labels[0]);
  }
}

TEST_CASE("nexenne::algorithm::connected_components labels stay distinct at the id edge (C1)") {
  // Regression for C1's secondary defect: a V-typed component counter wrapped to
  // 0 once every vertex was its own component, and a V-typed scan counter would
  // loop forever. 255 singletons must yield 255 distinct labels and a count of
  // 255 (the container holds at most max(V) == 255 vertices for uint8).
  using V8 = std::uint8_t;
  auto g{nc::graph<void, V8>{}};
  for (auto i{0}; i < 255; ++i) {
    REQUIRE(g.add_vertex().has_value());
  }
  auto const r{alg::connected_components(g)};
  REQUIRE(r.labels.size() == 255);
  CHECK(r.num_components == 255);
  auto seen{std::vector<char>(255, 0)};
  for (auto const label : r.labels) {
    seen[label] = char{1};
  }
  CHECK(std::ranges::count(seen, char{1}) == 255);
}

TEST_CASE("nexenne::algorithm::dijkstra guards integral distance overflow (M1)") {
  // Regression for M1: d_u + w wrapped for integral Weight, so a path whose true
  // cost overflowed was reported as a bogus small distance. The saturating guard
  // leaves the vertex at the unreachable sentinel instead of wrapping.
  using w_type = std::uint32_t;
  auto g{nc::graph<w_type, V>{}};
  for (auto i{0}; i < 3; ++i) {
    REQUIRE(g.add_vertex().has_value());
  }
  nexenne::utility::ignore(g.add_edge(0, 1, w_type{3'000'000'000}));
  nexenne::utility::ignore(g.add_edge(1, 2, w_type{3'000'000'000}));
  auto const r{alg::dijkstra<w_type, V, w_type>(g, V{0})};
  REQUIRE(r.has_value());
  auto const& d{*r};
  CHECK(d[0] == w_type{0});
  CHECK(d[1] == w_type{3'000'000'000});
  // 0 -> 2 costs 6e9, which overflows uint32; it must read as unreachable (max),
  // not the wrapped value 1'705'032'704 the unguarded sum produced.
  CHECK(d[2] == std::numeric_limits<w_type>::max());
}

TEST_CASE(
  "nexenne::algorithm::topological_sort and kruskal_mst are correct near the id edge (M2)"
) {
  // M2 is resolved by the container holding at most max(V) vertices plus C1's
  // std::size_t loops; this pins that both algorithms iterate every vertex on a
  // large uint8 graph rather than collapsing to an empty range.
  using V8 = std::uint8_t;
  auto chain{nc::graph<int, V8>{}};
  for (auto i{0}; i < 200; ++i) {
    REQUIRE(chain.add_vertex().has_value());
  }
  for (auto i{std::uint32_t{0}}; i + 1 < 200; ++i) {
    nexenne::utility::ignore(chain.add_edge(static_cast<V8>(i), static_cast<V8>(i + 1), 1));
  }
  auto const order{alg::topological_sort(chain)};
  REQUIRE(order.has_value());
  CHECK(order->size() == 200);
  CHECK(order->front() == V8{0});
  CHECK(order->back() == V8{199});
  auto const mst{alg::kruskal_mst(chain)};
  CHECK(mst.size() == 199);  // a 200-vertex tree has 199 edges
}

TEST_CASE(
  "nexenne::algorithm::floyd_warshall reports a dense negative cycle without overflowing "
  "(algorithm-04)"
) {
  // Every edge -1 over 40 vertices: the costs fell exponentially and the int
  // sum overflowed (UBSan) before the cycle was reported.
  constexpr V n{40};
  auto g{nc::graph<int, V>{}};
  REQUIRE(add_vertices(g, n));
  for (V u{0}; u < n; ++u) {
    for (V v{0}; v < n; ++v) {
      if (u != v) {
        REQUIRE(g.add_edge(u, v, -1).has_value());
      }
    }
  }
  auto const r{alg::floyd_warshall<int, V, int>(g)};
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error() == nc::container_error::not_found);
}

TEST_CASE(
  "nexenne::algorithm::a_star tolerates an infinite heuristic on a dead end (algorithm-10)"
) {
  // g + h overflowed (UBSan) for an admissible INT_MAX heuristic.
  auto g{nc::graph<int, V>{}};
  REQUIRE(add_vertices(g, 4));
  REQUIRE(g.add_edge(0u, 1u, 1).has_value());
  REQUIRE(g.add_edge(0u, 2u, 1).has_value());
  REQUIRE(g.add_edge(2u, 3u, 1).has_value());
  auto const h{[](V const v) noexcept { return v == 1u ? std::numeric_limits<int>::max() : 0; }};
  auto const r{alg::a_star<int, V, int>(g, 0u, 3u, h)};
  REQUIRE(r.has_value());
  CHECK(r->cost == 2);
}

struct throwing_weight {
  int v{0};

  throwing_weight() = default;

  throwing_weight(throwing_weight const& other) : v{other.v} {}

  auto operator=(throwing_weight const&) -> throwing_weight& = default;

  friend auto operator==(throwing_weight const& a, throwing_weight const& b) -> bool {
    return a.v == b.v;
  }

  friend auto operator<=>(throwing_weight const& a, throwing_weight const& b)
    -> std::strong_ordering {
    return a.v <=> b.v;
  }
};

struct throwing_edge {
  throwing_weight data{};
};

struct plain_edge {
  double data{};
};

template <typename Entry>
constexpr bool nothrow_compare_v{
  noexcept(std::declval<Entry const&>() == std::declval<Entry const&>())
  && noexcept(std::declval<Entry const&>() <=> std::declval<Entry const&>())
};

static_assert(!nothrow_compare_v<alg::detail::dijkstra_entry<V, throwing_weight>>);
static_assert(nothrow_compare_v<alg::detail::dijkstra_entry<V, double>>);
static_assert(!nothrow_compare_v<alg::detail::a_star_entry<V, throwing_weight>>);
static_assert(nothrow_compare_v<alg::detail::a_star_entry<V, double>>);
static_assert(!noexcept(alg::detail::default_weight_fn{}(std::declval<throwing_edge const&>())));
static_assert(noexcept(alg::detail::default_weight_fn{}(std::declval<plain_edge const&>())));
static_assert(
  !noexcept(std::declval<alg::floyd_warshall_result<V, throwing_weight> const&>().at(0, 0))
);
static_assert(noexcept(std::declval<alg::floyd_warshall_result<V, double> const&>().at(0, 0)));

}  // namespace
