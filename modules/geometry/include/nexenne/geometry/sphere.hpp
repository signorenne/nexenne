#pragma once

/**
 * @file
 * @brief 3D ball (sphere3): center plus radius, with its metric queries.
 *
 * The sphere here is the solid 3D ball, not the 2D surface: \c volume and
 * \c surface_area replace the disc's \c area and \c circumference, and the
 * containment and closest-point queries mirror \c circle2 one-to-one in three
 * dimensions. Everything is \c constexpr and \c noexcept; nothing allocates.
 *
 * Aliases: \c sphere3, \c sphere3_f (float), \c sphere3_d (double).
 */

#include <concepts>
#include <type_traits>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Solid 3D ball stored as a center point and a radius.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class sphere3 {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, 3>;

private:
  point_type m_center{};
  value_type m_radius{};

public:
  /// @brief Constructs the zero-radius ball at the origin.
  constexpr sphere3() noexcept = default;

  /**
   * @brief Constructs a ball from a center and radius.
   *
   * @param center Ball center.
   * @param radius Ball radius (expected non-negative).
   */
  constexpr sphere3(point_type center, value_type const radius) noexcept
      : m_center{center}, m_radius{radius} {}

  /**
   * @brief Ball center.
   *
   * @return Const reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() const noexcept -> point_type const& {
    return m_center;
  }

  /**
   * @brief Ball radius.
   *
   * @return Const reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() const noexcept -> value_type const& {
    return m_radius;
  }

  /**
   * @brief Mutable ball center.
   *
   * @return Reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() noexcept -> point_type& {
    return m_center;
  }

  /**
   * @brief Mutable ball radius.
   *
   * @return Reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() noexcept -> value_type& {
    return m_radius;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(sphere3 const&, sphere3 const&) noexcept = default;
};

using sphere3_f = sphere3<float>;
using sphere3_d = sphere3<double>;

static_assert(std::is_trivially_copyable_v<sphere3_f>);
static_assert(std::is_standard_layout_v<sphere3_f>);
static_assert(sizeof(sphere3_f) == 4 * sizeof(float));

/**
 * @brief Ball volume: \c (4/3) * pi * r^3.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 *
 * @return The volume of the ball.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto volume(sphere3<Real> const& s) noexcept -> Real {
  auto const r3{s.radius() * s.radius() * s.radius()};
  return (Real{4} / Real{3}) * nexenne::math::pi_v<Real> * r3;
}

/**
 * @brief Boundary area: \c 4 * pi * r^2.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 *
 * @return The surface area of the boundary.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto surface_area(sphere3<Real> const& s) noexcept -> Real {
  return Real{4} * nexenne::math::pi_v<Real> * s.radius() * s.radius();
}

/**
 * @brief Reports whether \p p is inside or on the boundary of \p s.
 *
 * Uses squared distance to avoid a square root.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param p Point.
 *
 * @return \c true when \c |p - s.center()| <= s.radius().
 *
 * @pre \c s.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
contains_point(sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& p) noexcept -> bool {
  return nexenne::math::length_squared(p - s.center()) <= s.radius() * s.radius();
}

/**
 * @brief Closest point on or inside the ball to \p p.
 *
 * Returns \p p when it is inside or on the boundary, otherwise projects radially
 * onto the boundary. When \p p coincides with the center the radial direction is
 * arbitrary, so the boundary point at \c center + (radius, 0, 0) is returned by
 * convention.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param p Query point.
 *
 * @return Closest point on or in the ball.
 *
 * @pre \c s.radius() is non-negative.
 * @post \c contains_point(s, result) is \c true (up to rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_point(
  sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& p
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const offset{p - s.center()};
  auto const len_sq{nexenne::math::length_squared(offset)};
  auto const r2{s.radius() * s.radius()};
  if (len_sq <= r2) {
    return p;
  }
  auto const len{nexenne::math::sqrt(len_sq)};
  return s.center() + offset * (s.radius() / len);
}

/**
 * @brief Closest point on the ball's boundary, even for interior points.
 *
 * When \p p coincides with the center the radial direction is arbitrary, so
 * \c center + (radius, 0, 0) is returned by convention.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param p Query point.
 *
 * @return Point on the boundary closest to \p p.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result lies on the boundary: \c |result - s.center()| equals
 *       \c s.radius() (up to rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto closest_point_on_boundary(
  sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& p
) noexcept -> nexenne::math::vector<Real, 3> {
  auto const offset{p - s.center()};
  auto const len_sq{nexenne::math::length_squared(offset)};
  if (len_sq <= static_cast<Real>(1e-20)) {
    return s.center() + nexenne::math::vector<Real, 3>{s.radius(), Real{0}, Real{0}};
  }
  auto const len{nexenne::math::sqrt(len_sq)};
  return s.center() + offset * (s.radius() / len);
}

/**
 * @brief Euclidean distance from \p p to the ball.
 *
 * Returns \c 0 when \p p is inside or on the boundary.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param p Query point.
 *
 * @return The distance from \p p to the ball.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result is non-negative; \c 0 when \p p is inside or on \p s.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
distance(sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& p) noexcept -> Real {
  auto const len{nexenne::math::length(p - s.center())};
  auto const d{len - s.radius()};
  return d < Real{0} ? Real{0} : d;
}

/**
 * @brief Squared distance from \p p to the ball.
 *
 * Squares \c distance(s, p). The square root is still needed for the distance to
 * the boundary, so this saves only the final multiply over computing the
 * distance and squaring it at the call site.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param p Query point.
 *
 * @return The squared distance.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result is non-negative; \c 0 when \p p is inside or on \p s.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
distance_squared(sphere3<Real> const& s, nexenne::math::vector<Real, 3> const& p) noexcept -> Real {
  auto const d{distance(s, p)};
  return d * d;
}

/**
 * @brief Reports whether two balls overlap or touch.
 *
 * Two balls intersect when the distance between their centers is at most the sum
 * of their radii. Boundary touching counts. Uses squared distance to avoid a
 * square root.
 *
 * @tparam Real Component type.
 * @param a First sphere.
 * @param b Second sphere.
 *
 * @return \c true when the balls overlap or touch.
 *
 * @pre \c a.radius() and \c b.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(sphere3<Real> const& a, sphere3<Real> const& b) noexcept -> bool {
  auto const sum_r{a.radius() + b.radius()};
  return nexenne::math::length_squared(a.center() - b.center()) <= sum_r * sum_r;
}

/**
 * @brief Smallest axis-aligned box around the ball.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 *
 * @return Tight box of \p s.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result is well-formed and contains \p s.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto bounding_aabb(sphere3<Real> const& s) noexcept -> aabb<Real, 3> {
  auto const r{nexenne::math::vector<Real, 3>{s.radius(), s.radius(), s.radius()}};
  return aabb<Real, 3>{s.center() - r, s.center() + r};
}

}  // namespace nexenne::geometry
