#pragma once

/**
 * @file
 * @brief 2D polygon (polygon2) as a non-owning view over caller-owned vertices.
 *
 * \c polygon2 wraps a \c std::span over the caller's vertex storage (a
 * \c std::array or \c std::vector), so the geometry module stays allocation-free
 * while supporting an arbitrary vertex count. The queries (shoelace area,
 * perimeter, area-weighted centroid, ray-cast containment, convexity, and the
 * axis-aligned bound) assume a simple, non-self-intersecting polygon with at
 * least three vertices. Everything is \c constexpr and \c noexcept.
 *
 * Aliases: \c polygon2, \c polygon2_f (float), \c polygon2_d (double).
 */

#include <concepts>
#include <cstddef>
#include <limits>
#include <span>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::geometry {

/**
 * @brief Non-owning 2D polygon view over a span of caller-owned vertices.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class polygon2 {
public:
  using value_type = Real;                                  ///< Scalar coordinate type.
  using point_type = nexenne::math::vector<value_type, 2>;  ///< Vertex type, a 2D vector.
  using span_type = std::span<point_type const>;            ///< Read-only view over the vertices.

private:
  span_type m_vertices{};

public:
  /**
   * @brief Constructs an empty polygon view.
   */
  constexpr polygon2() noexcept = default;

  /**
   * @brief Constructs a polygon viewing the given vertices.
   *
   * @param vertices Caller-owned vertices, in winding order.
   */
  constexpr explicit polygon2(span_type vertices) noexcept : m_vertices{vertices} {}

  /**
   * @brief The viewed vertices.
   *
   * @return A span over the caller-owned vertices.
   */
  [[nodiscard]] constexpr auto vertices() const noexcept -> span_type {
    return m_vertices;
  }
};

/// @brief Single-precision polygon view.
using polygon2_f = polygon2<float>;
/// @brief Double-precision polygon view.
using polygon2_d = polygon2<double>;

/**
 * @brief Signed area via the shoelace formula.
 *
 * Positive for counter-clockwise winding, negative for clockwise, zero when
 * degenerate (collinear vertices or fewer than three). The cross terms are
 * taken relative to the first vertex, so a polygon far from the origin keeps
 * its area.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return The signed area.
 *
 * @pre The polygon is simple (non-self-intersecting).
 * @post Positive for counter-clockwise winding, negative for clockwise, zero
 *       when degenerate.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto signed_area(polygon2<Real> const poly) noexcept -> Real {
  auto const n{poly.vertices().size()};
  if (n < 3) {
    return Real{0};
  }
  // Each cross term is twice the signed area of the triangle from a reference
  // point through an edge; any reference gives the same exact sum. The first
  // vertex keeps the terms at the polygon's own size: from the origin they grow
  // with the distance squared and cancel away the area (a float unit square at
  // (1e4, 1e4) would have none).
  auto const& origin{poly.vertices()[0]};
  auto sum{Real{0}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    auto const a{poly.vertices()[i] - origin};
    auto const b{poly.vertices()[(i + 1) % n] - origin};
    sum += a.x() * b.y() - b.x() * a.y();
  }
  return sum * Real{0.5};
}

/**
 * @brief Unsigned area: \c |signed_area|.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return The non-negative area.
 *
 * @pre The polygon is simple (non-self-intersecting).
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(polygon2<Real> const poly) noexcept -> Real {
  return nexenne::math::abs(signed_area(poly));
}

/**
 * @brief Sum of the edge lengths.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return The perimeter.
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto perimeter(polygon2<Real> const poly) noexcept -> Real {
  auto const n{poly.vertices().size()};
  if (n < 2) {
    return Real{0};
  }
  auto sum{Real{0}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    auto const& a{poly.vertices()[i]};
    auto const& b{poly.vertices()[(i + 1) % n]};
    sum += nexenne::math::length(b - a);
  }
  return sum;
}

/**
 * @brief Area-weighted centroid of a simple polygon.
 *
 * The standard polygon-centroid formula, valid for non-convex but
 * non-self-intersecting polygons, evaluated relative to the first vertex so a
 * polygon far from the origin keeps its precision. Falls back to the vertex
 * arithmetic mean when the polygon is degenerate: its summed cross products are
 * no larger than their rounding error, so the area carries no sign or size.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return The centroid position.
 *
 * @pre The polygon has at least three vertices and is simple.
 * @post For a non-degenerate polygon the result lies in its convex hull;
 *       degenerate input, including collinear vertices whose area is rounding
 *       noise, falls back to the vertex arithmetic mean.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto centroid(polygon2<Real> const poly) noexcept
  -> nexenne::math::vector<Real, 2> {
  auto const n{poly.vertices().size()};
  if (n == 0) {
    return nexenne::math::vector<Real, 2>{};
  }
  // Standard simple-polygon centroid. Each edge (p0, p1) contributes its 2D
  // cross product (twice the signed area of the triangle origin-p0-p1) as the
  // weight on the edge-midpoint sum: Cx = sum((x0 + x1) * cross) / (3 * 2A),
  // likewise Cy, where a6 accumulates 6A (six times the signed area). The /6 and
  // /3 fold into the final inv. A zero-area (collinear or self-cancelling) loop
  // falls back to the plain vertex mean below. See Bourke,
  // "Calculating the area and centroid of a polygon" (1988), and the polygon
  // centroid formula on Wikipedia. The formula runs on coordinates relative to
  // the first vertex, which is added back at the end: absolute coordinates far
  // from the origin cancel the cross terms away (see signed_area).
  auto const& origin{poly.vertices()[0]};
  auto cx{Real{0}};
  auto cy{Real{0}};
  auto a6{Real{0}};
  // Sum of the product magnitudes behind every cross term: a6's rounding
  // error is at most a few epsilons of it per term. A collinear loop leaves a6
  // at that noise level (5.6e-17 in one case), not at zero, and dividing by it
  // throws the centroid far outside the hull.
  auto magnitude{Real{0}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    auto const p0{poly.vertices()[i] - origin};
    auto const p1{poly.vertices()[(i + 1) % n] - origin};
    auto const cross_v{p0.x() * p1.y() - p1.x() * p0.y()};
    cx += (p0.x() + p1.x()) * cross_v;
    cy += (p0.y() + p1.y()) * cross_v;
    a6 += cross_v;
    magnitude += nexenne::math::abs(p0.x() * p1.y()) + nexenne::math::abs(p1.x() * p0.y());
  }
  auto const noise{static_cast<Real>(2 * n) * std::numeric_limits<Real>::epsilon() * magnitude};
  if (nexenne::math::abs(a6) <= noise) {
    auto mean{nexenne::math::vector<Real, 2>{}};
    for (auto const& v : poly.vertices()) {
      mean = mean + v;
    }
    return mean * (Real{1} / static_cast<Real>(n));
  }
  auto const inv{Real{1} / (Real{3} * a6)};
  return origin + nexenne::math::vector<Real, 2>{cx * inv, cy * inv};
}

/**
 * @brief Ray-casting point-in-polygon test (odd-parity rule).
 *
 * Counts crossings of a rightward horizontal ray from \p p and returns \c true
 * on an odd count. Works for non-convex simple polygons. Behaviour exactly on
 * the boundary is unspecified.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 * @param p Query point.
 *
 * @return \c true when \p p is inside.
 *
 * @pre The polygon is simple (non-self-intersecting).
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
contains_point(polygon2<Real> const poly, nexenne::math::vector<Real, 2> const p) noexcept -> bool {
  auto const n{poly.vertices().size()};
  if (n < 3) {
    return false;
  }
  // Crossing-number rule: shoot a ray from p in the +x direction and count how
  // many polygon edges it crosses; an odd count means p is inside. Each edge runs
  // between vj (previous vertex) and vi (current); j = i++ walks them as a closed
  // loop. crosses_y uses the half-open test (exactly one endpoint strictly above
  // p.y) so a vertex shared by two edges is counted once, not zero or twice.
  // x_intersect is the edge's x where it meets the horizontal line y = p.y; the
  // +x ray hits the edge only when p is to its left, and each hit flips inside.
  // This is W. R. Franklin's PNPOLY test (the half-open vertex treatment is his).
  auto inside{false};
  for (auto i{std::size_t{0}}, j{n - 1}; i < n; j = i++) {
    auto const& vi{poly.vertices()[i]};
    auto const& vj{poly.vertices()[j]};
    auto const crosses_y{(vi.y() > p.y()) != (vj.y() > p.y())};
    if (crosses_y) {
      auto const x_intersect{vi.x() + (p.y() - vi.y()) * (vj.x() - vi.x()) / (vj.y() - vi.y())};
      if (p.x() < x_intersect) {
        inside = !inside;
      }
    }
  }
  return inside;
}

/**
 * @brief Reports whether the polygon is convex.
 *
 * Tests that all consecutive edge cross-products share a sign and that the edge
 * directions turn through exactly one full turn: a star polygon (a pentagram)
 * turns the same way at every vertex but winds twice, so it is not convex.
 * Returns \c false for degenerate input (fewer than three vertices, or fully
 * collinear vertices that never establish a turn direction).
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return \c true when the polygon is convex.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto convex(polygon2<Real> const poly) noexcept -> bool {
  auto const n{poly.vertices().size()};
  if (n < 3) {
    return false;
  }
  auto sign_seen{0};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    auto const& a{poly.vertices()[i]};
    auto const& b{poly.vertices()[(i + 1) % n]};
    auto const& c{poly.vertices()[(i + 2) % n]};
    auto const cross_v{(b.x() - a.x()) * (c.y() - b.y()) - (b.y() - a.y()) * (c.x() - b.x())};
    if (cross_v > Real{0}) {
      if (sign_seen == -1) {
        return false;
      }
      sign_seen = 1;
    } else if (cross_v < Real{0}) {
      if (sign_seen == 1) {
        return false;
      }
      sign_seen = -1;
    }
  }
  if (sign_seen == 0) {
    return false;
  }
  // Every turn now has one sign and is under a half turn, so the edge
  // directions rotate one way and their total rotation is a whole number of
  // turns. Count the turns without trig (this stays constexpr): each crossing
  // of a direction from the lower half-plane (y < 0, or y == 0 and x <= 0) into
  // the upper one is one full turn. A convex polygon makes exactly one; a
  // pentagram makes two. Zero-length edges (repeated vertices) have no
  // direction and are skipped.
  using point_type = nexenne::math::vector<Real, 2>;
  auto const upper{[](point_type const e) noexcept -> bool {
    return e.y() > Real{0} || (e.y() == Real{0} && e.x() > Real{0});
  }};
  auto const edge{[&poly, n](std::size_t const i) noexcept -> point_type {
    return poly.vertices()[(i + 1) % n] - poly.vertices()[i];
  }};
  auto const zero{[](point_type const e) noexcept -> bool {
    return e.x() == Real{0} && e.y() == Real{0};
  }};
  auto last{n - 1};
  while (zero(edge(last))) {
    --last;  // a turn exists, so some edge is non-zero.
  }
  auto was_upper{upper(edge(last))};
  auto turns{std::size_t{0}};
  for (auto i{std::size_t{0}}; i < n; ++i) {
    auto const e{edge(i)};
    if (zero(e)) {
      continue;
    }
    auto const is_upper{upper(e)};
    if (is_upper && !was_upper) {
      ++turns;
    }
    was_upper = is_upper;
  }
  return turns == 1;
}

/**
 * @brief Smallest axis-aligned box around the vertices.
 *
 * @tparam Real Component type.
 * @param poly Polygon.
 *
 * @return The bounding box; \c empty() is \c true when the polygon has no
 *         vertices.
 *
 * @pre None.
 * @post The result contains every vertex.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto bounding_aabb(polygon2<Real> const poly) noexcept -> aabb<Real, 2> {
  auto result{empty_aabb<Real, 2>()};
  for (auto const& v : poly.vertices()) {
    result = expand_to_include(result, v);
  }
  return result;
}

}  // namespace nexenne::geometry
