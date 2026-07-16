#pragma once

/**
 * @file
 * @brief Expanding Polytope Algorithm (EPA): penetration depth and normal from
 *        an overlapping GJK result.
 *
 * Run order: \c gjk first; if it reports \c overlap, hand its terminal simplex
 * (a tetrahedron enclosing the origin) to \c epa. EPA expands that tetrahedron
 * outward through the Minkowski difference, always toward the face nearest the
 * origin, until the nearest face stops moving. That face's normal and its
 * distance to the origin are the penetration normal and depth, the smallest
 * translation that separates the two shapes.
 *
 * Output is the contact normal, the penetration depth, and per-shape contact
 * points reconstructed from the closest face via barycentric weights on the
 * original support pairs. The normal is the minimum-translation direction:
 * translating B by \c penetration_depth * normal (or A by its negative) just
 * separates the shapes, so it points out of A toward B.
 *
 * Unlike the rest of the module, EPA allocates: the polytope (vertices and
 * triangular faces) grows during expansion, so it is held in \c std::vector. It
 * is \c noexcept, so an allocation failure terminates, matching the module's
 * terminate-on-OOM policy. \c Real is deduced from the input simplex.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <utility>
#include <vector>

#include <nexenne/geometry/concepts.hpp>
#include <nexenne/geometry/gjk.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Output of EPA: penetration normal, depth, and per-shape contact points.
 *
 * A documented aggregate, like \c gjk_result3: the algorithm's result bundle,
 * the role the standard library gives its \c *_result types.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct epa_result3 {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  bool converged{false};         ///< True when expansion converged on a closest face.
  point_type normal{};           ///< Unit MTV direction, out of A toward B.
  Real penetration_depth{};      ///< Depth to separate the shapes along normal.
  point_type contact_point_a{};  ///< Best-guess contact point on A (world space).
  point_type contact_point_b{};  ///< Best-guess contact point on B (world space).
};

namespace detail {

/**
 * @brief A triangular face of the EPA polytope: three vertex indices, an outward
 *        unit normal, and the distance from the origin to the face plane.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct epa_face {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  std::array<std::size_t, 3> indices{};  ///< Indices into the shared vertex list.
  point_type normal{};                   ///< Outward unit normal (points away from origin).
  Real distance{};                       ///< Distance from the origin to the face plane.
};

/**
 * @brief Builds a face from three polytope vertices, with its normal oriented
 *        outward (away from \p interior) and its origin distance.
 *
 * The normal is \c cross(b - a, c - a) normalized, then flipped if it points
 * toward \p interior (a point known to be inside the polytope) so it always
 * points outward. Orienting against an interior reference rather than the origin
 * is what keeps EPA correct when the origin lies exactly on a seed face (a
 * shallow or perfectly symmetric contact): the origin then cannot say which side
 * is out, but a strictly-interior point always can.
 *
 * When the normal is flipped, two indices are swapped as well, so the stored
 * winding stays consistent with the outward normal across every face. The
 * expansion builds its horizon by cancelling each directed edge (i, j) against
 * its reverse (j, i), which is only correct when all faces wind the same way;
 * flipping a normal without reordering would silently corrupt the polytope and
 * stop EPA from ever converging. A degenerate (zero-area) triangle falls back to
 * the x axis, which the expansion then discards on the next step.
 *
 * @tparam Real Component type.
 * @param vertices Shared polytope vertex list.
 * @param ia First vertex index.
 * @param ib Second vertex index.
 * @param ic Third vertex index.
 * @param interior A point strictly inside the polytope (the seed centroid).
 *
 * @return The oriented face, wound consistently with its outward normal.
 *
 * @pre The three indices are valid in \p vertices and \p interior is inside the
 *      polytope.
 * @post \c result.distance is non-negative (the origin is inside the polytope)
 *       and \c result.normal has unit length and points outward.
 */
template <std::floating_point Real>
[[nodiscard]] auto build_face(
  std::vector<gjk_minkowski_point3<Real>> const& vertices,
  std::size_t const ia,
  std::size_t ib,
  std::size_t ic,
  nexenne::math::vector<Real, 3> const& interior
) noexcept -> epa_face<Real> {
  using point_type = nexenne::math::vector<Real, 3>;
  auto const& a{vertices[ia].difference};
  auto const& b{vertices[ib].difference};
  auto const& c{vertices[ic].difference};

  auto normal{nexenne::math::cross(b - a, c - a)};
  auto const length_sq{nexenne::math::length_squared(normal)};
  if (length_sq > static_cast<Real>(1e-20)) {
    normal = normal * (Real{1} / nexenne::math::sqrt(length_sq));
  } else {
    normal = point_type{Real{1}, Real{0}, Real{0}};  // degenerate; discarded next step.
  }

  if (nexenne::math::dot(normal, a - interior) < Real{0}) {
    normal = -normal;   // orient outward: away from the polytope interior, and
    std::swap(ib, ic);  // keep the winding consistent so the horizon cancels.
  }
  // Origin-to-plane distance along the outward normal, non-negative because the
  // origin is inside the polytope (clamped against a tiny negative from rounding
  // when the origin sits on the plane).
  auto const distance{nexenne::math::max(Real{0}, nexenne::math::dot(normal, a))};
  return epa_face<Real>{{ia, ib, ic}, normal, distance};
}

/**
 * @brief Barycentric coordinates of \p projected within triangle (v0, v1, v2).
 *
 * Solves the 2x2 normal-equations system for the triangle's edge basis
 * (Cramer's rule on the Gram matrix), the standard way to express a point in a
 * triangle's plane as a weighted blend of its vertices. EPA uses it to carry the
 * origin's projection on the closest face back onto the per-shape support points,
 * recovering world-space contact points. A degenerate (zero-area) triangle
 * returns (1, 0, 0).
 *
 * See Christer Ericson, "Real-Time Collision Detection", section 3.4, for the
 * Gram-matrix derivation of barycentric coordinates.
 *
 * @tparam Real Component type.
 * @param v0 First triangle vertex.
 * @param v1 Second triangle vertex.
 * @param v2 Third triangle vertex.
 * @param projected Point in the triangle plane to express barycentrically.
 *
 * @return Weights (alpha, beta, gamma) with \c alpha + beta + gamma == 1.
 *
 * @pre None. A degenerate triangle is handled.
 * @post The components sum to 1 up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto barycentric(
  nexenne::math::vector<Real, 3> const& v0,
  nexenne::math::vector<Real, 3> const& v1,
  nexenne::math::vector<Real, 3> const& v2,
  nexenne::math::vector<Real, 3> const& projected
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const e1{v1 - v0};
  auto const e2{v2 - v0};
  auto const rel{projected - v0};
  auto const d11{nexenne::math::dot(e1, e1)};
  auto const d12{nexenne::math::dot(e1, e2)};
  auto const d22{nexenne::math::dot(e2, e2)};
  auto const dr1{nexenne::math::dot(rel, e1)};
  auto const dr2{nexenne::math::dot(rel, e2)};
  auto const det{d11 * d22 - d12 * d12};  // Gram determinant (twice-area squared).
  if (nexenne::math::abs(det) < static_cast<Real>(1e-20)) {
    return nexenne::math::vector<Real, 3>{Real{1}, Real{0}, Real{0}};
  }
  auto const inv{Real{1} / det};
  auto const beta{(d22 * dr1 - d12 * dr2) * inv};
  auto const gamma{(d11 * dr2 - d12 * dr1) * inv};
  auto const alpha{Real{1} - beta - gamma};
  return nexenne::math::vector<Real, 3>{alpha, beta, gamma};
}

/**
 * @brief Clamps barycentric weights to the triangle and renormalizes them.
 *
 * The origin's projection onto the closest face plane can land outside the face
 * triangle when the true closest boundary point lies on a polytope edge, so the
 * raw \c barycentric weights can go negative and the blended contact points would
 * then extrapolate off the shapes' faces. Clamping each weight to non-negative and
 * renormalizing pins the blend back onto the triangle, matching what Bullet and
 * Box2D do at this step. A fully clamped-out weight set falls back to the first
 * vertex.
 *
 * @tparam Real Component type.
 * @param weights Raw barycentric weights (alpha, beta, gamma).
 *
 * @return Non-negative weights summing to 1.
 *
 * @pre None.
 * @post Each component is non-negative and the three sum to 1.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
clamp_barycentric(nexenne::math::vector<Real, 3> const& weights) noexcept
  -> nexenne::math::vector<Real, 3> {
  using point_type = nexenne::math::vector<Real, 3>;
  auto const a{nexenne::math::max(Real{0}, weights.x())};
  auto const b{nexenne::math::max(Real{0}, weights.y())};
  auto const c{nexenne::math::max(Real{0}, weights.z())};
  auto const sum{a + b + c};
  if (sum <= static_cast<Real>(1e-20)) {
    return point_type{Real{1}, Real{0}, Real{0}};
  }
  auto const inv{Real{1} / sum};
  return point_type{a * inv, b * inv, c * inv};
}

/**
 * @brief Reconstructs the per-shape contact points from the closest EPA face.
 *
 * Expresses the origin's projection onto the face plane in the face's barycentric
 * coordinates (clamped onto the triangle) and blends the three stored per-shape
 * support points by those weights, recovering the world-space contact point on A
 * and on B. Shared by the converged and the cap-exhaustion branches so a
 * non-converged result still carries the best face's actual contact points rather
 * than the value-initialized origin.
 *
 * @tparam Real Component type.
 * @param vertices Shared polytope vertex list (with per-shape support points).
 * @param face Closest face to reconstruct from.
 * @param closest_distance Origin-to-face-plane distance along the face normal.
 *
 * @return The pair (contact on A, contact on B) in world space.
 *
 * @pre The face indices are valid in \p vertices.
 * @post Both points lie on the blend of the face's stored support points.
 */
template <std::floating_point Real>
[[nodiscard]] auto face_contact_points(
  std::vector<gjk_minkowski_point3<Real>> const& vertices,
  epa_face<Real> const& face,
  Real const closest_distance
) noexcept -> std::pair<nexenne::math::vector<Real, 3>, nexenne::math::vector<Real, 3>> {
  using point_type = nexenne::math::vector<Real, 3>;
  auto const v0{vertices[face.indices[0]].difference};
  auto const v1{vertices[face.indices[1]].difference};
  auto const v2{vertices[face.indices[2]].difference};
  auto const projected_origin{face.normal * closest_distance};
  auto const weights{clamp_barycentric<Real>(barycentric<Real>(v0, v1, v2, projected_origin))};
  auto const blend{
    [&](point_type const& p0, point_type const& p1, point_type const& p2) noexcept -> point_type {
      return p0 * weights.x() + p1 * weights.y() + p2 * weights.z();
    }
  };
  auto const contact_a{blend(
    vertices[face.indices[0]].support_a,
    vertices[face.indices[1]].support_a,
    vertices[face.indices[2]].support_a
  )};
  auto const contact_b{blend(
    vertices[face.indices[0]].support_b,
    vertices[face.indices[1]].support_b,
    vertices[face.indices[2]].support_b
  )};
  return {contact_a, contact_b};
}

/**
 * @brief Grows a GJK terminal simplex into a non-degenerate seed tetrahedron.
 *
 * The signed-volumes GJK reports overlap with whatever simplex carries the origin
 * (a point, edge, triangle, or tetrahedron), but EPA needs a full tetrahedron to
 * expand. This adds support points to bring the simplex up to four affinely
 * independent vertices: a distinct second vertex along the world axes, a third
 * perpendicular to the resulting edge, and a fourth along the triangle normal
 * (whichever side reaches further off the plane). The origin, which lay on the
 * original lower simplex, ends up on a face or edge of the seed, which
 * \c build_face handles by orienting against the seed centroid. A simplex that
 * cannot be grown to a non-degenerate tetrahedron (coincident or collinear
 * supports) yields an empty result, on which EPA reports non-convergence.
 *
 * @tparam Real Component type, deduced from \p initial.
 * @tparam ShapeA First shape type; must satisfy \c convex_shape.
 * @tparam ShapeB Second shape type; must satisfy \c convex_shape.
 * @param a First convex shape.
 * @param b Second convex shape.
 * @param initial GJK terminal simplex (1 to 4 vertices carrying the origin).
 *
 * @return Four Minkowski-difference vertices forming a non-degenerate
 *         tetrahedron, or an empty vector when one cannot be built.
 *
 * @pre \p a and \p b overlap and \c initial.count is 1 to 4.
 * @post On success the result has four vertices with non-zero enclosed volume.
 */
template <std::floating_point Real, convex_shape<Real> ShapeA, convex_shape<Real> ShapeB>
[[nodiscard]] auto
seed_tetrahedron(ShapeA const& a, ShapeB const& b, gjk_simplex3<Real> const& initial) noexcept
  -> std::vector<gjk_minkowski_point3<Real>> {
  using point_type = nexenne::math::vector<Real, 3>;
  // Absolute distinctness and volume floor. This is calibrated for game-scale
  // shapes (roughly 1e-2 to 1e4 units): a pair much smaller than 1e-4 units, or a
  // contact between huge shapes whose supports differ only in their garbage
  // digits, can shift its meaning and yield an empty seed (a spurious
  // non-convergence). Scale the inputs into that band if this bites.
  auto const eps{static_cast<Real>(1e-12)};

  auto verts{std::vector<gjk_minkowski_point3<Real>>{}};
  verts.reserve(32);
  for (auto i{std::size_t{0}}; i < initial.count; ++i) {
    verts.push_back(initial.points[i]);
  }

  auto const ms{[&](point_type const& d) noexcept -> gjk_minkowski_point3<Real> {
    auto const pa{support(a, d)};
    auto const pb{support(b, -d)};
    return gjk_minkowski_point3<Real>{pa - pb, pa, pb};
  }};
  auto const distinct{[&](point_type const& p) noexcept -> bool {
    for (auto const& v : verts) {
      if (nexenne::math::length_squared(p - v.difference) <= eps) {
        return false;
      }
    }
    return true;
  }};

  // Grow to a second vertex: probe the world axes for a support distinct from the
  // first (a one-vertex simplex means the shapes touch at a single point).
  if (verts.size() < 2) {
    auto const axes{std::array<point_type, 6>{
      point_type{Real{1}, Real{0}, Real{0}},
      point_type{Real{-1}, Real{0}, Real{0}},
      point_type{Real{0}, Real{1}, Real{0}},
      point_type{Real{0}, Real{-1}, Real{0}},
      point_type{Real{0}, Real{0}, Real{1}},
      point_type{Real{0}, Real{0}, Real{-1}},
    }};
    for (auto const& d : axes) {
      auto const w{ms(d)};
      if (distinct(w.difference)) {
        verts.push_back(w);
        break;
      }
    }
    if (verts.size() < 2) {
      return {};
    }
  }

  // Grow to a triangle: search perpendicular to the edge, crossing it with the
  // least-aligned world axis so the two operands are well clear of parallel.
  if (verts.size() < 3) {
    auto const edge{verts[1].difference - verts[0].difference};
    auto const ax{nexenne::math::abs(edge.x())};
    auto const ay{nexenne::math::abs(edge.y())};
    auto const az{nexenne::math::abs(edge.z())};
    auto const axis{
      (ax <= ay && ax <= az) ? point_type{Real{1}, Real{0}, Real{0}}
      : (ay <= az)           ? point_type{Real{0}, Real{1}, Real{0}}
                             : point_type{Real{0}, Real{0}, Real{1}}
    };
    auto const dir{nexenne::math::cross(edge, axis)};
    if (nexenne::math::length_squared(dir) <= eps) {
      return {};
    }
    auto w{ms(dir)};
    if (!distinct(w.difference)) {
      w = ms(-dir);
    }
    if (!distinct(w.difference)) {
      return {};
    }
    verts.push_back(w);
  }

  // Grow to a tetrahedron: probe both sides of the triangle along its normal and
  // take the apex that reaches further off the plane.
  if (verts.size() < 4) {
    auto const normal{nexenne::math::cross(
      verts[1].difference - verts[0].difference, verts[2].difference - verts[0].difference
    )};
    if (nexenne::math::length_squared(normal) <= eps) {
      return {};
    }
    auto const wp{ms(normal)};
    auto const wn{ms(-normal)};
    auto const reach_p{
      nexenne::math::abs(nexenne::math::dot(wp.difference - verts[0].difference, normal))
    };
    auto const reach_n{
      nexenne::math::abs(nexenne::math::dot(wn.difference - verts[0].difference, normal))
    };
    auto const w{reach_p >= reach_n ? wp : wn};
    if (!distinct(w.difference)) {
      return {};
    }
    verts.push_back(w);
  }

  // Reject a flat (zero-volume) tetrahedron: EPA cannot expand a degenerate seed.
  auto const volume{nexenne::math::dot(
    verts[1].difference - verts[0].difference,
    nexenne::math::cross(
      verts[2].difference - verts[0].difference, verts[3].difference - verts[0].difference
    )
  )};
  if (nexenne::math::abs(volume) <= eps) {
    return {};
  }
  return verts;
}

}  // namespace detail

/**
 * @brief Recovers penetration depth, normal, and contact points from an
 *        overlapping GJK result via the Expanding Polytope Algorithm.
 *
 * Seeds a polytope with the GJK terminal tetrahedron, then repeatedly finds the
 * face nearest the origin, asks the shapes for a support point along that face's
 * normal, and either declares convergence (the support point does not push the
 * face outward by more than \p tolerance) or expands the polytope: it removes the
 * faces visible from the new vertex and stitches the resulting horizon to it. On
 * convergence the closest face yields the separation normal and depth, and its
 * barycentric weights blend the support pairs into per-shape contact points.
 *
 * @tparam Real Floating-point component type, deduced from \p initial.
 * @tparam ShapeA First shape type; must satisfy \c convex_shape.
 * @tparam ShapeB Second shape type; must satisfy \c convex_shape.
 * @param a First convex shape.
 * @param b Second convex shape.
 * @param initial Terminal GJK simplex; must be a 4-vertex tetrahedron enclosing
 *        the origin (a \c gjk overlap result).
 * @param max_iterations Hard cap on expansion steps (a safe backstop; smooth
 *        pairs need more steps than flat ones).
 * @param tolerance Relative convergence threshold: expansion stops once a step's
 *        depth growth falls below \p tolerance times the current depth.
 *
 * @return Result with the normal (the MTV direction, out of A toward B),
 *         penetration depth, and contact
 *         points; \c converged is \c false when \p initial was not a tetrahedron
 *         or the iteration cap was hit (the best-known face, with its
 *         reconstructed contact points, is still returned).
 *
 * @pre \p a and \p b overlap and \c initial.count equals 4.
 * @post On success \c converged is \c true, \c normal has unit length, and
 *       \c penetration_depth is non-negative.
 * @complexity \c O(max_iterations) support queries; each expansion step is linear
 *             in the current face count. Allocates the polytope.
 */
template <std::floating_point Real, convex_shape<Real> ShapeA, convex_shape<Real> ShapeB>
[[nodiscard]] auto epa(
  ShapeA const& a,
  ShapeB const& b,
  gjk_simplex3<Real> const& initial,
  std::size_t const max_iterations = 64,
  Real const tolerance = static_cast<Real>(1e-2)
) noexcept -> epa_result3<Real> {
  // Floor on the relative convergence scale, so a near-zero depth does not make
  // the threshold collapse to zero and iterate forever against the cap.
  auto const epsilon{static_cast<Real>(1e-6)};

  auto result{epa_result3<Real>{}};
  if (initial.count == 0) {
    return result;  // no simplex to seed from.
  }

  // The polytope starts from a seed tetrahedron grown out of the GJK simplex (a
  // shared vertex list plus triangular faces indexing into it). A simplex that
  // cannot grow to a non-degenerate tetrahedron leaves EPA non-converged.
  auto vertices{detail::seed_tetrahedron<Real>(a, b, initial)};
  if (vertices.size() < 4) {
    return result;
  }
  // One fresh vertex is pushed per expansion step, so size the pool for the cap
  // (seed_tetrahedron already reserved the seed; this covers the whole run).
  vertices.reserve(max_iterations + 4);

  // Centroid of the seed tetrahedron: a point strictly inside the polytope,
  // used to orient every face outward. It stays interior as the polytope only
  // grows, so the origin sitting on a seed face cannot misorient a normal.
  auto const interior{
    (vertices[0].difference + vertices[1].difference + vertices[2].difference
     + vertices[3].difference)
    * (Real{1} / Real{4})
  };

  auto faces{std::vector<detail::epa_face<Real>>{}};
  faces.reserve(2 * max_iterations + 4);
  // The four tetrahedron faces (one opposite each vertex); build_face orients
  // each outward, so the winding of these seed triples does not matter.
  faces.push_back(detail::build_face<Real>(vertices, 1, 2, 3, interior));
  faces.push_back(detail::build_face<Real>(vertices, 0, 2, 3, interior));
  faces.push_back(detail::build_face<Real>(vertices, 0, 1, 3, interior));
  faces.push_back(detail::build_face<Real>(vertices, 0, 1, 2, interior));

  for (auto iter{std::size_t{0}}; iter < max_iterations; ++iter) {
    // Find the face closest to the origin: its normal is the current best guess
    // at the penetration direction.
    auto closest{std::size_t{0}};
    for (auto i{std::size_t{1}}; i < faces.size(); ++i) {
      if (faces[i].distance < faces[closest].distance) {
        closest = i;
      }
    }
    auto const direction{faces[closest].normal};
    auto const closest_distance{faces[closest].distance};

    // Probe the Minkowski difference along that normal.
    auto const pa{support(a, direction)};
    auto const pb{support(b, -direction)};
    auto const new_difference{pa - pb};
    auto const reach{nexenne::math::dot(new_difference, direction)};

    // Converged when the surface is no further out than the closest face by more
    // than a fraction of the current depth. The threshold is RELATIVE, not
    // absolute: the Minkowski difference of two smooth shapes (two spheres, two
    // capsules) is itself smooth, so every polytope face sits a
    // curvature-dependent gap inside the true surface and an absolute floor would
    // demand thousands of faces to close. Scaling by the depth lets a smooth pair
    // converge in a bounded step count, while a flat contact (box pairs), whose
    // faces reach the surface exactly, still converges at once.
    if (reach - closest_distance < tolerance * nexenne::math::max(closest_distance, epsilon)) {
      // On convergence the closest face is on the true surface: its outward normal
      // of A (-) B is the minimum-translation direction (out of A toward B), its
      // origin distance the depth, and its reconstructed contacts the deepest pair.
      auto const [contact_a, contact_b]{
        detail::face_contact_points<Real>(vertices, faces[closest], closest_distance)
      };
      result.contact_point_a = contact_a;
      result.contact_point_b = contact_b;
      result.normal = direction;
      result.penetration_depth = closest_distance;
      result.converged = true;
      return result;
    }

    // Otherwise expand: add the new vertex and re-triangulate around it. Faces
    // the new vertex can "see" (its position is in front of their plane) are
    // removed; the boundary of that visible region (the horizon) is stitched to
    // the new vertex with fresh faces.
    auto const new_index{vertices.size()};
    vertices.push_back(gjk_minkowski_point3<Real>{new_difference, pa, pb});

    // Horizon edges: an edge shared by two visible faces is interior and cancels;
    // an edge on the silhouette survives. add_edge keeps only the un-cancelled.
    // It scans the horizon linearly per edge, so building it is O(h^2) in the
    // horizon size h; h stays small at the iteration cap, so this is not sorted.
    auto horizon{std::vector<std::array<std::size_t, 2>>{}};
    auto const add_edge{[&](std::size_t const i, std::size_t const j) noexcept {
      for (auto it{horizon.begin()}; it != horizon.end(); ++it) {
        if ((*it)[0] == j && (*it)[1] == i) {  // the reverse edge: interior, cancel.
          horizon.erase(it);
          return;
        }
      }
      horizon.push_back({i, j});
    }};

    auto visible{std::vector<std::size_t>{}};
    for (auto i{std::size_t{0}}; i < faces.size(); ++i) {
      auto const& on_face{vertices[faces[i].indices[0]].difference};
      if (nexenne::math::dot(faces[i].normal, new_difference - on_face) > Real{0}) {
        visible.push_back(i);
        add_edge(faces[i].indices[0], faces[i].indices[1]);
        add_edge(faces[i].indices[1], faces[i].indices[2]);
        add_edge(faces[i].indices[2], faces[i].indices[0]);
      }
    }

    if (visible.empty() || horizon.empty()) {
      // Defensive: the new vertex should always see at least the closest face.
      // Return the best face's actual contact points, not a zeroed pair.
      auto const [contact_a, contact_b]{
        detail::face_contact_points<Real>(vertices, faces[closest], closest_distance)
      };
      result.contact_point_a = contact_a;
      result.contact_point_b = contact_b;
      result.normal = direction;
      result.penetration_depth = closest_distance;
      result.converged = false;
      return result;
    }

    // Remove visible faces back-to-front so earlier indices stay valid.
    for (auto it{visible.rbegin()}; it != visible.rend(); ++it) {
      faces.erase(faces.begin() + static_cast<std::ptrdiff_t>(*it));
    }
    // Cap the horizon to the new vertex with one face per surviving edge.
    for (auto const& edge : horizon) {
      faces.push_back(detail::build_face<Real>(vertices, new_index, edge[0], edge[1], interior));
    }
  }

  // No convergence within the cap: return the best-known face as the estimate,
  // including its reconstructed contact points (M2), so a non-converged result is
  // still usable rather than carrying a value-initialized (origin) contact pair.
  auto closest{std::size_t{0}};
  for (auto i{std::size_t{1}}; i < faces.size(); ++i) {
    if (faces[i].distance < faces[closest].distance) {
      closest = i;
    }
  }
  auto const [contact_a, contact_b]{
    detail::face_contact_points<Real>(vertices, faces[closest], faces[closest].distance)
  };
  result.contact_point_a = contact_a;
  result.contact_point_b = contact_b;
  result.normal = faces[closest].normal;
  result.penetration_depth = faces[closest].distance;
  result.converged = false;
  return result;
}

/**
 * @brief A contact manifold: the small set of contact points shared by two
 *        overlapping shapes, with the shared normal.
 *
 * EPA alone yields one deepest contact point, which is enough to push two shapes
 * apart but not to keep a face-to-face contact (a box resting on the ground) from
 * rocking. A manifold is the polygon where the two contact faces meet: up to a
 * handful of points on the contact plane that a solver applies impulses at. A
 * curved contact (sphere, capsule) has no face, so the manifold collapses to the
 * single EPA point.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct contact_manifold3 {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  std::array<point_type, 8> points{};  ///< Contact points on the contact plane.
  std::size_t count{0};                ///< Number of valid points, 1 to 8.
  point_type normal{};  ///< Shared contact normal (the MTV direction, out of A toward B).
};

namespace detail {

/**
 * @brief A tangent basis (t1, t2) spanning the plane perpendicular to \p n.
 *
 * Crosses \p n with whichever world axis is least aligned with it, so the two
 * operands stay well clear of parallel and the basis is well-conditioned.
 *
 * @tparam Real Component type.
 * @param n Unit normal.
 * @param t1 Receives the first tangent (unit length).
 * @param t2 Receives the second tangent (unit length, completing a right-handed
 *        frame with \p n).
 *
 * @pre \p n has unit length.
 * @post \p t1 and \p t2 are unit length and orthogonal to \p n and each other.
 */
template <std::floating_point Real>
constexpr auto tangent_basis(
  nexenne::math::vector<Real, 3> const& n,
  nexenne::math::vector<Real, 3>& t1,
  nexenne::math::vector<Real, 3>& t2
) noexcept -> void {
  using vector_type = nexenne::math::vector<Real, 3>;
  auto const ax{nexenne::math::abs(n.x())};
  auto const ay{nexenne::math::abs(n.y())};
  auto const az{nexenne::math::abs(n.z())};
  auto const axis{
    (ax <= ay && ax <= az) ? vector_type{Real{1}, Real{0}, Real{0}}
    : (ay <= az)           ? vector_type{Real{0}, Real{1}, Real{0}}
                           : vector_type{Real{0}, Real{0}, Real{1}}
  };
  t1 = nexenne::math::normalize_or(
    nexenne::math::cross(n, axis), vector_type{Real{1}, Real{0}, Real{0}}
  );
  t2 = nexenne::math::cross(n, t1);
}

/**
 * @brief Extracts a shape's contact face by ring-sampling supports around \p dir.
 *
 * A support shape exposes no faces, so the contact face is recovered by tilting
 * the search direction a little toward eight points around the tangent circle and
 * collecting the distinct support points. A flat face yields its corners in
 * winding order; a curved surface yields (nearly) one point, which the caller
 * treats as a single contact.
 *
 * @tparam Real Component type.
 * @tparam Shape Convex shape type.
 * @param shape Shape to sample.
 * @param dir Face direction (the contact normal pointing out of \p shape).
 * @param t1 First plane tangent.
 * @param t2 Second plane tangent.
 * @param out Receives up to eight distinct face points in ring order.
 *
 * @return The number of distinct points written to \p out.
 *
 * @pre \p dir, \p t1, \p t2 form an orthonormal frame.
 * @post The result is between 1 and 8.
 */
template <std::floating_point Real, typename Shape>
[[nodiscard]] auto sample_contact_face(
  Shape const& shape,
  nexenne::math::vector<Real, 3> const& dir,
  nexenne::math::vector<Real, 3> const& t1,
  nexenne::math::vector<Real, 3> const& t2,
  std::array<nexenne::math::vector<Real, 3>, 8>& out
) noexcept -> std::size_t {
  // A small tangential tilt keeps the probe on the face perpendicular to dir while
  // steering it toward each corner; pre-tabulated cos/sin of k*45 degrees keep the
  // routine constexpr-friendly and trig-free.
  auto const diag{static_cast<Real>(0.70710678)};  // cos/sin of 45 degrees.
  auto const ring{std::array<std::array<Real, 2>, 8>{
    std::array<Real, 2>{Real{1}, Real{0}},
    std::array<Real, 2>{diag, diag},
    std::array<Real, 2>{Real{0}, Real{1}},
    std::array<Real, 2>{-diag, diag},
    std::array<Real, 2>{Real{-1}, Real{0}},
    std::array<Real, 2>{-diag, -diag},
    std::array<Real, 2>{Real{0}, Real{-1}},
    std::array<Real, 2>{diag, -diag},
  }};
  auto const tilt{static_cast<Real>(0.08)};
  auto const eps{static_cast<Real>(1e-8)};
  auto count{std::size_t{0}};
  for (auto const& cs : ring) {
    auto const probe{dir + (t1 * cs[0] + t2 * cs[1]) * tilt};
    auto const p{support(shape, probe)};
    auto duplicate{false};
    for (auto i{std::size_t{0}}; i < count; ++i) {
      if (nexenne::math::length_squared(p - out[i]) <= eps) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) {
      out[count] = p;
      ++count;
    }
  }
  return count;
}

}  // namespace detail

/**
 * @brief Builds a contact manifold for two overlapping shapes from an EPA result.
 *
 * Recovers the contact face of each shape (by ring-sampling its support function
 * around the EPA normal), projects both onto the contact plane, and clips one
 * against the other with the Sutherland-Hodgman algorithm: the clipped polygon is
 * the region where the faces actually meet. When either contact is a single point
 * (a curved surface, or a vertex/edge contact) the manifold is just the EPA
 * contact point, so the result always has at least one point.
 *
 * @tparam Real Floating-point component type.
 * @tparam ShapeA First shape type; must satisfy \c convex_shape.
 * @tparam ShapeB Second shape type; must satisfy \c convex_shape.
 * @param a First convex shape.
 * @param b Second convex shape.
 * @param hit A converged EPA result for \p a and \p b.
 *
 * @return The manifold: contact points on the contact plane and the shared
 *         normal (the MTV direction, out of A toward B).
 *
 * @pre \p hit came from \c epa on \p a and \p b and \c hit.converged is true.
 * @post \c count is between 1 and 8 and \c normal equals \c hit.normal.
 */
template <std::floating_point Real, convex_shape<Real> ShapeA, convex_shape<Real> ShapeB>
[[nodiscard]] auto
contact_manifold(ShapeA const& a, ShapeB const& b, epa_result3<Real> const& hit) noexcept
  -> contact_manifold3<Real> {
  using point_type = nexenne::math::vector<Real, 3>;
  using planar = nexenne::math::vector<Real, 2>;

  auto result{contact_manifold3<Real>{}};
  result.normal = hit.normal;
  // The single deepest point is always a valid fallback manifold.
  result.points[0] = (hit.contact_point_a + hit.contact_point_b) * Real{0.5};
  result.count = 1;

  auto t1{point_type{}};
  auto t2{point_type{}};
  detail::tangent_basis(hit.normal, t1, t2);

  // A's contact face points along -normal (toward B); B's along +normal.
  auto face_a{std::array<point_type, 8>{}};
  auto face_b{std::array<point_type, 8>{}};
  auto const na{detail::sample_contact_face<Real>(a, -hit.normal, t1, t2, face_a)};
  auto const nb{detail::sample_contact_face<Real>(b, hit.normal, t1, t2, face_b)};
  // A flat face repeats its few corners across the eight probes (3 to ~6 distinct
  // points); a smooth surface returns a fresh point for every probe (all eight
  // distinct). Treat fewer than three or all-eight as "no usable flat face" and
  // keep the single contact point.
  if (na < 3 || na >= std::size_t{8} || nb < 3 || nb >= std::size_t{8}) {
    return result;
  }

  // Project both faces onto the contact plane, using the fallback point as origin.
  auto const origin{result.points[0]};
  auto const to_plane{[&](point_type const& p) noexcept -> planar {
    return planar{nexenne::math::dot(p - origin, t1), nexenne::math::dot(p - origin, t2)};
  }};
  auto subject{std::array<planar, 8>{}};
  auto clip{std::array<planar, 8>{}};
  for (auto i{std::size_t{0}}; i < na; ++i) {
    subject[i] = to_plane(face_a[i]);
  }
  for (auto i{std::size_t{0}}; i < nb; ++i) {
    clip[i] = to_plane(face_b[i]);
  }

  // The clip polygon's winding sets the inside half-plane sign; sample its signed
  // area so the edge test keeps the correct side regardless of sample order.
  auto signed_area{Real{0}};
  for (auto i{std::size_t{0}}; i < nb; ++i) {
    auto const& p{clip[i]};
    auto const& q{clip[(i + 1) % nb]};
    signed_area += p.x() * q.y() - q.x() * p.y();
  }
  auto const inside_sign{signed_area >= Real{0} ? Real{1} : Real{-1}};

  // Sutherland-Hodgman: clip the subject polygon by each edge of the clip polygon.
  auto poly{std::array<planar, 16>{}};
  auto poly_n{std::size_t{0}};
  for (auto i{std::size_t{0}}; i < na; ++i) {
    poly[poly_n++] = subject[i];
  }
  auto const edge_inside{[&](planar const& e0, planar const& e1, planar const& p) noexcept -> Real {
    // Signed side of point p w.r.t. directed edge e0->e1, oriented so inside is >= 0.
    auto const s{(e1.x() - e0.x()) * (p.y() - e0.y()) - (e1.y() - e0.y()) * (p.x() - e0.x())};
    return s * inside_sign;
  }};
  for (auto c{std::size_t{0}}; c < nb && poly_n > 0; ++c) {
    auto const e0{clip[c]};
    auto const e1{clip[(c + 1) % nb]};
    auto next{std::array<planar, 16>{}};
    auto next_n{std::size_t{0}};
    for (auto i{std::size_t{0}}; i < poly_n; ++i) {
      auto const cur{poly[i]};
      auto const prv{poly[(i + poly_n - 1) % poly_n]};
      auto const cur_in{edge_inside(e0, e1, cur) >= Real{0}};
      auto const prv_in{edge_inside(e0, e1, prv) >= Real{0}};
      if (cur_in != prv_in) {
        // The edge prv->cur crosses the clip line: add the intersection.
        auto const dp{edge_inside(e0, e1, prv)};
        auto const dc{edge_inside(e0, e1, cur)};
        auto const tt{dp / (dp - dc)};
        if (next_n < next.size()) {
          next[next_n++] = prv + (cur - prv) * tt;
        }
      }
      if (cur_in && next_n < next.size()) {
        next[next_n++] = cur;
      }
    }
    poly = next;
    poly_n = next_n;
  }

  if (poly_n < 3) {
    return result;  // degenerate overlap: keep the single point.
  }

  // Lift the clipped polygon back onto the contact plane in world space, capping
  // at eight points (decimating evenly if the clip produced more).
  auto const out_n{poly_n <= result.points.size() ? poly_n : result.points.size()};
  result.count = out_n;
  for (auto i{std::size_t{0}}; i < out_n; ++i) {
    auto const& uv{poly[(i * poly_n) / out_n]};
    result.points[i] = origin + t1 * uv.x() + t2 * uv.y();
  }
  return result;
}

}  // namespace nexenne::geometry
