#pragma once

/**
 * @file
 * @brief Quaternion interpolation variants beyond the default slerp/nlerp.
 *
 * The canonical \c slerp (in quaternion.hpp) gives constant angular velocity, the
 * mathematically right choice for orientation interpolation, but it costs an
 * \c acos, a \c sin, and divisions. This header names the cheaper alternatives
 * explicitly so a call site can pick the exact algorithm:
 *
 * - \c nlerp_plain: normalized linear interpolation with no arc-length fix. One
 *   normalize, no trig. Slightly non-uniform velocity, invisible for small arcs
 *   (under about 45 degrees). The caller must pre-align the endpoints if the
 *   shorter arc matters.
 * - \c nlerp_short: \c nlerp_plain with the shorter-arc fix (negate one endpoint
 *   when the dot is negative). What most real-time code wants.
 * - \c slerp_short: shorter-arc spherical interpolation, falling back to
 *   \c nlerp_plain when the angle is tiny.
 *
 * \c nlerp_short and \c slerp_short are the same algorithms as quaternion.hpp's
 * \c nlerp and \c slerp; they exist here under explicit names alongside the
 * no-fix \c nlerp_plain so the choice is spelled out. All inputs are assumed unit
 * quaternions; outputs are renormalized. The interpolation reuses the quaternion
 * operators and \c dot / \c normalize rather than re-implementing them.
 */

#include <cmath>
#include <concepts>

#include <nexenne/math/quaternion.hpp>

namespace nexenne::math {

/**
 * @brief Normalized linear interpolation, with no arc-length adjustment.
 *
 * Linearly blends the two quaternions component-wise and renormalizes. Does not
 * pick the shorter arc, so pass endpoints already on the same hemisphere if that
 * matters (or use \c nlerp_short).
 *
 * @tparam Real Floating-point component type.
 * @param a Start quaternion at \c t == 0.
 * @param b End quaternion at \c t == 1.
 * @param t Interpolation parameter, normally in [0, 1].
 *
 * @return The unit quaternion interpolated between \p a and \p b.
 *
 * @pre \p a and \p b are unit length.
 * @post The result is unit length, except it is the unnormalized blend when \p a
 *       and \p b are exactly antipodal (a zero-length midpoint).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto nlerp_plain(
  quaternion<Real> const a, quaternion<Real> const b, Real const t
) noexcept -> quaternion<Real> {
  auto const mixed{a * (Real{1} - t) + b * t};
  return normalize(mixed).value_or(mixed);
}

/**
 * @brief Normalized linear interpolation along the shorter arc.
 *
 * Negates \p b when \c dot(a, b) is negative so the blend takes the shorter of
 * the two arcs, then delegates to \c nlerp_plain.
 *
 * @tparam Real Floating-point component type.
 * @param a Start quaternion at \c t == 0.
 * @param b End quaternion at \c t == 1.
 * @param t Interpolation parameter, normally in [0, 1].
 *
 * @return The unit quaternion interpolated along the shorter arc.
 *
 * @pre \p a and \p b are unit length.
 * @post The result is unit length and lies on the hemisphere nearer to \p a.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto nlerp_short(
  quaternion<Real> const a, quaternion<Real> b, Real const t
) noexcept -> quaternion<Real> {
  if (dot(a, b) < Real{0}) {
    b = -b;
  }
  return nlerp_plain(a, b, t);
}

/**
 * @brief Spherical linear interpolation along the shorter arc.
 *
 * Constant angular velocity along the shorter arc. Negates \p b when the
 * endpoints face opposite hemispheres, and falls back to \c nlerp_plain when the
 * angle is tiny, to avoid the \c 1/sin(theta) instability. The endpoint weights
 * are written in the incremental form \c s0 = cos(t*theta) - d*s1 and
 * \c s1 = sin(t*theta)/sin(theta), which is the same great-circle path as the
 * \c sin((1-t)*theta)/sin(theta) form.
 *
 * @tparam Real Floating-point component type.
 * @param a Start quaternion at \c t == 0.
 * @param b End quaternion at \c t == 1.
 * @param t Interpolation parameter, normally in [0, 1].
 *
 * @return The unit quaternion interpolated along the shorter arc.
 *
 * @pre \p a and \p b are unit length.
 * @post The result is unit length and lies on the shorter arc between \p a and
 *       \p b.
 */
template <std::floating_point Real>
[[nodiscard]] auto slerp_short(quaternion<Real> const a, quaternion<Real> b, Real const t) noexcept
  -> quaternion<Real> {
  auto d{dot(a, b)};
  if (d < Real{0}) {
    b = -b;
    d = -d;
  }
  if (d > static_cast<Real>(0.9995)) {
    // Nearly parallel: linear is safe and cheaper than the near-zero division.
    return nlerp_plain(a, b, t);
  }
  auto const theta_0{std::acos(d)};
  auto const sin_theta_0{std::sin(theta_0)};
  auto const theta{theta_0 * t};
  auto const s1{std::sin(theta) / sin_theta_0};
  auto const s0{std::cos(theta) - d * s1};
  return a * s0 + b * s1;
}

}  // namespace nexenne::math
