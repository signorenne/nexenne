#pragma once

/**
 * @file
 * @brief Single-source shortest paths tolerating negative edge weights.
 *
 * Relaxes every edge up to V-1 times; a relaxation on the V-th pass proves a
 * negative-weight cycle reachable from the source. Use it when some weights are
 * negative or when a negative cycle must be detected rather than assumed absent.
 */

#include <concepts>
#include <cstddef>
#include <expected>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/algorithm/graph/dijkstra.hpp>  // re-uses detail helpers
#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>

namespace nexenne::algorithm {

/**
 * @brief Shortest-path distances from \p source allowing negative weights.
 *
 * Performs up to V-1 relaxation passes, exiting early when a pass changes
 * nothing, then a final pass that reports any negative-weight cycle. The
 * per-edge weight comes from \p weight_of, which defaults to returning
 * \c edge.data.
 *
 * @tparam E Edge payload type.
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Signed numeric type for accumulated costs; default \c double.
 * @tparam WeightFn Callable \c (edge_record) -> Weight; default returns \c edge.data.
 *
 * @param g Graph to traverse.
 * @param source Starting vertex.
 * @param weight_of Edge-weight extractor; weights may be negative.
 *
 * @return A distance vector indexed by vertex ID, with unreached vertices set
 *         to infinity for floating-point \c Weight or \c max() for integral
 *         \c Weight; \c container_error::out_of_range when \p source is invalid;
 *         \c container_error::not_found when a negative cycle reachable from
 *         \p source exists.
 *
 * @pre \c Weight is a signed type able to represent the accumulated path costs
 *      without overflow.
 * @post On success, \c distances[source] == 0 and each finite entry is the weight
 *       of a shortest path from \p source. \p g is not modified.
 *
 * @complexity \c O(V * E) time and \c O(V) auxiliary space.
 */
template <
  typename E,
  std::unsigned_integral V,
  typename Weight = double,
  typename WeightFn = detail::default_weight_fn>
[[nodiscard]] auto bellman_ford(
  nexenne::container::graph<E, V> const& g, V const source, WeightFn weight_of = {}
) -> std::expected<std::vector<Weight>, nexenne::container::container_error> {
  using err = nexenne::container::container_error;

  if (!g.contains(source)) {
    return std::unexpected{err::out_of_range};
  }

  auto const n{g.vertex_count()};
  auto distances{std::vector<Weight>(n, detail::unreachable_weight<Weight>())};
  distances[source] = Weight{0};

  // V-1 relaxation passes. Early-exit when a pass changes nothing because no
  // further relaxation can produce a shorter path.
  for (std::size_t i{0}; i + 1 < n; i += 1) {
    auto changed{false};
    for (auto const u : g.vertices()) {
      if (distances[u] == detail::unreachable_weight<Weight>()) {
        continue;
      }
      for (auto const& edge : g.edges_of(u)) {
        auto const w{static_cast<Weight>(weight_of(edge))};
        auto const candidate{distances[u] + w};
        auto const target{static_cast<std::size_t>(edge.target)};
        if (candidate < distances[target]) {
          distances[target] = candidate;
          changed = true;
        }
      }
    }
    if (!changed) {
      return distances;
    }
  }

  // V-th pass: any further relaxation means a negative cycle is reachable from
  // source.
  for (auto const u : g.vertices()) {
    if (distances[u] == detail::unreachable_weight<Weight>()) {
      continue;
    }
    for (auto const& edge : g.edges_of(u)) {
      auto const w{static_cast<Weight>(weight_of(edge))};
      auto const candidate{distances[u] + w};
      auto const target{static_cast<std::size_t>(edge.target)};
      if (candidate < distances[target]) {
        return std::unexpected{err::not_found};
      }
    }
  }

  return distances;
}

}  // namespace nexenne::algorithm
