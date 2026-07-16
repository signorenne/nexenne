#pragma once

/**
 * @file
 * @brief Half-line (ray) defined by an origin and a unit direction.
 *
 * A \c ray<Real, N> is the set of points \c origin + t * direction for
 * \c t >= 0. The direction is expected to be unit length: \c ray_from_points
 * normalizes for you, and the queries assume the precondition holds. Templated
 * on \p N so one type covers 2D and 3D. Everything is \c constexpr and
 * \c noexcept.
 *
 * Aliases: \c ray2 / \c ray3, each with \c _f (float) and \c _d (double).
 */

#include <concepts>
#include <cstddef>
#include <expected>
#include <type_traits>

#include <nexenne/geometry/error.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Half-line stored as an origin and a unit direction.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
class ray {
public:
  using value_type = Real;
  using vector_type = nexenne::math::vector<value_type, N>;

private:
  vector_type m_origin{};
  vector_type m_direction{};

public:
  /// @brief Constructs a ray at the origin with a zero direction.
  constexpr ray() noexcept = default;

  /**
   * @brief Constructs a ray from an origin and direction.
   *
   * No normalization is performed; the caller passes a unit direction (see
   * \c ray_from_points for a checked, normalizing factory).
   *
   * @param origin Ray origin.
   * @param direction Ray direction, expected unit length.
   */
  constexpr ray(vector_type origin, vector_type direction) noexcept
      : m_origin{origin}, m_direction{direction} {}

  /**
   * @brief Ray origin.
   *
   * @return Const reference to the stored origin.
   */
  [[nodiscard]] constexpr auto origin() const noexcept -> vector_type const& {
    return m_origin;
  }

  /**
   * @brief Ray direction.
   *
   * @return Const reference to the stored direction.
   */
  [[nodiscard]] constexpr auto direction() const noexcept -> vector_type const& {
    return m_direction;
  }

  /**
   * @brief Mutable ray origin.
   *
   * @return Reference to the stored origin.
   */
  [[nodiscard]] constexpr auto origin() noexcept -> vector_type& {
    return m_origin;
  }

  /**
   * @brief Mutable ray direction.
   *
   * @return Reference to the stored direction.
   */
  [[nodiscard]] constexpr auto direction() noexcept -> vector_type& {
    return m_direction;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto operator<=>(ray const&, ray const&) noexcept = default;
};

template <std::floating_point Real>
using ray2 = ray<Real, 2>;
template <std::floating_point Real>
using ray3 = ray<Real, 3>;

using ray2_f = ray2<float>;
using ray2_d = ray2<double>;
using ray3_f = ray3<float>;
using ray3_d = ray3<double>;

static_assert(std::is_trivially_copyable_v<ray2_f>);
static_assert(std::is_standard_layout_v<ray2_f>);
static_assert(sizeof(ray2_f) == 4 * sizeof(float));
static_assert(sizeof(ray3_f) == 6 * sizeof(float));

/**
 * @brief Constructs a ray from an origin toward a target point.
 *
 * The direction is the unit vector from \p origin to \p target.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param origin Ray origin.
 * @param target Point the ray points toward.
 *
 * @return The ray on success, or \c geometry_error::degenerate_primitive when
 *         \p origin and \p target coincide.
 *
 * @pre None. Coincident points are detected and reported.
 * @post On success the ray has a unit-length direction and its origin equals
 *       \p origin.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto ray_from_points(
  nexenne::math::vector<Real, N> const& origin, nexenne::math::vector<Real, N> const& target
) noexcept -> result<ray<Real, N>> {
  auto const dir{nexenne::math::normalize(target - origin)};
  if (!dir) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  return ray<Real, N>{origin, *dir};
}

/**
 * @brief Point on the ray at parameter \p t.
 *
 * For a unit direction \p t is the distance from the origin. A negative \p t
 * lies behind the origin, off the half-line.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param r Ray.
 * @param t Distance parameter.
 *
 * @return \c r.origin() + r.direction() * t.
 *
 * @pre \c r.direction() has unit length.
 * @post The result lies on the ray's line; on the half-line when \p t >= 0.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto at(ray<Real, N> const& r, Real const t) noexcept
  -> nexenne::math::vector<Real, N> {
  return r.origin() + r.direction() * t;
}

/**
 * @brief Closest point on the ray to \p p.
 *
 * Projects \p p onto the ray's line and clamps the parameter to the non-negative
 * half. A point behind the origin returns the origin itself.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param r Ray with unit direction.
 * @param p Query point.
 *
 * @return Closest point on the ray.
 *
 * @pre \c r.direction() has unit length.
 * @post The result lies on the half-line (parameter \c t >= 0).
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
closest_point(ray<Real, N> const& r, nexenne::math::vector<Real, N> const& p) noexcept
  -> nexenne::math::vector<Real, N> {
  auto const offset{p - r.origin()};
  auto const t{nexenne::math::dot(offset, r.direction())};
  if (t <= Real{0}) {
    return r.origin();
  }
  return r.origin() + r.direction() * t;
}

/**
 * @brief Squared distance from \p p to the ray.
 *
 * Returns \c 0 when \p p lies on the ray. Square-root free.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param r Ray with unit direction.
 * @param p Query point.
 *
 * @return The squared distance.
 *
 * @pre \c r.direction() has unit length.
 * @post The result is non-negative; \c 0 when \p p lies on the ray.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance_squared(ray<Real, N> const& r, nexenne::math::vector<Real, N> const& p) noexcept -> Real {
  return nexenne::math::length_squared(p - closest_point(r, p));
}

/**
 * @brief Euclidean distance from \p p to the ray.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param r Ray with unit direction.
 * @param p Query point.
 *
 * @return The distance.
 *
 * @pre \c r.direction() has unit length.
 * @post The result is non-negative; \c 0 when \p p lies on the ray.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance(ray<Real, N> const& r, nexenne::math::vector<Real, N> const& p) noexcept -> Real {
  return nexenne::math::sqrt(distance_squared(r, p));
}

}  // namespace nexenne::geometry
