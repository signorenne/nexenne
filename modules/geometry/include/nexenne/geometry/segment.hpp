#pragma once

/**
 * @file
 * @brief Finite line segment (segment) defined by two endpoints.
 *
 * A \c segment<Real, N> is the set of points \c start + t * (end - start) for
 * \c t in \c [0, 1]. Templated on \p N so one type covers 2D and 3D. Everything
 * is \c constexpr and \c noexcept.
 *
 * A 2D segment additionally offers a square-root-free \c intersects that returns
 * the crossing point as a \c std::optional. The 3D skew-line closest-pair query
 * is intentionally left to the cross-type \c intersect.hpp header.
 *
 * Aliases: \c segment2 / \c segment3, each with \c _f (float) and \c _d
 * (double).
 */

#include <concepts>
#include <cstddef>
#include <optional>
#include <type_traits>

#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Finite line segment stored as two endpoints.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
class segment {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, N>;

private:
  point_type m_start{};
  point_type m_end{};

public:
  /// @brief Constructs a zero-length segment at the origin.
  constexpr segment() noexcept = default;

  /**
   * @brief Constructs a segment from two endpoints.
   *
   * @param start First endpoint (parameter 0).
   * @param end Second endpoint (parameter 1).
   */
  constexpr segment(point_type start, point_type end) noexcept : m_start{start}, m_end{end} {}

  /**
   * @brief First endpoint.
   *
   * @return Const reference to the stored start point.
   */
  [[nodiscard]] constexpr auto start() const noexcept -> point_type const& {
    return m_start;
  }

  /**
   * @brief Second endpoint.
   *
   * @return Const reference to the stored end point.
   */
  [[nodiscard]] constexpr auto end() const noexcept -> point_type const& {
    return m_end;
  }

  /**
   * @brief Mutable first endpoint.
   *
   * @return Reference to the stored start point.
   */
  [[nodiscard]] constexpr auto start() noexcept -> point_type& {
    return m_start;
  }

  /**
   * @brief Mutable second endpoint.
   *
   * @return Reference to the stored end point.
   */
  [[nodiscard]] constexpr auto end() noexcept -> point_type& {
    return m_end;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(segment const&, segment const&) noexcept = default;
};

template <std::floating_point Real>
using segment2 = segment<Real, 2>;
template <std::floating_point Real>
using segment3 = segment<Real, 3>;

using segment2_f = segment2<float>;
using segment2_d = segment2<double>;
using segment3_f = segment3<float>;
using segment3_d = segment3<double>;

static_assert(std::is_trivially_copyable_v<segment2_f>);
static_assert(std::is_standard_layout_v<segment2_f>);
static_assert(sizeof(segment2_f) == 4 * sizeof(float));
static_assert(sizeof(segment3_f) == 6 * sizeof(float));

/**
 * @brief Non-normalized direction vector \c end - start.
 *
 * Its magnitude is the segment's length; pass it through
 * \c nexenne::math::normalize for a unit direction.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 *
 * @return \c end - start.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto direction(segment<Real, N> const& s
) noexcept -> nexenne::math::vector<Real, N> {
  return s.end() - s.start();
}

/**
 * @brief Squared length of the segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 *
 * @return \c length_squared(end - start).
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto length_squared(segment<Real, N> const& s) noexcept -> Real {
  return nexenne::math::length_squared(direction(s));
}

/**
 * @brief Euclidean length of the segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 *
 * @return \c length(end - start).
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto length(segment<Real, N> const& s) noexcept -> Real {
  return nexenne::math::sqrt(length_squared(s));
}

/**
 * @brief Point on the segment at parameter \p t.
 *
 * \c start at \c t == 0, \c end at \c t == 1, a linear blend between. A \p t
 * outside \c [0, 1] extrapolates past the endpoints.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 * @param t Parameter.
 *
 * @return Point on the segment at parameter \p t.
 *
 * @pre None.
 * @post The result lies on the segment when \p t is in \c [0, 1].
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
at(segment<Real, N> const& s, Real const t) noexcept -> nexenne::math::vector<Real, N> {
  return s.start() + direction(s) * t;
}

/**
 * @brief Closest point on the segment to \p p.
 *
 * Projects \p p onto the line through the endpoints and clamps the parameter to
 * \c [0, 1]. A zero-length segment returns \c s.start().
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 * @param p Query point.
 *
 * @return Closest point on the segment.
 *
 * @pre None. Zero-length segments are handled.
 * @post The result lies on the segment (parameter in \c [0, 1]).
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto closest_point(
  segment<Real, N> const& s, nexenne::math::vector<Real, N> const& p
) noexcept -> nexenne::math::vector<Real, N> {
  auto const dir{direction(s)};
  auto const len_sq{nexenne::math::length_squared(dir)};
  if (len_sq <= static_cast<Real>(1e-20)) {
    return s.start();
  }
  auto const t{nexenne::math::dot(p - s.start(), dir) / len_sq};
  auto const t_clamped{nexenne::math::clamp(t, Real{0}, Real{1})};
  return s.start() + dir * t_clamped;
}

/**
 * @brief Squared distance from \p p to the segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 * @param p Query point.
 *
 * @return The squared distance.
 *
 * @pre None.
 * @post The result is non-negative; \c 0 when \p p lies on the segment.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto distance_squared(
  segment<Real, N> const& s, nexenne::math::vector<Real, N> const& p
) noexcept -> Real {
  return nexenne::math::length_squared(p - closest_point(s, p));
}

/**
 * @brief Euclidean distance from \p p to the segment.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param s Segment.
 * @param p Query point.
 *
 * @return The distance.
 *
 * @pre None.
 * @post The result is non-negative; \c 0 when \p p lies on the segment.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance(segment<Real, N> const& s, nexenne::math::vector<Real, N> const& p) noexcept -> Real {
  return nexenne::math::sqrt(distance_squared(s, p));
}

/**
 * @brief Intersection point of two 2D segments, if any.
 *
 * Solves for the parameters \c t and \c u along the two segments with the 2D
 * pseudo-cross, returning the point when both land in \c [0, 1]. Parallel and
 * collinear inputs return \c std::nullopt even when they overlap: that case is
 * rarely the intended answer, so check parallelism explicitly if you need it.
 *
 * @tparam Real Component type.
 * @param a First segment.
 * @param b Second segment.
 *
 * @return The crossing point, or \c std::nullopt when the segments do not cross
 *         (including parallel or collinear).
 *
 * @pre None.
 * @post When engaged the result lies on both \p a and \p b.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(
  segment<Real, 2> const& a, segment<Real, 2> const& b
) noexcept -> std::optional<nexenne::math::vector<Real, 2>> {
  // Solve p + t r = q + u s for the two parameters with the 2D pseudo-cross:
  // t = cross(qp, s) / cross(r, s), u = cross(qp, r) / cross(r, s), where qp is
  // q - p. A zero cross(r, s) means the segments are parallel or collinear. See
  // Ericson, RTCD section 5.1.9.1, or Gareth Rees's standard derivation.
  auto const r{direction(a)};
  auto const s{direction(b)};
  auto const rxs{nexenne::math::cross(r, s)};
  if (rxs == Real{0}) {
    return std::nullopt;  // parallel or collinear
  }
  auto const qp{b.start() - a.start()};
  auto const t{nexenne::math::cross(qp, s) / rxs};
  auto const u{nexenne::math::cross(qp, r) / rxs};
  if (t < Real{0} || t > Real{1} || u < Real{0} || u > Real{1}) {
    return std::nullopt;
  }
  return a.start() + r * t;
}

}  // namespace nexenne::geometry
