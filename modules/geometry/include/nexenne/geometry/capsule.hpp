#pragma once

/**
 * @file
 * @brief Capsule (capsule2 / capsule3): the points within a radius of a segment.
 *
 * A capsule is the set of points within \c radius of a central segment: a
 * stadium (rectangle with two semicircle caps) in 2D, a pill (cylinder with two
 * hemisphere caps) in 3D. It has no sharp corners and a cheap closest-point
 * query against its spine. Templated on \p N so one type covers both dimensions;
 * everything is \c constexpr and \c noexcept and nothing allocates.
 *
 * Aliases: \c capsule2 / \c capsule3, each with \c _f (float) and \c _d (double).
 */

#include <concepts>
#include <cstddef>
#include <type_traits>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Capsule stored as a central segment (start, end) plus a radius.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
class capsule {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, N>;

private:
  point_type m_start{};
  point_type m_end{};
  value_type m_radius{};

public:
  /**
   * @brief Constructs the zero-radius capsule with both endpoints at the origin.
   */
  constexpr capsule() noexcept = default;

  /**
   * @brief Constructs a capsule from its spine endpoints and radius.
   *
   * @param start Spine start point.
   * @param end Spine end point.
   * @param radius Capsule radius (expected non-negative).
   */
  constexpr capsule(point_type start, point_type end, value_type const radius) noexcept
      : m_start{start}, m_end{end}, m_radius{radius} {}

  /**
   * @brief Spine start point.
   *
   * @return Const reference to the stored start point.
   */
  [[nodiscard]] constexpr auto start() const noexcept -> point_type const& {
    return m_start;
  }

  /**
   * @brief Spine end point.
   *
   * @return Const reference to the stored end point.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> point_type const& {
    return m_end;
  }

  /**
   * @brief Capsule radius.
   *
   * @return Const reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() const noexcept -> value_type const& {
    return m_radius;
  }

  /**
   * @brief Mutable spine start point.
   *
   * @return Reference to the stored start point.
   */
  [[nodiscard]] constexpr auto start() noexcept -> point_type& {
    return m_start;
  }

  /**
   * @brief Mutable spine end point.
   *
   * @return Reference to the stored end point.
   */
  [[nodiscard]] constexpr auto end() noexcept -> point_type& {
    return m_end;
  }

  /**
   * @brief Mutable capsule radius.
   *
   * @return Reference to the stored radius.
   */
  [[nodiscard]] constexpr auto radius() noexcept -> value_type& {
    return m_radius;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(capsule const&, capsule const&) noexcept = default;
};

template <std::floating_point Real>
using capsule2 = capsule<Real, 2>;
template <std::floating_point Real>
using capsule3 = capsule<Real, 3>;

using capsule2_f = capsule2<float>;
using capsule2_d = capsule2<double>;
using capsule3_f = capsule3<float>;
using capsule3_d = capsule3<double>;

static_assert(std::is_trivially_copyable_v<capsule2_f>);
static_assert(std::is_standard_layout_v<capsule2_f>);
static_assert(sizeof(capsule2_f) == 5 * sizeof(float));
static_assert(sizeof(capsule3_f) == 7 * sizeof(float));

/**
 * @brief The capsule's central segment (its spine).
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule.
 *
 * @return The spine as a \c segment from \c start to \c end.
 *
 * @pre None.
 * @post The result's endpoints equal \c c.start() and \c c.end().
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto axis(capsule<Real, N> const& c) noexcept -> segment<Real, N> {
  return segment<Real, N>{c.start(), c.end()};
}

/**
 * @brief Length of the central segment, excluding the caps.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule.
 *
 * @return The distance from \c start to \c end.
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto length(capsule<Real, N> const& c) noexcept -> Real {
  return nexenne::math::length(c.end() - c.start());
}

/**
 * @brief Total area of a 2D stadium capsule.
 *
 * The rectangle \c 2*r*L plus the two semicircle caps \c pi*r^2.
 *
 * @tparam Real Component type.
 * @param c 2D capsule.
 *
 * @return The total area.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(capsule<Real, 2> const& c) noexcept -> Real {
  auto const l{length(c)};
  auto const r{c.radius()};
  return Real{2} * r * l + nexenne::math::pi_v<Real> * r * r;
}

/**
 * @brief Total volume of a 3D capsule (cylinder plus sphere).
 *
 * The cylinder \c pi*r^2*L plus the two hemisphere caps \c (4/3)*pi*r^3.
 *
 * @tparam Real Component type.
 * @param c 3D capsule.
 *
 * @return The total volume.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto volume(capsule<Real, 3> const& c) noexcept -> Real {
  auto const l{length(c)};
  auto const r{c.radius()};
  auto const cylinder{nexenne::math::pi_v<Real> * r * r * l};
  auto const sphere{(Real{4} / Real{3}) * nexenne::math::pi_v<Real> * r * r * r};
  return cylinder + sphere;
}

/**
 * @brief Reports whether \p p is inside or on the boundary of the capsule.
 *
 * Equivalent to the distance from \p p to the spine being at most \c radius;
 * uses squared distance to avoid a square root.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule.
 * @param p Query point.
 *
 * @return \c true when \p p is inside the capsule.
 *
 * @pre \c c.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto contains_point(
  capsule<Real, N> const& c, nexenne::math::vector<Real, N> const& p
) noexcept -> bool {
  return distance_squared(axis(c), p) <= c.radius() * c.radius();
}

/**
 * @brief Closest point on or inside the capsule to \p p.
 *
 * Returns \p p when it is inside. Otherwise projects onto the spine, then pushes
 * outward by \c radius along the offset direction.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule.
 * @param p Query point.
 *
 * @return Closest point on or in the capsule.
 *
 * @pre \c c.radius() is positive.
 * @post \c contains_point(c, result) is \c true (up to rounding).
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto closest_point(
  capsule<Real, N> const& c, nexenne::math::vector<Real, N> const& p
) noexcept -> nexenne::math::vector<Real, N> {
  auto const on_axis{closest_point(axis(c), p)};
  auto const offset{p - on_axis};
  auto const len_sq{nexenne::math::length_squared(offset)};
  auto const r2{c.radius() * c.radius()};
  if (len_sq <= r2) {
    return p;
  }
  auto const len{nexenne::math::sqrt(len_sq)};
  return on_axis + offset * (c.radius() / len);
}

/**
 * @brief Smallest axis-aligned box around the capsule.
 *
 * The union of the two end-cap boxes: the spine's per-axis min and max, each
 * grown by \c radius.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param c Capsule.
 *
 * @return The tight axis-aligned bound of \p c.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result is well-formed and contains \p c.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto bounding_aabb(capsule<Real, N> const& c) noexcept -> aabb<Real, N> {
  auto r{nexenne::math::vector<Real, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    r[i] = c.radius();
  }
  return aabb<Real, N>{
    nexenne::math::component_min(c.start(), c.end()) - r,
    nexenne::math::component_max(c.start(), c.end()) + r
  };
}

}  // namespace nexenne::geometry
