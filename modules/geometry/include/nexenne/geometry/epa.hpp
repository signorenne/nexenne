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
 * Output is a separation normal pointing from B's surface toward A's (the
 * Bullet/Jolt convention), the penetration depth, and per-shape contact points
 * reconstructed from the closest face via barycentric weights on the original
 * support pairs.
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
  point_type normal{};           ///< Unit penetration normal, from B toward A.
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
 * @param max_iterations Hard cap on expansion steps.
 * @param tolerance Convergence threshold on per-step face-distance improvement.
 *
 * @return Result with the normal (B toward A), penetration depth, and contact
 *         points; \c converged is \c false when \p initial was not a tetrahedron
 *         or the iteration cap was hit (the best-known face is still returned).
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
  std::size_t const max_iterations = 32,
  Real const tolerance = static_cast<Real>(1e-4)
) noexcept -> epa_result3<Real> {
  using point_type = nexenne::math::vector<Real, 3>;

  auto result{epa_result3<Real>{}};
  if (initial.count != 4) {
    return result;  // GJK did not terminate on a tetrahedron: nothing to expand.
  }

  // The polytope: a shared vertex list plus triangular faces indexing into it.
  auto vertices{std::vector<gjk_minkowski_point3<Real>>{}};
  vertices.reserve(32);
  for (auto i{std::size_t{0}}; i < 4; ++i) {
    vertices.push_back(initial.points[i]);
  }

  // Centroid of the seed tetrahedron: a point strictly inside the polytope,
  // used to orient every face outward. It stays interior as the polytope only
  // grows, so the origin sitting on a seed face cannot misorient a normal.
  auto const interior{
    (vertices[0].difference + vertices[1].difference + vertices[2].difference
     + vertices[3].difference)
    * (Real{1} / Real{4})
  };

  auto faces{std::vector<detail::epa_face<Real>>{}};
  faces.reserve(32);
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

    if (reach - closest_distance < tolerance) {
      // The surface is no further out than the closest face: converged. Blend
      // the support pairs by the origin projection's barycentric weights to get
      // the per-shape contact points.
      auto const& face{faces[closest]};
      auto const v0{vertices[face.indices[0]].difference};
      auto const v1{vertices[face.indices[1]].difference};
      auto const v2{vertices[face.indices[2]].difference};
      auto const projected_origin{direction * closest_distance};
      auto const weights{detail::barycentric<Real>(v0, v1, v2, projected_origin)};

      // Blend three world-space points by the barycentric weights.
      auto const blend{
        [&](point_type const& p0, point_type const& p1, point_type const& p2) noexcept {
          return p0 * weights.x() + p1 * weights.y() + p2 * weights.z();
        }
      };
      result.contact_point_a = blend(
        vertices[face.indices[0]].support_a,
        vertices[face.indices[1]].support_a,
        vertices[face.indices[2]].support_a
      );
      result.contact_point_b = blend(
        vertices[face.indices[0]].support_b,
        vertices[face.indices[1]].support_b,
        vertices[face.indices[2]].support_b
      );
      // The outward face normal of the Minkowski difference at the closest point
      // is exactly the B-toward-A separation direction the contact solver wants.
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

  // No convergence within the cap: return the best-known face as the estimate.
  auto closest{std::size_t{0}};
  for (auto i{std::size_t{1}}; i < faces.size(); ++i) {
    if (faces[i].distance < faces[closest].distance) {
      closest = i;
    }
  }
  result.normal = faces[closest].normal;
  result.penetration_depth = faces[closest].distance;
  result.converged = false;
  return result;
}

}  // namespace nexenne::geometry
