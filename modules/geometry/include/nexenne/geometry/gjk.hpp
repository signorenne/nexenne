#pragma once

/**
 * @file
 * @brief Gilbert-Johnson-Keerthi (GJK) overlap and distance via signed volumes.
 *
 * GJK answers "do these two convex shapes overlap, and if not how far apart are
 * they?" using only one operation from each shape, the support function (the
 * surface point furthest along a direction). It builds a simplex inside the
 * Minkowski difference A (-) B and walks it toward the origin: the shapes overlap
 * exactly when the simplex can enclose the origin, and when they do not, the
 * closest point of the simplex to the origin gives the separation distance and,
 * blended back through the per-shape support points, the closest points on each
 * shape.
 *
 * The simplex is reduced each step by the \e signed-volumes distance subalgorithm
 * of Montanari, Petrinic and Barbieri (2017): given the current simplex it finds
 * the point of its convex hull closest to the origin and the smallest sub-simplex
 * (a vertex, edge, triangle or the whole tetrahedron) that carries that point,
 * by comparing the signs of the barycentric cofactors rather than forming and
 * clamping a projection. That keeps GJK accurate to machine precision and shortens
 * the search compared with the original Johnson subalgorithm. The realization here
 * is the closest-point-on-simplex recursion (S1D / S2D / S3D), which is the same
 * decision Christer Ericson's closest-feature routines make, organized as the
 * signed-volumes reduction.
 *
 * Any type with an ADL-found \c support(shape, direction) overload in
 * \c nexenne::geometry is usable: \c convex_hull3 provides one and the analytic
 * primitives supply theirs in support.hpp. The \c convex_shape concept (in
 * concepts.hpp) spells out the requirement. On overlap the terminal simplex is a
 * tetrahedron enclosing the origin, which EPA picks up for the penetration depth.
 *
 * Convention: a \c gjk_simplex3 stores its vertices newest-first, so
 * \c points[0] is the most recently added one and \c count is the simplex
 * dimension. Everything is \c constexpr and \c noexcept; the algorithm allocates
 * nothing (the simplex is a fixed four-element array).
 *
 * References: M. Montanari, N. Petrinic, E. Barbieri, "Improving the GJK
 * Algorithm for Faster and More Reliable Distance Queries Between Convex
 * Objects", ACM Transactions on Graphics 36(3), 2017 (the signed-volumes
 * subalgorithm); C. Ericson, "Real-Time Collision Detection", section 5.1 (the
 * closest-point-on-simplex routines this reduction is built from).
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
 * lets GJK reconstruct world-space closest points (and EPA contact points) by
 * blending them with the simplex barycentric weights.
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
 * @brief Outcome of a GJK run: overlap flag, distance and closest points, the
 *        terminal simplex, and the iteration count.
 *
 * When \c overlap is \c false the shapes are apart: \c distance is the separation
 * and \c closest_a / \c closest_b are the nearest points on A and B. When
 * \c overlap is \c true the shapes intersect: \c distance is zero, the closest
 * points are unused, and \c simplex is a tetrahedron enclosing the origin for EPA.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct gjk_result3 {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  bool overlap{false};           ///< True when the two shapes overlap.
  Real distance{};               ///< Separation distance when apart; 0 on overlap.
  point_type closest_a{};        ///< Closest point on A (world space) when apart.
  point_type closest_b{};        ///< Closest point on B (world space) when apart.
  gjk_simplex3<Real> simplex{};  ///< Terminal simplex; a tetrahedron on overlap.
  std::size_t iterations{0};     ///< Iterations consumed before terminating.
};

namespace detail {

/**
 * @brief The signed-volumes sign test: whether \p a and \p b share a sign.
 *
 * The \c CompareSigns predicate of the signed-volumes subalgorithm. A simplex
 * vertex stays in the support set when its barycentric cofactor has the same sign
 * as the total, which means the origin's projection lies on that vertex's side.
 *
 * @tparam Real Component type.
 * @param a First value (a cofactor).
 * @param b Second value (the total).
 *
 * @return \c true when \p a and \p b are both positive or both negative.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto compare_signs(Real const a, Real const b) noexcept -> bool {
  return (a > Real{0} && b > Real{0}) || (a < Real{0} && b < Real{0});
}

/**
 * @brief Closest point of the origin on a simplex, with barycentric weights.
 *
 * The result of one signed-volumes reduction: the point of the simplex's convex
 * hull nearest the origin, the weights that blend the kept vertices into it, and
 * which input vertices were kept (the minimal support set). \c contains_origin is
 * set only by the tetrahedron case when the origin is strictly inside.
 *
 * @tparam Real Component type.
 */
template <std::floating_point Real>
struct simplex_reduction3 {
  using point_type = nexenne::math::vector<Real, 3>;

  std::array<std::size_t, 4> kept{};  ///< Indices into the input simplex that survive.
  std::array<Real, 4> weights{};      ///< Barycentric weight per kept vertex, [0, count).
  std::size_t count{0};               ///< Number of kept vertices, 1 to 4.
  point_type closest{};               ///< Closest point of the simplex to the origin.
  bool contains_origin{false};        ///< True when the origin is inside a tetrahedron.
};

/**
 * @brief Closest point of the origin on segment (a, b), with edge weights.
 *
 * Projects the origin onto the segment and clamps to the endpoints, the S1D case
 * of the signed-volumes reduction. The third return slot reports whether both
 * endpoints are kept (the projection lands inside) or just one.
 *
 * @tparam Real Component type.
 * @param a First endpoint.
 * @param b Second endpoint.
 * @param wa Receives the weight of \p a.
 * @param wb Receives the weight of \p b.
 *
 * @return The closest point; \p wa and \p wb are its barycentric weights and a
 *         weight of exactly zero marks an endpoint dropped from the support set.
 *
 * @pre None. A zero-length segment collapses to \p a.
 * @post \c wa + wb equals 1 and both are non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_on_segment(
  nexenne::math::vector<Real, 3> const& a,
  nexenne::math::vector<Real, 3> const& b,
  Real& wa,
  Real& wb
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const ab{b - a};
  auto const denom{nexenne::math::length_squared(ab)};
  if (denom <= static_cast<Real>(1e-20)) {
    wa = Real{1};
    wb = Real{0};
    return a;
  }
  // Parameter of the origin's projection onto the line through a and b.
  auto const t{nexenne::math::dot(-a, ab) / denom};
  if (t <= Real{0}) {
    wa = Real{1};
    wb = Real{0};
    return a;
  }
  if (t >= Real{1}) {
    wa = Real{0};
    wb = Real{1};
    return b;
  }
  wa = Real{1} - t;
  wb = t;
  return a + ab * t;
}

/**
 * @brief Closest point of the origin on triangle (a, b, c), with vertex weights.
 *
 * The S2D case: it walks the triangle's vertex, edge and face Voronoi regions in
 * the order of Ericson's closest-point-on-triangle test (with the query point at
 * the origin), which is the signed-volumes decision over the triangle's three
 * sub-areas. A weight of exactly zero marks a vertex dropped from the support set.
 *
 * @tparam Real Component type.
 * @param a First vertex.
 * @param b Second vertex.
 * @param c Third vertex.
 * @param wa Receives the weight of \p a.
 * @param wb Receives the weight of \p b.
 * @param wc Receives the weight of \p c.
 *
 * @return The closest point; the three weights are its barycentric coordinates.
 *
 * @pre None. A degenerate triangle reduces through its edges.
 * @post \c wa + wb + wc equals 1 and each is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_on_triangle(
  nexenne::math::vector<Real, 3> const& a,
  nexenne::math::vector<Real, 3> const& b,
  nexenne::math::vector<Real, 3> const& c,
  Real& wa,
  Real& wb,
  Real& wc
) noexcept -> nexenne::math::vector<Real, 3> {
  using nexenne::math::dot;
  auto const ab{b - a};
  auto const ac{c - a};

  // Vertex region of a: origin behind both edges leaving a.
  auto const d1{dot(ab, -a)};
  auto const d2{dot(ac, -a)};
  if (d1 <= Real{0} && d2 <= Real{0}) {
    wa = Real{1};
    wb = Real{0};
    wc = Real{0};
    return a;
  }
  // Vertex region of b.
  auto const d3{dot(ab, -b)};
  auto const d4{dot(ac, -b)};
  if (d3 >= Real{0} && d4 <= d3) {
    wa = Real{0};
    wb = Real{1};
    wc = Real{0};
    return b;
  }
  // Edge region ab.
  auto const vc{d1 * d4 - d3 * d2};
  if (vc <= Real{0} && d1 >= Real{0} && d3 <= Real{0}) {
    auto const t{d1 / (d1 - d3)};
    wa = Real{1} - t;
    wb = t;
    wc = Real{0};
    return a + ab * t;
  }
  // Vertex region of c.
  auto const d5{dot(ab, -c)};
  auto const d6{dot(ac, -c)};
  if (d6 >= Real{0} && d5 <= d6) {
    wa = Real{0};
    wb = Real{0};
    wc = Real{1};
    return c;
  }
  // Edge region ac.
  auto const vb{d5 * d2 - d1 * d6};
  if (vb <= Real{0} && d2 >= Real{0} && d6 <= Real{0}) {
    auto const t{d2 / (d2 - d6)};
    wa = Real{1} - t;
    wb = Real{0};
    wc = t;
    return a + ac * t;
  }
  // Edge region bc.
  auto const va{d3 * d6 - d5 * d4};
  if (va <= Real{0} && (d4 - d3) >= Real{0} && (d5 - d6) >= Real{0}) {
    auto const t{(d4 - d3) / ((d4 - d3) + (d5 - d6))};
    wa = Real{0};
    wb = Real{1} - t;
    wc = t;
    return b + (c - b) * t;
  }
  // Face interior.
  auto const denom{Real{1} / (va + vb + vc)};
  auto const v{vb * denom};
  auto const w{vc * denom};
  wa = Real{1} - v - w;
  wb = v;
  wc = w;
  return a + ab * v + ac * w;
}

/**
 * @brief Reduces the simplex toward the origin via the signed-volumes method.
 *
 * Dispatches on the simplex size to the S1D / S2D / S3D cases: it returns the
 * closest point of the simplex to the origin, the barycentric weights of the kept
 * vertices, and which input vertices form that minimal support set. For a
 * tetrahedron it also reports when the origin is enclosed (the overlap case).
 *
 * @tparam Real Component type.
 * @param simplex Working simplex (1 to 4 vertices), read for its difference
 *        points.
 *
 * @return The reduction: kept indices, weights, closest point, and whether the
 *         origin is inside.
 *
 * @pre \c simplex.count is 1, 2, 3, or 4.
 * @post \c result.count is between 1 and \c simplex.count and the weights are
 *       non-negative and sum to 1.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto signed_volumes(gjk_simplex3<Real> const& simplex
) noexcept -> simplex_reduction3<Real> {
  using point_type = nexenne::math::vector<Real, 3>;
  auto result{simplex_reduction3<Real>{}};

  auto const p0{simplex.points[0].difference};
  if (simplex.count == 1) {
    result.kept = {0, 0, 0, 0};
    result.weights[0] = Real{1};
    result.count = 1;
    result.closest = p0;
    return result;
  }

  if (simplex.count == 2) {
    auto wa{Real{0}};
    auto wb{Real{0}};
    auto const q{closest_on_segment(p0, simplex.points[1].difference, wa, wb)};
    result.closest = q;
    auto const w{std::array<Real, 2>{wa, wb}};
    for (auto i{std::size_t{0}}; i < 2; ++i) {
      if (w[i] > Real{0}) {
        result.kept[result.count] = i;
        result.weights[result.count] = w[i];
        ++result.count;
      }
    }
    return result;
  }

  if (simplex.count == 3) {
    auto wa{Real{0}};
    auto wb{Real{0}};
    auto wc{Real{0}};
    auto const q{closest_on_triangle(
      p0, simplex.points[1].difference, simplex.points[2].difference, wa, wb, wc
    )};
    result.closest = q;
    auto const w{std::array<Real, 3>{wa, wb, wc}};
    for (auto i{std::size_t{0}}; i < 3; ++i) {
      if (w[i] > Real{0}) {
        result.kept[result.count] = i;
        result.weights[result.count] = w[i];
        ++result.count;
      }
    }
    return result;
  }

  // count == 4: the tetrahedron case (S3D). The origin's barycentric weights are
  // the four signed sub-volumes (cofactors) over their total detM, by Cramer's
  // rule. The origin is enclosed exactly when every cofactor shares detM's sign;
  // otherwise it lies outside each face whose cofactor sign disagrees, so the
  // closest point is found by recursing S2D on those faces and keeping the
  // nearest. Comparing cofactor signs (rather than face-normal cross products)
  // is what keeps the test robust when the tetrahedron is nearly flat: a
  // degenerate simplex has detM ~ 0, no sign agrees, and it reduces to a face
  // instead of being mistaken for an enclosure.
  auto const a{p0};
  auto const b{simplex.points[1].difference};
  auto const c{simplex.points[2].difference};
  auto const d{simplex.points[3].difference};
  auto const corner{std::array<point_type, 4>{a, b, c, d}};

  auto const signed_volume{
    [](
      point_type const& p, point_type const& q, point_type const& r, point_type const& s
    ) noexcept { return nexenne::math::dot(q - p, nexenne::math::cross(r - p, s - p)); }
  };
  auto const o{point_type{}};
  auto const det_m{signed_volume(a, b, c, d)};
  auto const cofactor{std::array<Real, 4>{
    signed_volume(o, b, c, d),  // replace a with the origin.
    signed_volume(a, o, c, d),  // replace b.
    signed_volume(a, b, o, d),  // replace c.
    signed_volume(a, b, c, o),  // replace d.
  }};

  auto const enclosed{
    nexenne::math::abs(det_m) > static_cast<Real>(1e-20) && compare_signs(cofactor[0], det_m)
    && compare_signs(cofactor[1], det_m) && compare_signs(cofactor[2], det_m)
    && compare_signs(cofactor[3], det_m)
  };
  if (enclosed) {
    auto const inv{Real{1} / det_m};
    result.kept = {0, 1, 2, 3};
    result.count = 4;
    result.closest = o;
    result.contains_origin = true;
    for (auto i{std::size_t{0}}; i < 4; ++i) {
      result.weights[i] = cofactor[i] * inv;
    }
    return result;
  }

  // Recurse the faces the origin is outside of, keeping the nearest. Face
  // opposite vertex \c opp is the other three vertices.
  auto best_dist_sq{
    nexenne::math::length_squared(a) + nexenne::math::length_squared(b)
    + nexenne::math::length_squared(c) + nexenne::math::length_squared(d) + Real{1}
  };
  for (auto opp{std::size_t{0}}; opp < 4; ++opp) {
    if (compare_signs(cofactor[opp], det_m)) {
      continue;  // origin is on the inner side of this face: it cannot hold the closest point.
    }
    auto idx{std::array<std::size_t, 3>{}};
    auto n{std::size_t{0}};
    for (auto v{std::size_t{0}}; v < 4; ++v) {
      if (v != opp) {
        idx[n++] = v;
      }
    }
    auto wa{Real{0}};
    auto wb{Real{0}};
    auto wc{Real{0}};
    auto const q{closest_on_triangle(corner[idx[0]], corner[idx[1]], corner[idx[2]], wa, wb, wc)};
    auto const dist_sq{nexenne::math::length_squared(q)};
    if (dist_sq < best_dist_sq) {
      best_dist_sq = dist_sq;
      result.closest = q;
      result.count = 0;
      auto const w{std::array<Real, 3>{wa, wb, wc}};
      for (auto i{std::size_t{0}}; i < 3; ++i) {
        if (w[i] > Real{0}) {
          result.kept[result.count] = idx[i];
          result.weights[result.count] = w[i];
          ++result.count;
        }
      }
    }
  }
  return result;
}

}  // namespace detail

/**
 * @brief Runs GJK on two convex shapes, reporting overlap or the distance.
 *
 * Iterates the support-and-reduce loop: it asks the shapes for a support point
 * along the current search direction, folds the Minkowski-difference vertex into
 * the simplex, and lets \c detail::signed_volumes find the closest point of the
 * simplex to the origin and shrink the simplex to its minimal support set. It
 * stops when the origin is enclosed (overlap), when a support point cannot move
 * the closest point any nearer the origin (separated, distance found), or when the
 * iteration cap is hit. On overlap the terminal simplex is a tetrahedron for EPA;
 * when apart the result carries the distance and the closest point on each shape.
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
 * @return Result whose \c overlap flag, \c distance, \c closest_a / \c closest_b,
 *         and terminal \c simplex describe the outcome; \c iterations reports how
 *         many steps were used.
 *
 * @pre \p a and \p b are convex.
 * @post When \c overlap is \c true the \c simplex encloses the origin; when it is
 *       \c false \c distance is non-negative and the closest points lie on the
 *       respective shapes. \c iterations does not exceed \p max_iterations.
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

  // Below this squared distance the closest point is taken to be the origin: the
  // simplex touches it, so the shapes overlap (a touching contact).
  auto const touch_sq{static_cast<Real>(1e-20)};
  // Relative no-progress threshold for the separation test.
  auto const rel_tol{static_cast<Real>(1e-10)};

  auto result{gjk_result3<Real>{}};

  // A Minkowski-difference support point: furthest of A along d, minus furthest
  // of B along -d, kept with both world-space support points.
  auto const minkowski_support{[&](vector_type const& d) noexcept -> point_type {
    auto const pa{support(a, d)};
    auto const pb{support(b, -d)};
    return point_type{pa - pb, pa, pb};
  }};

  auto direction{initial_direction};
  if (nexenne::math::length_squared(direction) < touch_sq) {
    direction = vector_type{Real{1}, Real{0}, Real{0}};
  }

  result.simplex.points[0] = minkowski_support(direction);
  result.simplex.count = 1;
  auto closest{result.simplex.points[0].difference};
  auto weights{std::array<Real, 4>{Real{1}, Real{0}, Real{0}, Real{0}}};
  // Largest squared vertex magnitude seen, the scale the relative tests use.
  auto scale_sq{nexenne::math::length_squared(closest)};

  // Overlap is decided by whether a separating axis is ever found: a single
  // support that fails to reach the origin proves the shapes are apart. The loop
  // runs until the closest point stops moving toward the origin (distance
  // converged) or the origin is enclosed (the fast tetrahedron path).
  auto separated{false};
  for (auto iter{std::size_t{0}}; iter < max_iterations; ++iter) {
    result.iterations = iter + 1;

    auto const dist_sq{nexenne::math::length_squared(closest)};

    // Search toward the origin from the current closest point.
    direction = -closest;
    auto const w{minkowski_support(direction)};
    scale_sq = nexenne::math::max(scale_sq, nexenne::math::length_squared(w.difference));

    // Separating-axis test: if the support furthest toward the origin still does
    // not pass it, that direction separates the shapes, so they cannot overlap.
    if (nexenne::math::dot(w.difference, direction) < Real{0}) {
      separated = true;
    }

    // Stop when the support repeats a simplex vertex (no new information, so the
    // origin lies on the simplex: an overlap), when the closest point has reached
    // the origin, or when the support can no longer push it nearer (the distance
    // has converged): the last is |v| - dot(v, w)/|v| <= rel_tol * |v|.
    // Classification uses the separating-axis flag, not these floors, so an early
    // stop never misjudges overlap.
    auto duplicate{false};
    for (auto i{std::size_t{0}}; i < result.simplex.count; ++i) {
      if (nexenne::math::length_squared(w.difference - result.simplex.points[i].difference)
          <= rel_tol * scale_sq) {
        duplicate = true;
      }
    }
    if (dist_sq <= touch_sq || duplicate
        || dist_sq - nexenne::math::dot(closest, w.difference) <= rel_tol * dist_sq) {
      break;
    }

    // Add the new vertex at the front (newest-first convention).
    for (auto i{result.simplex.count}; i > 0; --i) {
      result.simplex.points[i] = result.simplex.points[i - 1];
    }
    result.simplex.points[0] = w;
    result.simplex.count += 1;

    // Reduce the simplex to the support set of its closest point.
    auto const reduction{detail::signed_volumes(result.simplex)};
    if (reduction.contains_origin) {
      result.overlap = true;  // origin enclosed: a tetrahedron is ready for EPA.
      result.distance = Real{0};
      result.simplex.count = reduction.count;
      return result;
    }

    auto reduced{gjk_simplex3<Real>{}};
    reduced.count = reduction.count;
    for (auto i{std::size_t{0}}; i < reduction.count; ++i) {
      reduced.points[i] = result.simplex.points[reduction.kept[i]];
      weights[i] = reduction.weights[i];
    }
    result.simplex = reduced;
    closest = reduction.closest;
  }

  if (!separated) {
    // No separating axis was ever found: the shapes intersect. The terminal
    // simplex (the origin lies on it) seeds EPA, which grows it to a tetrahedron.
    result.overlap = true;
    result.distance = Real{0};
    return result;
  }

  // Apart: blend the per-shape support points by the simplex weights to recover
  // the closest point on each shape, and report the distance.
  result.overlap = false;
  result.distance = nexenne::math::length(closest);
  auto point_a{vector_type{}};
  auto point_b{vector_type{}};
  for (auto i{std::size_t{0}}; i < result.simplex.count; ++i) {
    point_a = point_a + result.simplex.points[i].support_a * weights[i];
    point_b = point_b + result.simplex.points[i].support_b * weights[i];
  }
  result.closest_a = point_a;
  result.closest_b = point_b;
  return result;
}

}  // namespace nexenne::geometry
