#pragma once

/**
 * @file
 * @brief Infinite 3D plane (plane3) in Hessian normal form.
 *
 * Stored as a unit \c normal plus a scalar \c d, so a point \c p lies on the
 * plane when \c dot(normal, p) + d == 0. Equivalently \c -d is the signed
 * distance from the origin to the plane along \c normal.
 *
 * Two factories cover the common inputs: a point with a normal, or three
 * points. Both normalize and report a degenerate primitive (a zero-length
 * normal, or collinear points) through \c result rather than producing a
 * NaN-laden plane. Everything is \c constexpr and \c noexcept.
 *
 * Aliases: \c plane3, \c plane3_f (float), \c plane3_d (double).
 */

#include <concepts>
#include <expected>
#include <type_traits>

#include <nexenne/geometry/error.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Infinite 3D plane in Hessian normal form (unit normal plus offset).
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class plane3 {
public:
  using value_type = Real;
  using vector_type = nexenne::math::vector<value_type, 3>;

private:
  vector_type m_normal{value_type{0}, value_type{0}, value_type{1}};
  value_type m_d{};

public:
  /// @brief Constructs the XY plane through the origin (normal +Z, d 0).
  constexpr plane3() noexcept = default;

  /**
   * @brief Constructs a plane from a (unit) normal and offset.
   *
   * No normalization is performed; the caller passes a unit normal (see
   * \c plane_from_point_normal for a checked, normalizing factory).
   *
   * @param normal Plane normal, expected unit length.
   * @param d Signed offset so points satisfy \c dot(normal, p) + d == 0.
   */
  constexpr plane3(vector_type normal, value_type const d) noexcept : m_normal{normal}, m_d{d} {}

  /**
   * @brief Plane normal.
   *
   * @return Const reference to the stored normal.
   */
  [[nodiscard]] constexpr auto normal() const noexcept -> vector_type const& {
    return m_normal;
  }

  /**
   * @brief Signed plane offset.
   *
   * @return Const reference to the stored offset.
   */
  [[nodiscard]] constexpr auto d() const noexcept -> value_type const& {
    return m_d;
  }

  /**
   * @brief Mutable plane normal.
   *
   * @return Reference to the stored normal.
   */
  [[nodiscard]] constexpr auto normal() noexcept -> vector_type& {
    return m_normal;
  }

  /**
   * @brief Mutable signed plane offset.
   *
   * @return Reference to the stored offset.
   */
  [[nodiscard]] constexpr auto d() noexcept -> value_type& {
    return m_d;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto operator<=>(plane3 const&, plane3 const&) noexcept = default;
};

using plane3_f = plane3<float>;
using plane3_d = plane3<double>;

static_assert(std::is_trivially_copyable_v<plane3_f>);
static_assert(std::is_standard_layout_v<plane3_f>);
static_assert(sizeof(plane3_f) == 4 * sizeof(float));

/**
 * @brief Plane through \p point with the given \p normal direction.
 *
 * Normalizes \p normal internally, so any non-zero direction is accepted.
 *
 * @tparam Real Component type.
 * @param point A point on the plane.
 * @param normal Plane normal (need not be unit).
 *
 * @return The plane on success, or \c geometry_error::degenerate_primitive when
 *         \p normal is too short to normalize.
 *
 * @pre None. \p normal is validated.
 * @post On success the plane has a unit-length normal and passes through
 *       \p point.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto plane_from_point_normal(
  nexenne::math::vector<Real, 3> const& point, nexenne::math::vector<Real, 3> const& normal
) noexcept -> result<plane3<Real>> {
  auto const n{nexenne::math::normalize(normal)};
  if (!n) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  return plane3<Real>{*n, -nexenne::math::dot(*n, point)};
}

/**
 * @brief Plane through three non-collinear points.
 *
 * The normal follows the right-hand rule on \c (a, b, c): viewing the points
 * counter-clockwise, the normal points toward the viewer.
 *
 * @tparam Real Component type.
 * @param a First point.
 * @param b Second point.
 * @param c Third point.
 *
 * @return The plane on success, or \c geometry_error::degenerate_primitive when
 *         the three points are collinear.
 *
 * @pre None. Collinearity is detected and reported.
 * @post On success the plane has a unit-length normal and passes through all
 *       three points.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto plane_from_three_points(
  nexenne::math::vector<Real, 3> const& a,
  nexenne::math::vector<Real, 3> const& b,
  nexenne::math::vector<Real, 3> const& c
) noexcept -> result<plane3<Real>> {
  auto const n{nexenne::math::normalize(nexenne::math::cross(b - a, c - a))};
  if (!n) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  return plane3<Real>{*n, -nexenne::math::dot(*n, a)};
}

/**
 * @brief Signed distance from \p p to the plane.
 *
 * Positive in the half-space the normal points to, negative on the other side,
 * zero on the plane.
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param p Query point.
 *
 * @return \c dot(pl.normal(), p) + pl.d().
 *
 * @pre \c pl.normal() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
signed_distance(plane3<Real> const& pl, nexenne::math::vector<Real, 3> const& p) noexcept -> Real {
  return nexenne::math::dot(pl.normal(), p) + pl.d();
}

/**
 * @brief Unsigned distance from \p p to the plane.
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param p Query point.
 *
 * @return \c |signed_distance(pl, p)|.
 *
 * @pre \c pl.normal() has unit length.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
distance(plane3<Real> const& pl, nexenne::math::vector<Real, 3> const& p) noexcept -> Real {
  return nexenne::math::abs(signed_distance(pl, p));
}

/**
 * @brief Closest point on the plane to \p p (orthogonal projection).
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param p Query point.
 *
 * @return The orthogonal projection of \p p onto the plane.
 *
 * @pre \c pl.normal() has unit length.
 * @post \c contains_point(pl, result) is \c true (up to rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
closest_point(plane3<Real> const& pl, nexenne::math::vector<Real, 3> const& p) noexcept
  -> nexenne::math::vector<Real, 3> {
  return p - pl.normal() * signed_distance(pl, p);
}

/**
 * @brief Reports whether \p p lies on the plane within \p tolerance.
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param p Query point.
 * @param tolerance Absolute distance tolerance. Default 1e-6.
 *
 * @return \c true when \c distance(pl, p) <= tolerance.
 *
 * @pre \c pl.normal() has unit length and \p tolerance is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto contains_point(
  plane3<Real> const& pl,
  nexenne::math::vector<Real, 3> const& p,
  Real const tolerance = static_cast<Real>(1e-6)
) noexcept -> bool {
  return distance(pl, p) <= tolerance;
}

}  // namespace nexenne::geometry
