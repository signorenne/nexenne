#pragma once

/**
 * @file
 * @brief 2D disc (circle2): center plus radius, with its metric queries.
 *
 * The circle here is the closed disc, not the 1D boundary curve: \c area and
 * \c circumference are the disc measures, and the containment and closest-point
 * queries are the 2D analogues of \c sphere3. Everything is \c constexpr and
 * \c noexcept; nothing allocates.
 *
 * Aliases: \c circle2, \c circle2_f (float), \c circle2_d (double).
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
 * @brief Closed 2D disc stored as a center point and a radius.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class circle2 {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, 2>;

private:
  point_type m_center{};
  value_type m_radius{};

public:
  /// @brief Constructs the zero-radius disc at the origin.
  constexpr circle2() noexcept = default;

  /**
   * @brief Constructs a disc from a center and radius.
   *
   * @param center Disc center.
   * @param radius Disc radius (expected non-negative).
   */
  constexpr circle2(point_type center, value_type const radius) noexcept
      : m_center{center}, m_radius{radius} {}

  /**
   * @brief Disc center.
   *
   * @return Const reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() const noexcept -> point_type const& {
    return m_center;
  }

  /**
   * @brief Disc radius.
   *
   * @return Const reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() const noexcept -> value_type const& {
    return m_radius;
  }

  /**
   * @brief Mutable disc center.
   *
   * @return Reference to the stored center.
   */
  [[nodiscard]] constexpr auto center() noexcept -> point_type& {
    return m_center;
  }

  /**
   * @brief Mutable disc radius.
   *
   * @return Reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() noexcept -> value_type& {
    return m_radius;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(circle2 const&, circle2 const&) noexcept = default;
};

using circle2_f = circle2<float>;
using circle2_d = circle2<double>;

static_assert(std::is_trivially_copyable_v<circle2_f>);
static_assert(std::is_standard_layout_v<circle2_f>);
static_assert(sizeof(circle2_f) == 3 * sizeof(float));

/**
 * @brief Disc area: \c pi * r^2.
 *
 * @tparam Real Component type.
 * @param c Circle.
 *
 * @return The area enclosed by the circle.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(circle2<Real> const& c) noexcept -> Real {
  return nexenne::math::pi_v<Real> * c.radius() * c.radius();
}

/**
 * @brief Boundary length: \c 2 * pi * r.
 *
 * @tparam Real Component type.
 * @param c Circle.
 *
 * @return The circumference of the circle.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto circumference(circle2<Real> const& c) noexcept -> Real {
  return nexenne::math::tau_v<Real> * c.radius();
}

/**
 * @brief Reports whether \p p is inside or on the boundary of \p c.
 *
 * Uses squared distance to avoid a square root.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param p Point.
 *
 * @return \c true when \c |p - c.center()| <= c.radius().
 *
 * @pre \c c.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
contains_point(circle2<Real> const& c, nexenne::math::vector<Real, 2> const& p) noexcept -> bool {
  return nexenne::math::length_squared(p - c.center()) <= c.radius() * c.radius();
}

/**
 * @brief Closest point on or inside the disc to \p p.
 *
 * Returns \p p when it is inside or on the boundary, otherwise projects radially
 * onto the boundary. When \p p coincides with the center the radial direction is
 * arbitrary, so the boundary point at \c center + (radius, 0) is returned by
 * convention.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param p Query point.
 *
 * @return Closest point on or in the disc.
 *
 * @pre \c c.radius() is non-negative.
 * @post \c contains_point(c, result) is \c true (up to rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
closest_point(circle2<Real> const& c, nexenne::math::vector<Real, 2> const& p) noexcept
  -> nexenne::math::vector<Real, 2> {
  auto const offset{p - c.center()};
  auto const len_sq{nexenne::math::length_squared(offset)};
  auto const r2{c.radius() * c.radius()};
  if (len_sq <= r2) {
    return p;
  }
  auto const len{nexenne::math::sqrt(len_sq)};
  return c.center() + offset * (c.radius() / len);
}

/**
 * @brief Closest point on the circle's boundary, even for interior points.
 *
 * When \p p coincides with the center the radial direction is arbitrary, so
 * \c center + (radius, 0) is returned by convention.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param p Query point.
 *
 * @return Point on the perimeter closest to \p p.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result lies on the boundary: \c |result - c.center()| equals
 *       \c c.radius() (up to rounding).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
closest_point_on_boundary(circle2<Real> const& c, nexenne::math::vector<Real, 2> const& p) noexcept
  -> nexenne::math::vector<Real, 2> {
  auto const offset{p - c.center()};
  auto const len_sq{nexenne::math::length_squared(offset)};
  if (len_sq <= static_cast<Real>(1e-20)) {
    return c.center() + nexenne::math::vector<Real, 2>{c.radius(), Real{0}};
  }
  auto const len{nexenne::math::sqrt(len_sq)};
  return c.center() + offset * (c.radius() / len);
}

/**
 * @brief Euclidean distance from \p p to the disc.
 *
 * Returns \c 0 when \p p is inside or on the boundary.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param p Query point.
 *
 * @return The distance from \p p to the disc.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative; \c 0 when \p p is inside or on \p c.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
distance(circle2<Real> const& c, nexenne::math::vector<Real, 2> const& p) noexcept -> Real {
  auto const len{nexenne::math::length(p - c.center())};
  auto const d{len - c.radius()};
  return d < Real{0} ? Real{0} : d;
}

/**
 * @brief Squared distance from \p p to the disc.
 *
 * Squares \c distance(c, p); the square root is still needed for the distance to
 * the boundary.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param p Query point.
 *
 * @return The squared distance.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative; \c 0 when \p p is inside or on \p c.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
distance_squared(circle2<Real> const& c, nexenne::math::vector<Real, 2> const& p) noexcept -> Real {
  auto const d{distance(c, p)};
  return d * d;
}

/**
 * @brief Reports whether two discs overlap or touch.
 *
 * Two discs intersect when the distance between their centers is at most the sum
 * of their radii. Boundary touching counts. Uses squared distance to avoid a
 * square root.
 *
 * @tparam Real Component type.
 * @param a First circle.
 * @param b Second circle.
 *
 * @return \c true when the discs overlap or touch.
 *
 * @pre \c a.radius() and \c b.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(circle2<Real> const& a, circle2<Real> const& b) noexcept
  -> bool {
  auto const sum_r{a.radius() + b.radius()};
  return nexenne::math::length_squared(a.center() - b.center()) <= sum_r * sum_r;
}

/**
 * @brief Smallest axis-aligned box around the circle.
 *
 * @tparam Real Component type.
 * @param c Circle.
 *
 * @return Tight box of \p c.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is well-formed and contains \p c.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto bounding_aabb(circle2<Real> const& c) noexcept -> aabb<Real, 2> {
  auto const r{nexenne::math::vector<Real, 2>{c.radius(), c.radius()}};
  return aabb<Real, 2>{c.center() - r, c.center() + r};
}

}  // namespace nexenne::geometry
