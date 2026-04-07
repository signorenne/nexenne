#pragma once

/**
 * @file
 * @brief Iterative depth-first traversal over container::graph.
 *
 * Stack-based rather than recursive, so deep graphs that would overflow a
 * recursive call stack still traverse, bounded by heap rather than thread-stack
 * size.
 */

#include <concepts>
#include <cstddef>
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
 * @brief Visits every vertex reachable from \p source in pre-order DFS.
 *
 * Invokes \p visit once per reachable vertex the first time it is popped from the
 * work stack, descending edges in their iteration order. A \p visit callback that
 * returns \c bool stops the traversal early when it returns \c false; a \c void
 * callback always continues.
 *
 * @tparam E Edge payload type (or void).
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Visitor Callable invocable as \c f(V) returning \c void or \c bool.
 *
 * @param g Graph to traverse.
 * @param source Starting vertex.
 * @param visit Per-vertex callback in pre-order DFS sequence.
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
auto dfs(nexenne::container::graph<E, V> const& g, V const source, Visitor&& visit)
  -> std::expected<void, nexenne::container::container_error> {
  if (!g.contains(source)) {
    return std::unexpected{nexenne::container::container_error::out_of_range};
  }

  auto visited{nexenne::container::bitset_dynamic(g.vertex_count())};
  auto stack{std::vector<V>{}};
  stack.reserve(g.vertex_count());

  stack.push_back(source);

  while (!stack.empty()) {
    auto const u{stack.back()};
    stack.pop_back();

    auto const u_idx{static_cast<std::size_t>(u)};
    if (visited[u_idx]) {
      continue;
    }
    nexenne::utility::discard(visited.set(u_idx));

    if constexpr (std::is_same_v<std::invoke_result_t<Visitor&, V>, bool>) {
      if (!visit(u)) {
        return {};
      }
    } else {
      visit(u);
    }

    // Push neighbours in reverse order so iteration order matches recursive DFS
    // (first neighbour processed first).
    auto const adj{g.edges_of(u)};
    for (auto i{adj.size()}; i > 0; i -= 1) {
      auto const target{adj[i - 1].target};
      if (!visited[static_cast<std::size_t>(target)]) {
        stack.push_back(target);
      }
    }
  }
  return {};
}

}  // namespace nexenne::algorithm
