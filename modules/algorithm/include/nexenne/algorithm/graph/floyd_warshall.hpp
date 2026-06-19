#pragma once

/**
 * @file
 * @brief All-pairs shortest paths via the Floyd-Warshall dynamic program.
 *
 * Builds a dense V-by-V distance matrix in O(V^3) regardless of edge density,
 * handling negative weights and detecting negative cycles. Prefer it over V
 * Dijkstra runs on dense graphs or small V.
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
 * @brief Result of floyd_warshall: a dense distance matrix.
 *
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Signed numeric type for accumulated costs.
 */
template <std::unsigned_integral V, typename Weight>
struct floyd_warshall_result {
  std::vector<Weight> distances;  ///< Row-major V-by-V distance matrix.
  std::size_t n{0};               ///< Side length, equal to the vertex count.

  /**
   * @brief Distance of the shortest path from \p i to \p j.
   *
   * @param i Source vertex.
   * @param j Target vertex.
   *
   * @return The shortest-path cost from \p i to \p j, or the unreachable sentinel
   *         when no path exists.
   *
   * @pre \p i and \p j are both less than \c n. Larger indices read outside the
   *      matrix and are undefined behaviour.
   * @post The result is unchanged by the call.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto at(V const i, V const j) const noexcept -> Weight {
    return distances[static_cast<std::size_t>(i) * n + static_cast<std::size_t>(j)];
  }
};

/**
 * @brief Computes the all-pairs shortest-path matrix of \p g.
 *
 * Seeds the matrix with direct edges (keeping the smallest parallel edge), runs
 * the classical triple loop, and reports a negative cycle if any diagonal entry
 * becomes negative. The per-edge weight comes from \p weight_of, which defaults to
 * returning \c edge.data.
 *
 * @tparam E Edge payload type.
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Signed numeric type for accumulated costs; default \c double.
 * @tparam WeightFn Callable \c (edge_record) -> Weight; default returns \c edge.data.
 *
 * @param g Graph to analyse.
 * @param weight_of Edge-weight extractor; weights may be negative.
 *
 * @return A \c floyd_warshall_result holding the V-by-V distance matrix, or
 *         \c container_error::not_found when \p g contains a negative cycle.
 *
 * @pre \c Weight is a signed type able to hold accumulated path costs without
 *      overflow.
 * @post On success, every \c result.at(i, j) is the shortest-path cost from
 *       \c i to \c j, and \c result.at(i, i) == 0. \p g is not modified.
 *
 * @complexity \c O(V^3) time and \c O(V^2) space.
 */
template <
  typename E,
  std::unsigned_integral V,
  typename Weight = double,
  typename WeightFn = detail::default_weight_fn>
[[nodiscard]] auto floyd_warshall(nexenne::container::graph<E, V> const& g, WeightFn weight_of = {})
  -> std::expected<floyd_warshall_result<V, Weight>, nexenne::container::container_error> {
  using err = nexenne::container::container_error;
  using fwr = floyd_warshall_result<V, Weight>;

  auto const n{g.vertex_count()};
  auto const inf{detail::unreachable_weight<Weight>()};

  auto d{std::vector<Weight>(n * n, inf)};
  auto const idx{[n](std::size_t i, std::size_t j) noexcept { return i * n + j; }};

  // Diagonal: distance from a vertex to itself is zero.
  for (std::size_t i{0}; i < n; i += 1) {
    d[idx(i, i)] = Weight{0};
  }

  // Seed with direct edges. Parallel edges keep the smallest.
  for (auto const u : g.vertices()) {
    for (auto const& edge : g.edges_of(u)) {
      auto const w{static_cast<Weight>(weight_of(edge))};
      auto const cell{idx(static_cast<std::size_t>(u), static_cast<std::size_t>(edge.target))};
      if (w < d[cell]) {
        d[cell] = w;
      }
    }
  }

  // Classical triple loop. k is the outer index so that on iteration k,
  // d[i][j] holds the shortest path using intermediates from {0..k-1}.
  for (std::size_t k{0}; k < n; k += 1) {
    for (std::size_t i{0}; i < n; i += 1) {
      auto const dik{d[idx(i, k)]};
      if (dik == inf) {
        continue;
      }
      for (std::size_t j{0}; j < n; j += 1) {
        auto const dkj{d[idx(k, j)]};
        if (dkj == inf) {
          continue;
        }
        auto const cand{dik + dkj};
        if (cand < d[idx(i, j)]) {
          d[idx(i, j)] = cand;
        }
      }
    }
  }

  // Negative cycle detector: a vertex on a negative cycle ends up with a path
  // back to itself of negative weight.
  for (std::size_t i{0}; i < n; i += 1) {
    if (d[idx(i, i)] < Weight{0}) {
      return std::unexpected{err::not_found};
    }
  }

  return fwr{
    .distances = std::move(d),
    .n = static_cast<std::size_t>(n),
  };
}

}  // namespace nexenne::algorithm
