#pragma once

/**
 * @file
 * @brief Gilbert-Johnson-Keerthi (GJK) convex overlap test via support functions.
 *
 * GJK answers "do these two convex shapes overlap?" without ever looking at
 * their faces or edges. It needs one operation from each shape, the support
 * function (the surface point furthest along a direction), and builds a simplex
 * inside the Minkowski difference A (-) B; the shapes overlap exactly when that
 * simplex can be made to enclose the origin. The terminal simplex is what EPA
 * picks up to recover the penetration normal and depth.
 *
 * Any type with an ADL-found \c support(shape, direction) overload in
 * \c nexenne::geometry is usable here: \c convex_hull3 provides one, and the
 * analytic primitives can each supply their own. The \c convex_shape concept
 * (in concepts.hpp) spells out that requirement.
 *
 * Convention: a \c gjk_simplex3 stores its vertices newest-first, so
 * \c points[0] is always the most recently added one, and \c count is the
 * simplex dimension (1 point, 2 line, 3 triangle, 4 tetrahedron). Everything is
 * \c constexpr and \c noexcept; the algorithm allocates nothing (the simplex is
 * a fixed four-element array).
 */

#include <array>
#include <concepts>
#include <cstddef>

#include <nexenne/geometry/concepts.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief A vertex of the Minkowski difference, kept with the per-shape support
 *        points that generated it.
 *
 * Storing the original support points alongside the Minkowski-difference vertex
 * lets EPA reconstruct world-space contact points via barycentric weights once
 * the closest face is known.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct gjk_minkowski_point3 {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  point_type difference{};  ///< Minkowski-difference point: support_a - support_b.
  point_type support_a{};   ///< Support point chosen on shape A (world space).
  point_type support_b{};   ///< Support point chosen on shape B (world space).
};

/**
 * @brief The GJK working simplex: up to four Minkowski-difference vertices.
 *
 * \c points[0] is always the most recently added vertex; only the first
 * \c count entries are valid. \c count is the simplex dimension (1 = point,
 * 2 = line, 3 = triangle, 4 = tetrahedron).
 *
 * Kept as a documented aggregate rather than an encapsulated class: it is the
 * algorithm's transient working state and the gjk-to-epa handoff, the same role
 * the standard library gives its \c *_result aggregates (\c from_chars_result).
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct gjk_simplex3 {
  using value_type = Real;
  using point_type = gjk_minkowski_point3<Real>;

  std::array<point_type, 4> points{};  ///< Vertices, newest first; only [0, count) valid.
  std::size_t count{0};                ///< Number of valid vertices, 0 to 4.
};

/**
 * @brief Outcome of a GJK run: the overlap flag, the terminal simplex, and the
 *        iteration count.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct gjk_result3 {
  using value_type = Real;

  bool overlap{false};           ///< True when the two shapes overlap.
  gjk_simplex3<Real> simplex{};  ///< Terminal simplex; a tetrahedron on overlap.
  std::size_t iterations{0};     ///< Iterations consumed before terminating.
};

namespace detail {

/**
 * @brief Tests whether two vectors point into the same half-space.
 *
 * @tparam Real Component type.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return \c true when \c dot(a, b) is strictly positive.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto same_direction(
  nexenne::math::vector<Real, 3> const& a, nexenne::math::vector<Real, 3> const& b
) noexcept -> bool {
  return nexenne::math::dot(a, b) > Real{0};
}

/**
 * @brief Any vector orthogonal to \p v, well-conditioned for every non-zero \p v.
 *
 * Crosses \p v with whichever world axis is least aligned with it (the axis of
 * \p v's smallest-magnitude component). Picking the least-aligned axis keeps the
 * two cross operands far from parallel, so the result is never a near-zero
 * vector, which the naive "cross with a fixed axis" can produce. Used by GJK
 * when the origin lies exactly on a simplex edge and the usual triple-cross
 * search direction collapses to zero.
 *
 * See J. F. Hughes and T. Moller, "Building an Orthonormal Basis from a Unit
 * Vector", Journal of Graphics Tools 4(4), 1999, for the least-aligned-axis idea.
 *
 * @tparam Real Component type.
 * @param v Non-zero vector to find an orthogonal of.
 *
 * @return A vector orthogonal to \p v (zero only if \p v is zero).
 *
 * @pre \p v is non-zero.
 * @post \c dot(result, v) is zero up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto any_orthogonal(nexenne::math::vector<Real, 3> const& v
) noexcept -> nexenne::math::vector<Real, 3> {
  using vector_type = nexenne::math::vector<Real, 3>;
  auto const ax{nexenne::math::abs(v.x())};
  auto const ay{nexenne::math::abs(v.y())};
  auto const az{nexenne::math::abs(v.z())};
  auto const axis{
    (ax <= ay && ax <= az) ? vector_type{Real{1}, Real{0}, Real{0}}
    : (ay <= az)           ? vector_type{Real{0}, Real{1}, Real{0}}
                           : vector_type{Real{0}, Real{0}, Real{1}}
  };
  return nexenne::math::cross(v, axis);
}

/**
 * @brief Updates the simplex and search direction toward the origin; returns
 *        \c true when the simplex (a tetrahedron) encloses the origin.
 *
 * Implements the classic Casey Muratori formulation: the newest vertex is at
 * index 0, and each step keeps only the part of the simplex whose Voronoi region
 * faces the origin, then aims the next search there. The line-reduction step is
 * shared by the line and both triangle-edge cases through \c reduce_to_edge.
 *
 * @tparam Real Component type.
 * @param simplex Working simplex, updated in place.
 * @param direction Search direction, updated in place.
 *
 * @return \c true when the origin is enclosed (overlap); \c false to continue.
 *
 * @pre \c simplex.count is 2, 3, or 4.
 * @post On \c false, \c simplex and \c direction describe the next search step.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto do_simplex(
  gjk_simplex3<Real>& simplex, nexenne::math::vector<Real, 3>& direction
) noexcept -> bool {
  using vector_type = nexenne::math::vector<Real, 3>;

  // Squared-length floor below which the triple-cross search direction has
  // collapsed: the origin sits on the current edge, so any orthogonal will do.
  auto const collapse_sq{static_cast<Real>(1e-12)};

  auto const a{simplex.points[0].difference};
  auto const ao{-a};  // direction from the newest vertex a toward the origin.

  // Reduce to the edge [a, p]: aim perpendicular to the edge toward the origin,
  // or drop back to the single vertex a when the origin is behind it. Assumes
  // points[1] already holds p and count is 2.
  auto const reduce_to_edge{[&](vector_type const& p) noexcept {
    auto const ap{p - a};
    if (same_direction(ap, ao)) {
      // Perpendicular to ap, in the plane of ap and ao, pointing at the origin.
      direction = nexenne::math::cross(nexenne::math::cross(ap, ao), ap);
      if (nexenne::math::length_squared(direction) < collapse_sq) {
        direction = any_orthogonal(ap);
      }
    } else {
      simplex.count = 1;  // origin is behind a: keep just [a].
      direction = ao;
    }
  }};

  if (simplex.count == 2) {
    reduce_to_edge(simplex.points[1].difference);
    return false;
  }

  if (simplex.count == 3) {
    auto const b{simplex.points[1].difference};
    auto const c{simplex.points[2].difference};
    auto const ab{b - a};
    auto const ac{c - a};
    auto const abc{nexenne::math::cross(ab, ac)};  // triangle face normal.

    if (same_direction(nexenne::math::cross(abc, ac), ao)) {
      // Origin is outside edge ac.
      if (same_direction(ac, ao)) {
        simplex.points[1] = simplex.points[2];  // keep [a, c].
        simplex.count = 2;
        reduce_to_edge(simplex.points[1].difference);
      } else {
        simplex.count = 2;  // keep [a, b].
        reduce_to_edge(simplex.points[1].difference);
      }
    } else if (same_direction(nexenne::math::cross(ab, abc), ao)) {
      // Origin is outside edge ab.
      simplex.count = 2;  // keep [a, b].
      reduce_to_edge(simplex.points[1].difference);
    } else {
      // Origin is over the triangle face: search toward whichever side it lies.
      if (same_direction(abc, ao)) {
        direction = abc;
      } else {
        // Flip the winding so a new tetrahedron grows on the origin's side.
        auto const tmp{simplex.points[1]};
        simplex.points[1] = simplex.points[2];
        simplex.points[2] = tmp;
        direction = -abc;
      }
    }
    return false;
  }

  // count == 4: a tetrahedron. Test the three faces adjacent to the newest
  // vertex a; if the origin is outside one, drop the opposite vertex and recurse
  // on that face. If it is inside all three, the origin is enclosed.
  auto const b{simplex.points[1].difference};
  auto const c{simplex.points[2].difference};
  auto const d{simplex.points[3].difference};
  auto const ab{b - a};
  auto const ac{c - a};
  auto const ad{d - a};
  auto const abc{nexenne::math::cross(ab, ac)};
  auto const acd{nexenne::math::cross(ac, ad)};
  auto const adb{nexenne::math::cross(ad, ab)};

  if (same_direction(abc, ao)) {
    simplex.count = 3;  // drop d, recurse on face [a, b, c].
    return do_simplex(simplex, direction);
  }
  if (same_direction(acd, ao)) {
    simplex.points[1] = simplex.points[2];  // drop b, face [a, c, d].
    simplex.points[2] = simplex.points[3];
    simplex.count = 3;
    return do_simplex(simplex, direction);
  }
  if (same_direction(adb, ao)) {
    simplex.points[2] = simplex.points[1];  // drop c, face [a, d, b].
    simplex.points[1] = simplex.points[3];
    simplex.count = 3;
    return do_simplex(simplex, direction);
  }
  return true;  // origin is inside all three faces: enclosed.
}

}  // namespace detail

/**
 * @brief Runs GJK on two convex shapes and reports whether they overlap.
 *
 * Iterates the support-and-reduce loop: it asks each shape for a support point,
 * folds the Minkowski-difference vertex into the simplex, and lets
 * \c detail::do_simplex walk toward the origin, until the origin is enclosed
 * (overlap), a support point fails to pass the origin (separated), or the
 * iteration cap is hit. The terminal simplex is returned for EPA to consume.
 *
 * @tparam Real Floating-point component type, deduced from \p initial_direction.
 * @tparam ShapeA First shape type; must satisfy \c convex_shape.
 * @tparam ShapeB Second shape type; must satisfy \c convex_shape.
 * @param a First convex shape.
 * @param b Second convex shape.
 * @param initial_direction Seed search direction; any non-zero vector works (the
 *        center-to-center vector is a good choice). A zero vector falls back to
 *        the +x axis.
 * @param max_iterations Hard cap guarding against non-termination on degenerate
 *        input. 32 is comfortable for typical shapes.
 *
 * @return Result whose \c overlap flag and terminal \c simplex describe the
 *         outcome; \c iterations reports how many steps were used.
 *
 * @pre \p a and \p b are convex.
 * @post When \c overlap is \c true the \c simplex is a tetrahedron enclosing the
 *       origin and \c iterations does not exceed \p max_iterations.
 * @complexity \c O(max_iterations) support queries; each is the shape's support
 *             cost (\c O(N) for a \c convex_hull3 of N vertices).
 */
template <std::floating_point Real, convex_shape<Real> ShapeA, convex_shape<Real> ShapeB>
[[nodiscard]] constexpr auto gjk(
  ShapeA const& a,
  ShapeB const& b,
  nexenne::math::vector<Real, 3> const& initial_direction,
  std::size_t const max_iterations = 32
) noexcept -> gjk_result3<Real> {
  using vector_type = nexenne::math::vector<Real, 3>;
  using point_type = gjk_minkowski_point3<Real>;

  // Squared-length floor below which the search direction has collapsed to zero,
  // taken to mean the origin lies on the simplex boundary (a touching contact).
  auto const collapse_sq{static_cast<Real>(1e-20)};

  auto result{gjk_result3<Real>{}};

  // A Minkowski-difference support point: furthest of A along d, minus furthest
  // of B along -d, kept with both world-space support points for EPA.
  auto const minkowski_support{[&](vector_type const& d) noexcept -> point_type {
    auto const pa{support(a, d)};
    auto const pb{support(b, -d)};
    return point_type{pa - pb, pa, pb};
  }};

  auto direction{initial_direction};
  if (nexenne::math::length_squared(direction) < collapse_sq) {
    direction = vector_type{Real{1}, Real{0}, Real{0}};
  }

  auto const first{minkowski_support(direction)};
  result.simplex.points[0] = first;
  result.simplex.count = 1;
  direction = -first.difference;  // head from the first vertex toward the origin.

  for (auto iter{std::size_t{0}}; iter < max_iterations; ++iter) {
    result.iterations = iter + 1;

    auto const next{minkowski_support(direction)};
    // If the furthest point in the search direction did not pass the origin, the
    // Minkowski difference cannot contain it: the shapes are separated.
    if (nexenne::math::dot(next.difference, direction) < Real{0}) {
      result.overlap = false;
      return result;
    }

    // Insert the new vertex at the front (newest-first convention).
    for (auto i{result.simplex.count}; i > 0; --i) {
      result.simplex.points[i] = result.simplex.points[i - 1];
    }
    result.simplex.points[0] = next;
    result.simplex.count += 1;

    if (detail::do_simplex(result.simplex, direction)) {
      result.overlap = true;
      return result;
    }
    if (nexenne::math::length_squared(direction) < collapse_sq) {
      result.overlap = true;  // direction collapsed: origin on the boundary, touching.
      return result;
    }
  }

  result.overlap = false;  // no convergence within the cap: conservatively separated.
  return result;
}

}  // namespace nexenne::geometry
