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
#include <nexenne/utility/discard.hpp>

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;
using V = std::uint32_t;

auto main() -> int {
  // A weighted DAG: 0 -> 1 -> 3, 0 -> 2 -> 3. Two paths to vertex 3, costs 3 and 5.
  auto g{nc::graph<double, V>(4)};
  g.add_edge(0, 1, 1.0);
  g.add_edge(0, 2, 4.0);
  g.add_edge(1, 3, 2.0);
  g.add_edge(2, 3, 1.0);

  // bfs visits in nondecreasing hop count; the visitor is called once per vertex.
  std::printf("bfs from 0     :");
  nexenne::utility::discard(alg::bfs(g, V{0}, [](V const u) { std::printf(" %u", u); }));
  std::printf("\n");

  // dfs goes deep before wide; same O(V + E) cost, different visit order.
  std::printf("dfs from 0     :");
  nexenne::utility::discard(alg::dfs(g, V{0}, [](V const u) { std::printf(" %u", u); }));
  std::printf("\n");

  // dijkstra: single-source shortest paths with non-negative weights, the
  // O((V+E) log V) workhorse. The shortest 0->3 is 1 + 2 = 3, not the 4 + 1 = 5
  // route, even though the latter has a cheaper final edge.
  auto const dist{alg::dijkstra(g, V{0})};
  std::printf("dijkstra 0->3  = %.1f\n", dist.value()[3]);

  // bellman_ford is slower (O(V*E)) but the right call when weights may be
  // negative; it also reports a negative cycle (here: none, so the result holds).
  auto neg{nc::graph<double, V>(3)};
  neg.add_edge(0, 1, 4.0);
  neg.add_edge(0, 2, 5.0);
  neg.add_edge(1, 2, -3.0);  // a negative edge dijkstra could not handle
  if (auto const bf{alg::bellman_ford(neg, V{0})}) {
    std::printf("bellman 0->2   = %.1f  (via 1: 4 + -3 = 1)\n", bf.value()[2]);
  } else {
    std::printf("bellman: negative cycle reachable\n");
  }

  // floyd_warshall: all-pairs shortest paths in one O(V^3) pass, worth it when
  // you need the full distance matrix rather than one source. at(i, j) indexes it.
  if (auto const fw{alg::floyd_warshall(g)}; fw.has_value()) {
    std::printf("floyd 0->3     = %.1f   1->3 = %.1f\n", fw->at(0, 3), fw->at(1, 3));
  }

  // a_star: goal-directed shortest path. With a zero heuristic it equals
  // dijkstra; a non-overestimating (admissible) heuristic prunes the frontier.
  // Here the heuristic is 0, so we recover the optimal 0->3 path and its cost.
  auto const astar{alg::a_star(g, V{0}, V{3}, [](V) { return 0.0; })};
  if (astar.has_value()) {
    std::printf("a_star 0->3    : cost %.1f path", astar->cost);
    for (auto const v : astar->path) {
      std::printf(" %u", v);
    }
    std::printf("\n");
  }

  // topological_sort: a build order where every edge points forward; errors on a
  // cycle. For this DAG, 0 precedes 1 and 2, which precede 3.
  auto const order{alg::topological_sort(g)};
  std::printf("topo order     :");
  for (auto const v : order.value()) {
    std::printf(" %u", v);
  }
  std::printf("\n");

  // kruskal_mst: the cheapest set of edges connecting every vertex, O(E log E).
  // It treats edges as undirected weighted triples and greedily unions endpoints.
  auto const mst{alg::kruskal_mst(g)};
  std::printf("mst edges      :");
  for (auto const& e : mst) {
    std::printf(" (%u-%u:%.0f)", e.from, e.to, e.weight);
  }
  std::printf("\n");

  // connected_components: dense per-vertex labels via union-find, O(V + E*alpha).
  // Add an isolated vertex 4 to split the graph into two components.
  auto cc_graph{g};
  nexenne::utility::discard(cc_graph.add_vertex());
  auto const cc{alg::connected_components(cc_graph)};
  std::printf("components     = %zu  (vertex 4 stands alone)\n", cc.num_components);

  // tarjan_scc: strongly connected components in one O(V + E) pass. A DAG has
  // none larger than a single vertex, so the count equals the vertex count.
  std::printf(
    "scc count      = %zu  (a DAG, so all singletons)\n", alg::tarjan_scc(g).num_components
  );
  return 0;
}
