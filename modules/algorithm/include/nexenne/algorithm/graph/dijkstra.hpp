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

namespace nexenne::algorithm {

namespace detail {

template <typename Weight>
[[nodiscard]] constexpr auto unreachable_weight() noexcept -> Weight {
  if constexpr (std::numeric_limits<Weight>::has_infinity) {
    return std::numeric_limits<Weight>::infinity();
  } else {
    return std::numeric_limits<Weight>::max();
  }
}

struct default_weight_fn {
  template <typename Edge>
  [[nodiscard]] constexpr auto operator()(Edge const& e) const noexcept -> decltype(e.data) {
    return e.data;
  }
};

/// PQ entry. Lives at namespace scope so that the friend operator<=> is
/// well-formed (friend declarations in local classes are ill-formed under
/// C++23).
template <std::unsigned_integral V, typename Weight>
struct dijkstra_entry {
  V vertex{};
  Weight distance{};

  [[nodiscard]] friend constexpr auto
  operator==(dijkstra_entry const&, dijkstra_entry const&) noexcept -> bool = default;

  [[nodiscard]] friend constexpr auto
  operator<=>(dijkstra_entry const& a, dijkstra_entry const& b) noexcept {
    return a.distance <=> b.distance;
  }
};

}  // namespace detail

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
 *      weights break the optimality of the result.
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
  auto handles{std::vector<std::uint32_t>(n, no_h)};

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
      auto const candidate{d_u + w};
      auto const target{static_cast<std::size_t>(edge.target)};
      if (candidate < distances[target]) {
        distances[target] = candidate;
        if (handles[target] != no_h) {
          static_cast<void>(pq.update(handles[target], entry{edge.target, candidate}));
        } else {
          handles[target] = pq.push(entry{edge.target, candidate});
        }
      }
    }
  }
  return distances;
}

}  // namespace nexenne::algorithm
