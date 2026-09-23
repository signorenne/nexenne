/**
 * @file
 * @brief Example: the nexenne::algorithm graph algorithms.
 *
 * Builds small graphs over container::graph and tours the catalogue: a traversal
 * (bfs, dfs), single-source shortest paths (dijkstra, bellman_ford with negative
 * weights), all-pairs paths (floyd_warshall), a goal-directed search (a_star), a
 * build order (topological_sort), a minimum spanning tree (kruskal_mst),
 * connectivity (connected_components), and strong components (tarjan_scc). Each
 * fallible call returns expected/optional, handled inline.
 *
 * The main graph is a weighted DAG with two routes to vertex 3: 0 -> 1 -> 3
 * costs 1 + 2 = 3 and 0 -> 2 -> 3 costs 4 + 1 = 5. The program walks eight
 * steps:
 *
 * 1. bfs visits in nondecreasing hop count and dfs goes deep before wide; both
 *    call the visitor once per vertex in O(V + E).
 * 2. dijkstra, the O((V + E) log V) single-source workhorse for non-negative
 *    weights, picks the cost-3 route although the other ends on the cheaper
 *    edge.
 * 3. bellman_ford, O(V * E), handles a negative edge dijkstra cannot and would
 *    report a reachable negative cycle as an error.
 * 4. floyd_warshall builds the full all-pairs matrix in one O(V^3) pass, worth
 *    it when every source matters; at(i, j) reads it back.
 * 5. a_star is goal-directed: with the zero heuristic used here it equals
 *    dijkstra, and an admissible (never overestimating) heuristic prunes the
 *    frontier.
 * 6. topological_sort gives a build order with every edge pointing forward and
 *    fails on a cycle.
 * 7. kruskal_mst treats edges as undirected weighted triples and greedily
 *    unions their endpoints, O(E log E).
 * 8. connected_components labels vertices via union-find, O(V + E * alpha), and
 *    an added isolated vertex 4 forms a second component; tarjan_scc finds only
 *    singletons, since a DAG has no cycle.
 */

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <nexenne/algorithm/graph/a_star.hpp>
#include <nexenne/algorithm/graph/bellman_ford.hpp>
#include <nexenne/algorithm/graph/bfs.hpp>
#include <nexenne/algorithm/graph/connected_components.hpp>
#include <nexenne/algorithm/graph/dfs.hpp>
#include <nexenne/algorithm/graph/dijkstra.hpp>
#include <nexenne/algorithm/graph/floyd_warshall.hpp>
#include <nexenne/algorithm/graph/kruskal_mst.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/graph/topological_sort.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/utility/ignore.hpp>

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;
using V = std::uint32_t;

auto main() -> int {
  auto g{nc::graph<double, V>{}};
  for (auto i{0}; i < 4; ++i) {
    if (!g.add_vertex()) {
      return 1;
    }
  }
  nexenne::utility::ignore(g.add_edge(0, 1, 1.0));
  nexenne::utility::ignore(g.add_edge(0, 2, 4.0));
  nexenne::utility::ignore(g.add_edge(1, 3, 2.0));
  nexenne::utility::ignore(g.add_edge(2, 3, 1.0));

  std::printf("bfs from 0     :");
  nexenne::utility::ignore(alg::bfs(g, V{0}, [](V const u) { std::printf(" %u", u); }));
  std::printf("\n");

  std::printf("dfs from 0     :");
  nexenne::utility::ignore(alg::dfs(g, V{0}, [](V const u) { std::printf(" %u", u); }));
  std::printf("\n");

  auto const dist{alg::dijkstra(g, V{0})};
  std::printf("dijkstra 0->3  = %.1f\n", dist.value()[3]);

  auto neg{nc::graph<double, V>{}};
  for (auto i{0}; i < 3; ++i) {
    if (!neg.add_vertex()) {
      return 1;
    }
  }
  nexenne::utility::ignore(neg.add_edge(0, 1, 4.0));
  nexenne::utility::ignore(neg.add_edge(0, 2, 5.0));
  nexenne::utility::ignore(neg.add_edge(1, 2, -3.0));
  if (auto const bf{alg::bellman_ford(neg, V{0})}) {
    std::printf("bellman 0->2   = %.1f  (via 1: 4 + -3 = 1)\n", bf.value()[2]);
  } else {
    std::printf("bellman: negative cycle reachable\n");
  }

  if (auto const fw{alg::floyd_warshall(g)}; fw.has_value()) {
    std::printf("floyd 0->3     = %.1f   1->3 = %.1f\n", fw->at(0, 3), fw->at(1, 3));
  }

  auto const astar{alg::a_star(g, V{0}, V{3}, [](V) { return 0.0; })};
  if (astar.has_value()) {
    std::printf("a_star 0->3    : cost %.1f path", astar->cost);
    for (auto const v : astar->path) {
      std::printf(" %u", v);
    }
    std::printf("\n");
  }

  auto const order{alg::topological_sort(g)};
  std::printf("topo order     :");
  for (auto const v : order.value()) {
    std::printf(" %u", v);
  }
  std::printf("\n");

  auto const mst{alg::kruskal_mst(g)};
  std::printf("mst edges      :");
  for (auto const& e : mst) {
    std::printf(" (%u-%u:%.0f)", e.from, e.to, e.weight);
  }
  std::printf("\n");

  auto cc_graph{g};
  nexenne::utility::ignore(cc_graph.add_vertex());
  auto const cc{alg::connected_components(cc_graph)};
  std::printf("components     = %zu  (vertex 4 stands alone)\n", cc.num_components);

  std::printf(
    "scc count      = %zu  (a DAG, so all singletons)\n", alg::tarjan_scc(g).num_components
  );
  return 0;
}
