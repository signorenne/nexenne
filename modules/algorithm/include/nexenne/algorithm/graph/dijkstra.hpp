#pragma once

/**
 * @file
 * @brief Single-source shortest paths on non-negative edge weights.
 *
 * Classical Dijkstra backed by the indexed_priority_queue from the container
 * module, relaxing out-edges in O(log V) per update.
 */

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

#include <nexenne/container/error.hpp>
#include <nexenne/container/graph.hpp>
#include <nexenne/container/indexed_priority_queue.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::algorithm {

/// @cond INTERNAL
namespace detail {

/**
 * @brief The sentinel weight standing for an unreached vertex.
 *
 * Uses positive infinity for a floating-point \c Weight and the type maximum
 * otherwise, so an unreached distance always compares greater than any real one.
 *
 * @tparam Weight Numeric type for accumulated distances.
 *
 * @return Positive infinity for a type with an infinity, else its maximum.
 *
 * @pre None.
 * @post None.
 */
template <typename Weight>
[[nodiscard]] constexpr auto unreachable_weight() noexcept -> Weight {
  if constexpr (std::numeric_limits<Weight>::has_infinity) {
    return std::numeric_limits<Weight>::infinity();
  } else {
    return std::numeric_limits<Weight>::max();
  }
}

/**
 * @brief Default edge-weight extractor returning an edge's \c data member.
 *
 * The fallback \c WeightFn for the shortest-path routines when the caller does
 * not supply one, so an edge whose payload is its weight needs no extractor.
 *
 * @pre None.
 * @post None.
 */
struct default_weight_fn {
  /**
   * @brief Returns the weight stored in \p e as its \c data member.
   *
   * @tparam Edge Edge record type exposing a \c data member.
   * @param e Edge record to read.
   *
   * @return The edge's \c data member.
   *
   * @pre None.
   * @post None.
   */
  template <typename Edge>
  [[nodiscard]] constexpr auto operator()(Edge const& e) const noexcept -> decltype(e.data) {
    return e.data;
  }
};

/**
 * @brief Priority-queue entry for Dijkstra's algorithm.
 *
 * Lives at namespace scope so the friend \c operator<=> is well-formed (friend
 * declarations in local classes are ill-formed under C++23).
 */
template <std::unsigned_integral V, typename Weight>
struct dijkstra_entry {
  V vertex{};
  Weight distance{};

  /**
   * @brief Tests two entries equal on their distance ordering key.
   *
   * Equality is on the distance alone, kept consistent with the distance-only
   * \c operator<=> so equal-distance entries never disagree.
   *
   * @param a First entry.
   * @param b Second entry.
   *
   * @return \c true when the two entries share a distance.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(dijkstra_entry const& a, dijkstra_entry const& b) noexcept -> bool {
    return a.distance == b.distance;
  }

  /**
   * @brief Orders two entries by their distance.
   *
   * @param a First entry.
   * @param b Second entry.
   *
   * @return The three-way ordering of the two distances.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(dijkstra_entry const& a, dijkstra_entry const& b) noexcept {
    return a.distance <=> b.distance;
  }
};

}  // namespace detail
/// @endcond

/**
 * @brief Shortest-path distances from \p source over non-negative weights.
 *
 * Relaxes out-edges in best-first order, popping the closest pending vertex
 * from an indexed priority queue. The per-edge weight comes from \p weight_of,
 * which defaults to returning \c edge.data.
 *
 * @tparam E Edge payload type.
 * @tparam V Unsigned-integer vertex ID type.
 * @tparam Weight Numeric type for accumulated distances; default \c double.
 * @tparam WeightFn Callable \c (edge_record) -> Weight; default returns \c edge.data.
 *
 * @param g Graph to traverse.
 * @param source Starting vertex.
 * @param weight_of Extracts a non-negative weight per edge.
 *
 * @return A distance vector indexed by vertex ID, with unreached vertices set
 *         to infinity for floating-point \c Weight or \c max() for integral
 *         \c Weight, or \c container_error::out_of_range when \p source is not a
 *         valid vertex of \p g.
 *
 * @pre Every edge weight produced by \p weight_of is non-negative; negative
 *      weights break the optimality of the result. \c Weight can represent every
 *      accumulated path cost without overflow (a sum that would reach or exceed
 *      the unreachable sentinel is treated as unreachable, not wrapped).
 * @post On success, \c distances[source] == 0 and each finite entry is the weight
 *       of a shortest path from \p source to that vertex. \p g is not modified.
 *
 * @complexity \c O((V + E) log V) time and \c O(V) auxiliary space.
 */
template <
  typename E,
  std::unsigned_integral V,
  typename Weight = double,
  typename WeightFn = detail::default_weight_fn>
[[nodiscard]] auto dijkstra(
  nexenne::container::graph<E, V> const& g, V const source, WeightFn weight_of = {}
) -> std::expected<std::vector<Weight>, nexenne::container::container_error> {
  if (!g.contains(source)) {
    return std::unexpected{nexenne::container::container_error::out_of_range};
  }

  using entry = detail::dijkstra_entry<V, Weight>;

  auto const n{g.vertex_count()};
  auto distances{std::vector<Weight>(n, detail::unreachable_weight<Weight>())};
  distances[source] = Weight{0};

  auto pq{nexenne::container::indexed_priority_queue<entry, std::greater<>>{}};

  using pq_type = decltype(pq);
  auto constexpr no_h{pq_type::invalid_handle};
  auto handles{std::vector<typename pq_type::handle_type>(n, no_h)};

  handles[source] = pq.push(entry{source, Weight{0}});

  while (!pq.empty()) {
    auto popped{pq.pop()};
    if (!popped.has_value()) {
      break;
    }
    auto const u{popped->vertex};
    auto const d_u{popped->distance};
    handles[u] = no_h;

    if (d_u > distances[u]) {
      continue;  // stale entry obsoleted by a later update
    }
    for (auto const& edge : g.edges_of(u)) {
      auto const w{static_cast<Weight>(weight_of(edge))};
      // Saturating overflow guard: a candidate d_u + w that would reach or pass
      // the unreachable sentinel (integral overflow, and the sentinel value
      // itself, which stays reserved for "unreached") cannot improve any real
      // distance, so skip the edge instead of wrapping to a bogus small cost.
      if (w > detail::unreachable_weight<Weight>() - d_u) {
        continue;
      }
      auto const candidate{static_cast<Weight>(d_u + w)};
      auto const target{static_cast<std::size_t>(edge.target)};
      if (candidate < distances[target]) {
        distances[target] = candidate;
        if (handles[target] != no_h) {
          nexenne::utility::discard(pq.update(handles[target], entry{edge.target, candidate}));
        } else {
          handles[target] = pq.push(entry{edge.target, candidate});
        }
      }
    }
  }
  return distances;
}

}  // namespace nexenne::algorithm
