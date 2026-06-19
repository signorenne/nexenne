#pragma once

/**
 * @file
 * @brief Kruskal minimum spanning tree of an undirected weighted graph.
 *
 * Treats the directed container::graph as undirected, sorts all edges by
 * weight, and adds each edge that joins two distinct components, tested via
 * container::union_find.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

#include <nexenne/container/graph.hpp>
#include <nexenne/container/union_find.hpp>

namespace nexenne::algorithm {

/**
 * @brief An edge of a minimum spanning tree: endpoints and weight.
 *
 * @tparam E Edge payload (weight) type.
 * @tparam V Unsigned-integer vertex ID type.
 */
template <typename E, std::unsigned_integral V>
struct mst_edge {
  V from;    ///< Source endpoint of the edge.
  V to;      ///< Target endpoint of the edge.
  E weight;  ///< Edge weight.
};

/**
 * @brief Builds the minimum spanning tree (or forest) of \p g.
 *
 * Flattens the directed edges to weighted triples, sorts them ascending by
 * weight, and greedily keeps each edge whose endpoints lie in different
 * components. A connected graph of V vertices yields V-1 edges; a disconnected
 * graph yields a spanning forest with one tree per component. Edge weights are
 * compared with \c <.
 *
 * @tparam E Edge payload (weight) type, comparable with \c <.
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @param g Graph to build the spanning tree from.
 *
 * @return The edges forming the minimum spanning tree or forest, in the order
 *         they were added.
 *
 * @pre Edge weights of type \c E are comparable with \c <.
 * @post The result contains no cycle and, for a connected \p g, spans every
 *       vertex with V-1 edges of minimum total weight. \p g is not modified.
 *
 * @complexity \c O(E log E) time, dominated by the sort, and \c O(E) auxiliary
 *             space.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto kruskal_mst(nexenne::container::graph<E, V> const& g
) -> std::vector<mst_edge<E, V>> {
  auto const n{g.vertex_count()};

  // Flatten directed edges to (u, v, weight) triples.
  auto edges{std::vector<mst_edge<E, V>>{}};
  edges.reserve(g.edge_count());
  for (auto const u : g.vertices()) {
    for (auto const& edge : g.edges_of(u)) {
      edges.push_back(mst_edge<E, V>{u, edge.target, edge.data});
    }
  }

  // Sort by weight (ascending). Standard MST primitive.
  std::ranges::sort(edges, [](auto const& a, auto const& b) noexcept {
    return a.weight < b.weight;
  });

  auto uf{nexenne::container::union_find<V>{n}};
  auto result{std::vector<mst_edge<E, V>>{}};
  result.reserve(n > 0 ? n - 1 : 0);

  for (auto const& e : edges) {
    auto const united{uf.unite(e.from, e.to)};
    if (united.has_value() && *united) {
      // Different components before this edge, add it to the MST.
      result.push_back(e);
      if (result.size() == n - 1) {
        break;  // tree complete
      }
    }
  }
  return result;
}

}  // namespace nexenne::algorithm
