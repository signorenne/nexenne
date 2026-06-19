/**
 * @file
 * @brief Example: the nexenne::algorithm graph algorithms.
 *
 * Builds a small weighted graph over container::graph and runs a traversal,
 * single-source shortest paths, a topological sort, and SCC detection.
 */

#include <cstddef>
#include <cstdint>
#include <cstdio>

#include <nexenne/algorithm/graph/bfs.hpp>
#include <nexenne/algorithm/graph/dijkstra.hpp>
#include <nexenne/algorithm/graph/tarjan_scc.hpp>
#include <nexenne/algorithm/graph/topological_sort.hpp>
#include <nexenne/container/graph.hpp>

namespace alg = nexenne::algorithm;
namespace nc = nexenne::container;
using V = std::uint32_t;

auto main() -> int {
  // A weighted DAG: 0 -> 1 -> 3, 0 -> 2 -> 3.
  auto g{nc::graph<double, V>(4)};
  g.add_edge(0, 1, 1.0);
  g.add_edge(0, 2, 4.0);
  g.add_edge(1, 3, 2.0);
  g.add_edge(2, 3, 1.0);

  std::printf("bfs from 0     :");
  static_cast<void>(alg::bfs(g, V{0}, [](V const u) { std::printf(" %u", u); }));
  std::printf("\n");

  auto const dist{alg::dijkstra(g, V{0})};
  std::printf("dijkstra 0->3  = %.1f\n", dist.value()[3]);  // 1 + 2 = 3

  auto const order{alg::topological_sort(g)};
  std::printf("topo order     :");
  for (auto const v : order.value()) {
    std::printf(" %u", v);
  }
  std::printf("\n");

  std::printf("scc count      = %zu\n", alg::tarjan_scc(g).num_components);  // 4 (a DAG)
  return 0;
}
