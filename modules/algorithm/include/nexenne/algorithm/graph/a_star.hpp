#pragma once

/**
 * @file
 * @brief A* heuristic shortest-path search.
 *
 * A* generalises Dijkstra by keying the priority queue on f(v) = g(v) + h(v),
 * where g is the best-known cost from the source and h estimates the remaining
 * cost to the goal, so it expands the vertex that looks closest to the goal
 * first.
 */

#include <algorithm>
#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/algorithm/graph/dijkstra.hpp>  // re-uses detail helpers
#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::algorithm {

/**
 * @brief Result of an A* search: the optimal path and its total cost.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric type for accumulated costs.
 */
template <std::unsigned_integral V, typename Weight>
struct a_star_result {
  std::vector<V> path;  ///< Vertices from source to goal, both inclusive.
  Weight cost{};        ///< Total cost of path.
};

namespace detail {

/**
 * @brief Priority-queue entry for A*.
 *
 * Same shape as the Dijkstra entry but ordered by the f-value (g + h) rather
 * than the raw distance.
 */
template <std::unsigned_integral V, typename Weight>
struct a_star_entry {
  V vertex{};
  Weight f_score{};

  [[nodiscard]] friend constexpr auto
  operator==(a_star_entry const&, a_star_entry const&) noexcept -> bool = default;

  [[nodiscard]] friend constexpr auto
  operator<=>(a_star_entry const& a, a_star_entry const& b) noexcept {
    return a.f_score <=> b.f_score;
  }
};

}  // namespace detail

/**
 * @brief Finds a least-cost path from \p source to \p goal using A*.
 *
 * Expands vertices in order of g(v) + \p heuristic(v) and reconstructs the path
 * through a parent map once \p goal is popped. With an admissible \p heuristic the
 * path found is optimal; a \p heuristic returning 0 reduces the search to
 * Dijkstra. The per-edge weight comes from \p weight_of, which defaults to
 * returning \c edge.data.
 *
 * @tparam E Edge payload type.
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric type for accumulated costs; default \c double.
 * @tparam HeuristicFn Callable \c (V) -> Weight estimating remaining cost to goal.
 * @tparam WeightFn Callable \c (edge_record) -> Weight; default returns \c edge.data.
 *
 * @param g Graph to search.
 * @param source Starting vertex.
 * @param goal Target vertex.
 * @param heuristic Estimator from each vertex to goal.
 * @param weight_of Edge-weight extractor.
 *
 * @return An \c a_star_result with the path from \p source to \p goal inclusive
 *         and its total cost; \c container_error::out_of_range when \p source or
 *         \p goal is not a valid vertex; \c container_error::not_found when
 *         \p goal is unreachable.
 *
 * @pre Every edge weight is non-negative and \p heuristic is admissible (never
 *      overestimates the true remaining cost) for the returned path to be
 *      optimal.
 * @post On success, \c result.path begins at \p source, ends at \p goal, and
 *       \c result.cost equals the sum of its edge weights. \p g is not modified.
 *
 * @complexity \c O((V + E) log V) worst case; a sharp \p heuristic prunes most of
 *             the search in practice.
 */
template <
  typename E,
  std::unsigned_integral V,
  typename Weight = double,
  typename HeuristicFn,
  typename WeightFn = detail::default_weight_fn>
[[nodiscard]] auto a_star(
  nexenne::container::graph<E, V> const& g,
  V const source,
  V const goal,
  HeuristicFn heuristic,
  WeightFn weight_of = {}
) -> std::expected<a_star_result<V, Weight>, nexenne::container::container_error> {
  using err = nexenne::container::container_error;
  using entry = detail::a_star_entry<V, Weight>;

  if (!g.contains(source) || !g.contains(goal)) {
    return std::unexpected{err::out_of_range};
  }

  auto const n{g.vertex_count()};
  auto constexpr no_parent{std::numeric_limits<V>::max()};

  auto g_score{std::vector<Weight>(n, detail::unreachable_weight<Weight>())};
  auto came_from{std::vector<V>(n, no_parent)};

  auto pq{nexenne::container::indexed_priority_queue<entry, std::greater<>>{}};
  using pq_type = decltype(pq);
  auto constexpr no_h{pq_type::invalid_handle};
  auto handles{std::vector<std::uint32_t>(n, no_h)};

  // The heuristic is a pure function of the vertex, so evaluate it at most once
  // per vertex and reuse the value; an expensive heuristic is then not recomputed
  // on every relaxation (O(V) evaluations instead of O(E)).
  auto h_value{std::vector<Weight>(n)};
  auto h_seen{std::vector<std::uint8_t>(n, 0)};
  auto const h_of{[&](V const v) -> Weight {
    auto const vi{static_cast<std::size_t>(v)};
    if (!h_seen[vi]) {
      h_value[vi] = static_cast<Weight>(heuristic(v));
      h_seen[vi] = 1;
    }
    return h_value[vi];
  }};

  g_score[source] = Weight{0};
  handles[source] = pq.push(entry{source, h_of(source)});

  while (!pq.empty()) {
    auto popped{pq.pop()};
    if (!popped.has_value()) {
      break;
    }
    auto const u{popped->vertex};
    auto const f_u{popped->f_score};
    handles[u] = no_h;

    // Stale entry guard: if we popped a value that's worse than what we now
    // know, ignore.
    if (f_u > g_score[u] + h_of(u)) {
      continue;
    }

    if (u == goal) {
      // Reconstruct path goal to source via came_from chain.
      auto path{std::vector<V>{}};
      path.reserve(16);
      auto v{goal};
      while (true) {
        path.push_back(v);
        if (v == source) {
          break;
        }
        v = came_from[v];
      }
      std::ranges::reverse(path);
      return a_star_result<V, Weight>{
        .path = std::move(path),
        .cost = g_score[goal],
      };
    }

    for (auto const& edge : g.edges_of(u)) {
      auto const target{edge.target};
      auto const target_i{static_cast<std::size_t>(target)};
      auto const w{static_cast<Weight>(weight_of(edge))};
      auto const tentative{g_score[u] + w};
      if (tentative < g_score[target_i]) {
        came_from[target_i] = u;
        g_score[target_i] = tentative;
        auto const f{tentative + h_of(target)};
        if (handles[target_i] != no_h) {
          nexenne::utility::discard(pq.update(handles[target_i], entry{target, f}));
        } else {
          handles[target_i] = pq.push(entry{target, f});
        }
      }
    }
  }

  return std::unexpected{err::not_found};
}

}  // namespace nexenne::algorithm
