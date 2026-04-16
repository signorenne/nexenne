#pragma once

/**
 * @file
 * @brief Support mappings for the analytic primitives, so GJK and EPA work on them.
 *
 * GJK and EPA never inspect a shape's faces or edges: the only thing they ask of
 * a shape is its support function, the surface point furthest along a given
 * direction. \c convex_hull3 already supplies one (a linear scan over its
 * vertices); this header gives the analytic primitives their own \c support
 * overloads so that the same collision pipeline runs on a \c sphere3, an
 * \c obb3, a \c capsule3, an \c aabb (3D), a \c triangle3, or a \c segment3,
 * and on any mixed pair of them.
 *
 * Each overload is a free function in \c nexenne::geometry returning a
 * \c vector<Real, 3>, which is exactly the protocol the \c convex_shape concept
 * (in concepts.hpp) requires; GJK and EPA find them through argument-dependent
 * lookup. The closed-form support of each shape is the standard one:
 *
 *   - sphere:   center + r * dir_hat                  (the cap furthest along d)
 *   - capsule:  furthest spine endpoint + r * dir_hat (a sphere swept along a
 *               segment, so its support is the segment's support grown by r)
 *   - obb:      center + sum_k sign(axis_k . d) * h_k * axis_k
 *   - aabb:     per-axis min or max corner by the sign of d
 *   - triangle: the vertex maximising the dot product with d
 *   - segment:  the endpoint maximising the dot product with d
 *
 * For shapes with a flat face or straight edge the support is a vertex (ties on
 * the dot product resolve to one of them), so a small argmax is exact. For the
 * rounded shapes (sphere, capsule) it adds the radius along the unit direction.
 * Everything is \c constexpr and \c noexcept; nothing allocates.
 *
 * Reference: C. Ericson, Real-Time Collision Detection (Morgan Kaufmann 2005),
 * section 9.5.2 on support mappings; G. van den Bergen, Collision Detection in
 * Interactive 3D Environments (Morgan Kaufmann 2003).
 */

#include <array>
#include <concepts>
#include <cstddef>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Support point of a sphere: the boundary point furthest along \p direction.
 *
 * The cap of the ball in the search direction, \c center + radius * dir_hat. When
 * \p direction is (near) zero the direction is arbitrary, so the \c +x cap is
 * returned by convention; this only happens on a degenerate seed, which GJK
 * avoids in practice.
 *
 * @tparam Real Floating-point component type.
 * @param s Sphere.
 * @param direction Search direction (need not be unit length).
 *
 * @return The boundary point of \p s furthest along \p direction.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result lies on the sphere's boundary.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const dir_hat{nexenne::math::normalize_or(
    direction, nexenne::math::vector<Real, 3>{Real{1}, Real{0}, Real{0}}
  )};
  return s.center() + dir_hat * s.radius();
}

/**
 * @brief Support point of a capsule: its spine's support grown by the radius.
 *
 * A capsule is a sphere of radius \c r swept along its spine, so its support is
 * the spine endpoint furthest along \p direction pushed out by \c r * dir_hat.
 *
 * @tparam Real Floating-point component type.
 * @param c Capsule (3D).
 * @param direction Search direction (need not be unit length).
 *
 * @return The boundary point of \p c furthest along \p direction.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result lies on the capsule's boundary.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  capsule<Real, 3> const& c, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const tip{
    nexenne::math::dot(c.start(), direction) >= nexenne::math::dot(c.end(), direction) ? c.start()
                                                                                       : c.end()
  };
  auto const dir_hat{nexenne::math::normalize_or(
    direction, nexenne::math::vector<Real, 3>{Real{1}, Real{0}, Real{0}}
  )};
  return tip + dir_hat * c.radius();
}

/**
 * @brief Support point of an oriented box: the corner furthest along \p direction.
 *
 * Folds each local half-axis in by the sign of its projection on \p direction:
 * \c center + sum_k sign(axis_k . d) * h_k * axis_k, where the axes are the
 * columns of the box's rotation. Choosing the sign per axis selects the furthest
 * of the eight corners without enumerating them.
 *
 * @tparam Real Floating-point component type.
 * @param box Oriented box (3D).
 * @param direction Search direction (need not be unit length).
 *
 * @return The corner of \p box furthest along \p direction.
 *
 * @pre \c box.rotation() has unit length.
 * @post The result is one of the box's eight corners.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  obb3<Real> const& box, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  using vector_type = nexenne::math::vector<Real, 3>;
  auto result{box.center()};
  auto const h{box.half_size()};
  // The local axes are the box rotation applied to the world basis vectors.
  auto const axes{std::array<vector_type, 3>{
    nexenne::math::rotate(box.rotation(), vector_type{Real{1}, Real{0}, Real{0}}),
    nexenne::math::rotate(box.rotation(), vector_type{Real{0}, Real{1}, Real{0}}),
    nexenne::math::rotate(box.rotation(), vector_type{Real{0}, Real{0}, Real{1}}),
  }};
  for (auto k{std::size_t{0}}; k < 3; ++k) {
    auto const sign{nexenne::math::dot(axes[k], direction) >= Real{0} ? Real{1} : Real{-1}};
    result = result + axes[k] * (sign * h[k]);
  }
  return result;
}

/**
 * @brief Support point of an axis-aligned box: the corner furthest along \p direction.
 *
 * Picks, per axis, the \c max corner when the direction component is
 * non-negative and the \c min corner otherwise.
 *
 * @tparam Real Floating-point component type.
 * @param box Axis-aligned box (3D).
 * @param direction Search direction (need not be unit length).
 *
 * @return The corner of \p box furthest along \p direction.
 *
 * @pre \p box is well-formed (\c min <= max componentwise).
 * @post The result is one of the box's eight corners.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  aabb<Real, 3> const& box, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  auto result{nexenne::math::vector<Real, 3>{}};
  for (auto k{std::size_t{0}}; k < 3; ++k) {
    result[k] = direction[k] >= Real{0} ? box.max()[k] : box.min()[k];
  }
  return result;
}

/**
 * @brief Support point of a triangle: the vertex furthest along \p direction.
 *
 * @tparam Real Floating-point component type.
 * @param t Triangle (3D).
 * @param direction Search direction (need not be unit length).
 *
 * @return The vertex of \p t maximising the dot product with \p direction.
 *
 * @pre None.
 * @post The result is one of \c t.a(), \c t.b(), \c t.c().
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  triangle<Real, 3> const& t, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  auto best{t.a()};
  auto best_dot{nexenne::math::dot(t.a(), direction)};
  auto const consider{[&](nexenne::math::vector<Real, 3> const& v) noexcept {
    auto const d{nexenne::math::dot(v, direction)};
    if (d > best_dot) {
      best_dot = d;
      best = v;
    }
  }};
  consider(t.b());
  consider(t.c());
  return best;
}

/**
 * @brief Support point of a segment: the endpoint furthest along \p direction.
 *
 * Degenerate (zero-volume) on its own, but a useful support shape: the Minkowski
 * sum of a segment with another convex shape recovers a swept query.
 *
 * @tparam Real Floating-point component type.
 * @param s Segment (3D).
 * @param direction Search direction (need not be unit length).
 *
 * @return \c s.start() or \c s.end(), whichever maximises the dot product.
 *
 * @pre None.
 * @post The result is one of the two endpoints.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto support(
  segment<Real, 3> const& s, nexenne::math::vector<Real, 3> const& direction
) noexcept -> nexenne::math::vector<Real, 3> {
  return nexenne::math::dot(s.start(), direction) >= nexenne::math::dot(s.end(), direction)
           ? s.start()
           : s.end();
}

}  // namespace nexenne::geometry
