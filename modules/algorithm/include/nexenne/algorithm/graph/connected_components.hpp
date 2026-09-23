#pragma once

/**
 * @file
 * @brief Computes connected components of a graph read as undirected.
 *
 * A container::graph is directed, but each directed edge is treated as if it
 * also existed in reverse, yielding the connected components of the underlying
 * undirected graph via container::union_find.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include <nexenne/container/graph.hpp>
#include <nexenne/container/union_find.hpp>
#include <nexenne/utility/ignore.hpp>

namespace nexenne::algorithm {

/**
 * @brief Result of connected_components: per-vertex labels and a count.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 */
template <typename E, std::unsigned_integral V>
struct components_result {
  using value_type = V;  ///< Component-label type carried per vertex.

  std::vector<V> labels;          ///< Dense component label for each vertex.
  std::size_t num_components{0};  ///< Number of distinct components.
};

/**
 * @brief Labels every vertex of \p g with its connected component.
 *
 * Unites the endpoints of every edge with union-by-size and path halving, then
 * renumbers the component roots to dense identifiers in \c [0, num_components) so
 * the labels index directly into a per-component array. The graph is
 * interpreted as undirected.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @param g Graph, interpreted as undirected for this computation.
 *
 * @return A \c components_result whose \c labels[v] is the dense component
 *         identifier of vertex \c v and whose \c num_components is the count of
 *         distinct components.
 *
 * @pre None.
 * @post \c labels has size \c g.vertex_count(); two vertices share a label if and
 *       only if they are connected, and every label lies in
 *       \c [0, num_components). \p g is not modified.
 *
 * @complexity \c O((V + E) * alpha(V)) time and \c O(V) auxiliary space, where
 *             alpha is the inverse Ackermann function.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto connected_components(nexenne::container::graph<E, V> const& g)
  -> components_result<E, V> {
  auto const n{g.vertex_count()};
  // make_set cannot fail: the graph holds at most max(V) vertices, union_find's own bound.
  auto uf{nexenne::container::union_find<V>{}};
  uf.reserve(n);
  for (auto i{std::size_t{0}}; i < n; ++i) {
    nexenne::utility::ignore(uf.make_set());
  }

  for (auto const u : g.vertices()) {
    for (auto const& edge : g.edges_of(u)) {
      nexenne::utility::ignore(uf.unite(u, edge.target));
    }
  }

  // size_t counters cannot wrap max(V) -> 0; each label is < n <= max(V), so the cast is exact.
  auto labels{std::vector<V>(n, V{0})};
  auto remap{std::vector<V>(n, V{0})};
  auto seen{std::vector<std::uint8_t>(n, 0)};
  auto next{std::size_t{0}};

  for (auto v{std::size_t{0}}; v < n; ++v) {
    auto const root{uf.root_of(static_cast<V>(v))};
    if (!seen[root]) {
      seen[root] = 1;
      remap[root] = static_cast<V>(next);
      next += 1;
    }
    labels[v] = remap[root];
  }

  return components_result<E, V>{
    .labels = std::move(labels),
    .num_components = next,
  };
}

}  // namespace nexenne::algorithm
