#pragma once

/**
 * @file
 * @brief Closest-point queries between two extended primitives.
 *
 * Point-to-primitive queries are simpler and live next to each primitive (see
 * \c segment.hpp, \c sphere.hpp, and the rest). This header collects the
 * cross-type pairs where both inputs are extended primitives:
 *   - \c closest_points(segment, segment): the pair of points realizing the
 *     minimum distance between two segments.
 *   - \c closest_points(segment, triangle): the closest pair between a 3D segment
 *     and a triangle, handling the segment piercing the triangle face.
 *   - \c closest_points(capsule, capsule): the same on the two central spines, a
 *     building block for capsule-capsule penetration depth.
 *
 * Everything is \c constexpr and \c noexcept and nothing allocates.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <utility>

#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Closest pair of points between two segments.
 *
 * Returns \c {p1, p2} with \c p1 on \p s1 and \c p2 on \p s2 realizing the
 * minimum separation. Handles every degeneracy: one or both segments collapsed
 * to a point, and parallel segments.
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param s1 First segment.
 * @param s2 Second segment.
 *
 * @return The closest pair, \c .first on \p s1 and \c .second on \p s2.
 *
 * @pre None. All degenerate configurations are handled.
 * @post \c .first lies on \p s1 and \c .second on \p s2, at minimum separation.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto closest_points(
  segment<Real, N> const& s1, segment<Real, N> const& s2
) noexcept -> std::pair<nexenne::math::vector<Real, N>, nexenne::math::vector<Real, N>> {
  // Ericson, "Real-Time Collision Detection", section 5.1.9. Parametrize the
  // segments as s1(s) = s1.start + s*d1 and s2(t) = t2.start + t*d2 with s, t in
  // [0, 1]. Minimizing the squared distance gives a 2x2 linear system in (s, t);
  // a, e are the squared edge lengths, b is d1.d2, and r is the start offset. The
  // denominator a*e - b*b is the Gram determinant: it is zero exactly when the
  // edges are parallel (the angle term collapses), so we fall back to s = 0 there
  // and recover t by projection. Each parameter is clamped back into [0, 1] and
  // the other re-solved, which is what turns the infinite-line solution into the
  // finite-segment one. The leading guards handle a degenerate (zero-length)
  // segment, where its direction carries no information.
  auto const d1{s1.end() - s1.start()};
  auto const d2{s2.end() - s2.start()};
  auto const r{s1.start() - s2.start()};
  auto const a{nexenne::math::dot(d1, d1)};  // squared length of segment 1
  auto const e{nexenne::math::dot(d2, d2)};  // squared length of segment 2
  auto const f{nexenne::math::dot(d2, r)};

  auto const epsilon{static_cast<Real>(1e-20)};
  auto s{Real{0}};
  auto t{Real{0}};

  if (a <= epsilon && e <= epsilon) {
    return {s1.start(), s2.start()};  // both segments are points
  }
  if (a <= epsilon) {
    // Segment 1 is a point: clamp t to segment 2.
    t = nexenne::math::clamp(f / e, Real{0}, Real{1});
  } else {
    auto const c{nexenne::math::dot(d1, r)};
    if (e <= epsilon) {
      // Segment 2 is a point: clamp s to segment 1.
      s = nexenne::math::clamp(-c / a, Real{0}, Real{1});
    } else {
      auto const b{nexenne::math::dot(d1, d2)};
      auto const denom{a * e - b * b};  // Gram determinant, 0 when parallel
      if (denom != Real{0}) {
        s = nexenne::math::clamp((b * f - c * e) / denom, Real{0}, Real{1});
      }
      // Recover t for this s, then, if it fell outside [0, 1], pin it to the
      // nearer end and re-solve s for that fixed t.
      t = (b * s + f) / e;
      if (t < Real{0}) {
        t = Real{0};
        s = nexenne::math::clamp(-c / a, Real{0}, Real{1});
      } else if (t > Real{1}) {
        t = Real{1};
        s = nexenne::math::clamp((b - c) / a, Real{0}, Real{1});
      }
    }
  }
  return {s1.start() + d1 * s, s2.start() + d2 * t};
}

/**
 * @brief Closest pair of points between a 3D segment and a triangle.
 *
 * Returns \c {p, q} with \c p on \p seg and \c q on \p tri at minimum separation.
 * If the segment pierces the triangle's interior the two points coincide at the
 * crossing (zero distance); otherwise the minimum is realized on a feature, so it
 * is the nearest of the three segment-versus-edge closest pairs and the two
 * segment endpoints projected onto the triangle.
 *
 * @tparam Real Component type.
 * @param seg Segment.
 * @param tri Triangle.
 *
 * @return The closest pair, \c .first on \p seg and \c .second on \p tri.
 *
 * @pre None. A degenerate triangle reduces to its edges.
 * @post \c .first lies on \p seg and \c .second on \p tri, at minimum separation.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_points(
  segment<Real, 3> const& seg, triangle<Real, 3> const& tri
) noexcept -> std::pair<nexenne::math::vector<Real, 3>, nexenne::math::vector<Real, 3>> {
  using vector_type = nexenne::math::vector<Real, 3>;
  using nexenne::math::dot;
  auto const epsilon{static_cast<Real>(1e-12)};

  // Pierce test: if the segment crosses the triangle plane between its endpoints
  // and the crossing lands inside the triangle, the closest distance is zero.
  auto const normal{nexenne::math::cross(tri.b() - tri.a(), tri.c() - tri.a())};
  if (nexenne::math::length_squared(normal) > epsilon) {
    auto const da{dot(normal, seg.start() - tri.a())};
    auto const db{dot(normal, seg.end() - tri.a())};
    if ((da < Real{0}) != (db < Real{0})) {  // endpoints straddle the plane.
      auto const t{da / (da - db)};
      auto const pierce{seg.start() + (seg.end() - seg.start()) * t};
      if (nexenne::math::length_squared(closest_point(tri, pierce) - pierce) <= epsilon) {
        return {pierce, pierce};
      }
    }
  }

  // Otherwise enumerate the feature candidates and keep the nearest pair.
  auto best{std::pair<vector_type, vector_type>{seg.start(), tri.a()}};
  auto best_dist_sq{nexenne::math::length_squared(best.second - best.first)};
  auto const consider{[&](vector_type const& p, vector_type const& q) noexcept {
    auto const d{nexenne::math::length_squared(q - p)};
    if (d < best_dist_sq) {
      best_dist_sq = d;
      best = {p, q};
    }
  }};

  // Segment versus each triangle edge.
  auto const edges{std::array<segment<Real, 3>, 3>{
    segment<Real, 3>{tri.a(), tri.b()},
    segment<Real, 3>{tri.b(), tri.c()},
    segment<Real, 3>{tri.c(), tri.a()},
  }};
  for (auto const& edge : edges) {
    auto const pair{closest_points(seg, edge)};
    consider(pair.first, pair.second);
  }
  // Each segment endpoint against the triangle face.
  consider(seg.start(), closest_point(tri, seg.start()));
  consider(seg.end(), closest_point(tri, seg.end()));
  return best;
}

/**
 * @brief Closest pair of points on the central spines of two capsules.
 *
 * The distance between the returned points minus the two radii is the signed
 * capsule-capsule gap (negative means the capsules overlap).
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c1 First capsule.
 * @param c2 Second capsule.
 *
 * @return The closest pair of spine points, \c .first on \p c1, \c .second on
 *         \p c2.
 *
 * @pre None.
 * @post \c .first lies on the spine of \p c1 and \c .second on that of \p c2.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto closest_points(
  capsule<Real, N> const& c1, capsule<Real, N> const& c2
) noexcept -> std::pair<nexenne::math::vector<Real, N>, nexenne::math::vector<Real, N>> {
  return closest_points(axis(c1), axis(c2));
}

}  // namespace nexenne::geometry
