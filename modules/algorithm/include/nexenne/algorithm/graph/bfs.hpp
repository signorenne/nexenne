#pragma once

/**
 * @file
 * @brief Breadth-first traversal over container::graph.
 *
 * Walks every vertex reachable from a source layer by layer using a
 * queue-based BFS and a bitset_dynamic for constant-time visited checks.
 */

#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/container/bitset_dynamic.hpp>
#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::algorithm {

/**
 * @brief Visits every vertex reachable from \p source in BFS order.
 *
 * Invokes \p visit exactly once per reachable vertex, in order of
 * non-decreasing graph distance from \p source. A \p visit callback that returns
 * \c bool stops the traversal early when it returns \c false; a \c void callback
 * always continues.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Visitor Callable invocable as \c f(V) returning \c void or \c bool.
 *
 * @param g Graph to traverse.
 * @param source Starting vertex.
 * @param visit Per-vertex callback invoked once per reachable vertex in BFS
 *        order.
 *
 * @return An empty success, or \c container_error::out_of_range when \p source
 *         is not a valid vertex of \p g.
 *
 * @pre \p visit is invocable as \c visit(V) returning \c void or \c bool.
 * @post On success, every vertex reachable from \p source was passed to \p visit
 *       exactly once unless a \c bool callback short-circuited the traversal;
 *       \p g is not modified.
 *
 * @complexity \c O(V + E) time and \c O(V) auxiliary space.
 */
template <typename E, std::unsigned_integral V, typename Visitor>
auto bfs(nexenne::container::graph<E, V> const& g, V const source, Visitor&& visit)
  -> std::expected<void, nexenne::container::container_error> {
  if (!g.contains(source)) {
    return std::unexpected{nexenne::container::container_error::out_of_range};
  }

  auto visited{nexenne::container::bitset_dynamic(g.vertex_count())};
  auto queue{std::vector<V>{}};
  queue.reserve(g.vertex_count());
  auto head{std::size_t{0}};

  queue.push_back(source);
  nexenne::utility::discard(visited.set(static_cast<std::size_t>(source)));

  while (head < queue.size()) {
    auto const u{queue[head]};
    head += 1;
    if constexpr (std::is_same_v<std::invoke_result_t<Visitor&, V>, bool>) {
      if (!visit(u)) {
        return {};
      }
    } else {
      visit(u);
    }
    for (auto const& edge : g.edges_of(u)) {
      auto const target_idx{static_cast<std::size_t>(edge.target)};
      if (!visited[target_idx]) {
        nexenne::utility::discard(visited.set(target_idx));
        queue.push_back(edge.target);
      }
    }
  }
  return {};
}

}  // namespace nexenne::algorithm
