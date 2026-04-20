#pragma once

/**
 * @file
 * @brief convex_hull3: a convex polytope as a non-owning view over its vertices.
 *
 * A \c convex_hull3 represents the convex hull of a vertex set, the shape a
 * stretched elastic band would take around the points. It does not compute or
 * store faces, edges, or normals, and it does not run a hull algorithm: it holds
 * a \c std::span over caller-owned vertices and assumes they already describe a
 * convex polytope. The single operation it exposes is the one a collision engine
 * actually needs, the support function: given a direction, the vertex furthest
 * along it. That is all GJK and EPA ever ask of a shape.
 *
 * Like \c polygon2, the type is a non-owning view, so the geometry module stays
 * allocation-free while still handling an arbitrary vertex count. The caller
 * keeps the vertices in whatever storage suits them (a \c std::array on the
 * stack, a \c std::vector they own, a slice of a mesh buffer) and the hull
 * borrows a read-only window onto them. Everything is \c constexpr and
 * \c noexcept.
 *
 * Aliases: \c convex_hull3, \c convex_hull3_f (float), \c convex_hull3_d
 * (double).
 */

#include <concepts>
#include <cstddef>
#include <span>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Non-owning view of a 3D convex polytope, given by its vertices.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class convex_hull3 {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, 3>;
  using span_type = std::span<point_type const>;

private:
  span_type m_vertices{};

public:
  /**
   * @brief Constructs an empty hull (no vertices).
   */
  constexpr convex_hull3() noexcept = default;

  /**
   * @brief Constructs a hull viewing the given vertices.
   *
   * The vertices are assumed to be those of a convex polytope; interior or
   * duplicate points are harmless but wasted work, and a non-convex set yields
   * silently wrong collision results. No validation is performed and nothing is
   * copied: the hull is valid only while \p vertices outlives it.
   *
   * @param vertices Caller-owned vertex set.
   */
  constexpr explicit convex_hull3(span_type vertices) noexcept : m_vertices{vertices} {}

  /**
   * @brief The viewed vertices.
   *
   * @return A span over the caller-owned vertices.
   */
  [[nodiscard]] constexpr auto vertices() const noexcept -> span_type {
    return m_vertices;
  }

  /**
   * @brief Support function: the vertex furthest along \p direction.
   *
   * Returns the vertex maximising the dot product with \p direction, that is,
   * the surface point a plane with normal \p direction touches last as it
   * sweeps in from infinity. For a polytope the support is always at a vertex
   * (faces and edges tied on the dot product just resolve to one of their
   * vertices), so a linear scan over the vertex set computes it exactly. This is
   * the only primitive GJK and EPA require of a shape, which is why the hull
   * stores nothing else.
   *
   * The scan is \c O(N). Hulls used in physics typically have N up to roughly 30
   * vertices, so this is fine; for larger hulls the standard optimisation is
   * hill-climbing over a vertex-adjacency list, which can be added later without
   * changing this signature. An empty hull returns the origin.
   *
   * See E. G. Gilbert, D. W. Johnson, S. S. Keerthi, "A fast procedure for
   * computing the distance between complex objects in three-dimensional space",
   * IEEE J. Robotics and Automation 4(2), 1988, which introduced the
   * support-function view of convex shapes.
   *
   * @param direction Search direction (need not be unit length).
   *
   * @return Vertex maximising the dot product with \p direction; the origin for
   *         an empty hull.
   *
   * @pre None. An empty hull is handled.
   * @post For a non-empty hull the result is one of \c vertices().
   * @complexity \c O(N) in the vertex count.
   */
  [[nodiscard]] constexpr auto support(point_type const& direction) const noexcept -> point_type {
    if (m_vertices.empty()) {
      return point_type{};
    }
    auto best{m_vertices[0]};
    auto best_dot{nexenne::math::dot(best, direction)};
    for (auto i{std::size_t{1}}; i < m_vertices.size(); ++i) {
      auto const d{nexenne::math::dot(m_vertices[i], direction)};
      if (d > best_dot) {
        best_dot = d;
        best = m_vertices[i];
      }
    }
    return best;
  }
};

using convex_hull3_f = convex_hull3<float>;
using convex_hull3_d = convex_hull3<double>;

/**
 * @brief Support function as a free function, for ADL dispatch.
 *
 * GJK and EPA call \c support as a free function so they can dispatch on the
 * shape type through argument-dependent lookup: any type with a
 * \c support(shape, direction) overload in \c nexenne::geometry becomes usable
 * with them. This overload is the extension point for \c convex_hull3; it
 * forwards to the member \c support.
 *
 * @tparam Real Component type.
 * @param hull Convex hull.
 * @param direction Search direction (need not be unit length).
 *
 * @return Support vertex of \p hull along \p direction.
 *
 * @pre None. An empty hull is handled.
 * @post For a non-empty hull the result is one of \c hull.vertices().
 * @complexity \c O(N) in the vertex count.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  convex_hull3<Real> const& hull, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  return hull.support(direction);
}

/**
 * @brief Smallest axis-aligned box enclosing every vertex of the hull.
 *
 * Folds every vertex through \c expand_to_include starting from \c empty_aabb,
 * the same construction \c polygon2 uses. The broad phase uses this to place the
 * hull's conservative bound.
 *
 * @tparam Real Component type.
 * @param hull Convex hull.
 *
 * @return Tight bounding box of the vertices; \c empty() is \c true when the
 *         hull has no vertices.
 *
 * @pre None.
 * @post The result contains every vertex of \p hull.
 * @complexity \c O(N) in the vertex count.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto bounding_aabb(convex_hull3<Real> const& hull
) noexcept -> aabb<Real, 3> {
  auto result{empty_aabb<Real, 3>()};
  for (auto const& v : hull.vertices()) {
    result = expand_to_include(result, v);
  }
  return result;
}

}  // namespace nexenne::geometry
