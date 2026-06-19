#pragma once

/**
 * @file
 * @brief Topological sort of a directed graph via Kahn's algorithm.
 *
 * Counts in-degrees, repeatedly emits in-degree-zero vertices, and decrements
 * successors. A graph with a cycle leaves vertices unemitted and is reported as
 * an error.
 */

#include <concepts>
#include <cstddef>
#include <expected>
#include <utility>
#include <vector>

#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>

namespace nexenne::algorithm {

/**
 * @brief Orders the vertices of \p g so every edge points forward.
 *
 * Returns a permutation of the vertex IDs in which, for every edge (u, v),
 * vertex u precedes vertex v. When \p g contains a cycle no valid ordering exists
 * and an error is returned.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @param g Directed graph to sort.
 *
 * @return A topologically ordered vector of every vertex ID, or
 *         \c container_error::not_found when \p g contains a cycle.
 *
 * @pre None.
 * @post On success the result is a permutation of \c [0, g.vertex_count())
 *       respecting every edge direction; on failure \p g contains at least one
 *       cycle. \p g is not modified.
 *
 * @complexity \c O(V + E) time and \c O(V) auxiliary space.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto topological_sort(nexenne::container::graph<E, V> const& g
) -> std::expected<std::vector<V>, nexenne::container::container_error> {
  auto const n{g.vertex_count()};
  auto in_degree{std::vector<std::size_t>(n, 0)};

  for (auto const u : g.vertices()) {
    for (auto const& edge : g.edges_of(u)) {
      ++in_degree[edge.target];
    }
  }

  auto queue{std::vector<V>{}};
  queue.reserve(n);
  auto head{std::size_t{0}};

  for (auto const u : g.vertices()) {
    if (in_degree[u] == 0) {
      queue.push_back(u);
    }
  }

  auto order{std::vector<V>{}};
  order.reserve(n);

  while (head < queue.size()) {
    auto const u{queue[head]};
    head += 1;
    order.push_back(u);
    for (auto const& edge : g.edges_of(u)) {
      in_degree[edge.target] -= 1;
      if (in_degree[edge.target] == 0) {
        queue.push_back(edge.target);
      }
    }
  }

  if (order.size() != n) {
    // Cycle: at least one vertex never reached in-degree zero.
    return std::unexpected{nexenne::container::container_error::not_found};
  }
  return order;
}

/**
 * @brief Reports whether \p g is acyclic.
 *
 * Equivalent to \c topological_sort(g).has_value() but discards the order. Useful
 * for dependency-graph validation where the ordering itself is not needed.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 *
 * @param g Directed graph to test.
 *
 * @return \c true when \p g has no directed cycle, \c false otherwise.
 *
 * @pre None.
 * @post \p g is not modified.
 *
 * @complexity \c O(V + E) time and \c O(V) auxiliary space.
 */
template <typename E, std::unsigned_integral V>
[[nodiscard]] auto is_acyclic(nexenne::container::graph<E, V> const& g) -> bool {
  return topological_sort(g).has_value();
}

}  // namespace nexenne::algorithm
