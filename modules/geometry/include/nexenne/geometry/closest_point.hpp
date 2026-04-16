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
 *   - \c closest_points(capsule, capsule): the same on the two central spines, a
 *     building block for capsule-capsule penetration depth.
 *
 * Everything is \c constexpr and \c noexcept and nothing allocates.
 */

#include <concepts>
#include <cstddef>
#include <utility>

#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/segment.hpp>
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
