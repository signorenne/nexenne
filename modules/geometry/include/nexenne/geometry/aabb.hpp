#pragma once

/**
 * @file
 * @brief Axis-aligned bounding box (2D and 3D) and its queries.
 *
 * Stored as a (min, max) corner pair. This min/max form is what every collision
 * and broad-phase routine wants: containment is two component-wise comparisons,
 * the intersection of two boxes is one component-wise \c max and one \c min, and
 * ray-box uses the slab method directly on the stored corners. The center and
 * half-size forms are recovered cheaply on demand.
 *
 * Everything here is \c constexpr and \c noexcept: an \c aabb is a trivially
 * copyable value and none of the queries allocate. The per-axis loops are
 * fixed-trip over \p N, so the compiler unrolls them and vectorizes the
 * component-wise math through \c nexenne::math (see vector_algorithms.hpp).
 *
 * Aliases: \c aabb2 / \c aabb3, each with \c _f (float), \c _d (double), and
 * \c _i (int) variants.
 */

#include <concepts>
#include <cstddef>
#include <limits>
#include <type_traits>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Axis-aligned bounding box stored as a (min, max) corner pair.
 *
 * @tparam Value Component type (any \c nexenne::math::arithmetic type).
 * @tparam N Dimension.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
class aabb {
public:
  using value_type = Value;
  using point_type = nexenne::math::vector<value_type, N>;

private:
  point_type m_min{};
  point_type m_max{};

public:
  /**
   * @brief Constructs the degenerate box at the origin (min == max == 0).
   */
  constexpr aabb() noexcept = default;

  /**
   * @brief Constructs a box from explicit corners.
   *
   * No validation is performed; the caller ensures \c min[i] <= max[i] on every
   * axis (see \c make_aabb_from_points for an order-independent constructor).
   *
   * @param min Lower corner.
   * @param max Upper corner.
   */
  constexpr aabb(point_type min, point_type max) noexcept : m_min{min}, m_max{max} {}

  /**
   * @brief Lower corner.
   *
   * @return Const reference to the stored minimum corner.
   */
  [[nodiscard]] constexpr auto min() const noexcept -> point_type const& {
    return m_min;
  }

  /**
   * @brief Upper corner.
   *
   * @return Const reference to the stored maximum corner.
   */
  [[nodiscard]] constexpr auto max() const noexcept -> point_type const& {
    return m_max;
  }

  /**
   * @brief Mutable lower corner.
   *
   * @return Reference to the stored minimum corner.
   */
  [[nodiscard]] constexpr auto min() noexcept -> point_type& {
    return m_min;
  }

  /**
   * @brief Mutable upper corner.
   *
   * @return Reference to the stored maximum corner.
   */
  [[nodiscard]] constexpr auto max() noexcept -> point_type& {
    return m_max;
  }

  /**
   * @brief Component-wise equality and ordering, defaulted.
   */
  [[nodiscard]] friend constexpr auto operator<=>(aabb const&, aabb const&) noexcept = default;
};

template <nexenne::math::arithmetic Value>
using aabb2 = aabb<Value, 2>;
template <nexenne::math::arithmetic Value>
using aabb3 = aabb<Value, 3>;

using aabb2_f = aabb2<float>;
using aabb2_d = aabb2<double>;
using aabb2_i = aabb2<int>;
using aabb3_f = aabb3<float>;
using aabb3_d = aabb3<double>;
using aabb3_i = aabb3<int>;

static_assert(std::is_trivially_copyable_v<aabb2_f>);
static_assert(std::is_standard_layout_v<aabb2_f>);
static_assert(sizeof(aabb2_f) == 4 * sizeof(float));
static_assert(std::is_trivially_copyable_v<aabb3_f>);
static_assert(sizeof(aabb3_f) == 6 * sizeof(float));

/**
 * @brief Constructs a box from explicit min/max corners.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param min Lower corner.
 * @param max Upper corner.
 *
 * @return The box spanning the two corners.
 *
 * @pre \c min[i] <= max[i] on every axis.
 * @post \c result.min() equals \p min and \c result.max() equals \p max.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto make_aabb_from_corners(
  nexenne::math::vector<Value, N> const& min, nexenne::math::vector<Value, N> const& max
) noexcept -> aabb<Value, N> {
  return aabb<Value, N>{min, max};
}

/**
 * @brief Constructs a box from a center point and a half-size.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param center Box center.
 * @param half Half-extent along each axis.
 *
 * @return The box spanning \c [center - half, center + half].
 *
 * @pre Every component of \p half is non-negative.
 * @post The result is well-formed (\c min[i] <= max[i]) with center \p center.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto make_aabb_from_center_half_size(
  nexenne::math::vector<Value, N> const& center, nexenne::math::vector<Value, N> const& half
) noexcept -> aabb<Value, N> {
  return aabb<Value, N>{center - half, center + half};
}

/**
 * @brief Constructs the smallest box containing two points, in any order.
 *
 * Both points are folded into the corners component-wise, so the argument order
 * does not matter.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param a First point.
 * @param b Second point.
 *
 * @return Smallest box containing both points.
 *
 * @pre None.
 * @post The result is well-formed and contains both \p a and \p b.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto make_aabb_from_points(
  nexenne::math::vector<Value, N> const& a, nexenne::math::vector<Value, N> const& b
) noexcept -> aabb<Value, N> {
  return aabb<Value, N>{nexenne::math::component_min(a, b), nexenne::math::component_max(a, b)};
}

/**
 * @brief Inverted box suitable as the seed for a fold over a point set.
 *
 * \c min is set to the largest finite value of \p Value and \c max to its
 * negative, so the first \c expand_to_include yields a tight box around exactly
 * the points added. \c empty() is \c true until a point is added.
 *
 * @tparam Value Floating-point component type.
 * @tparam N Dimension.
 *
 * @return Inverted box whose intersection with anything is empty.
 *
 * @pre None.
 * @post \c empty() returns \c true for the result.
 */
template <std::floating_point Value, std::size_t N>
[[nodiscard]] constexpr auto empty_aabb() noexcept -> aabb<Value, N> {
  auto const big{std::numeric_limits<Value>::max()};
  auto lo{nexenne::math::vector<Value, N>{}};
  auto hi{nexenne::math::vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    lo[i] = big;
    hi[i] = -big;
  }
  return aabb<Value, N>{lo, hi};
}

/**
 * @brief Center of the box: \c (min + max) / 2.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 * @param box Input box.
 *
 * @return The center point.
 *
 * @pre \p box is well-formed (\c min <= max component-wise).
 * @post The result lies between \c box.min() and \c box.max() on every axis.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto center(aabb<Real, N> const& box) noexcept
  -> nexenne::math::vector<Real, N> {
  return (box.min() + box.max()) * Real{0.5};
}

/**
 * @brief Full size along each axis: \c max - min.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param box Input box.
 *
 * @return Per-axis extent.
 *
 * @pre None.
 * @post The result is non-negative on every axis when \p box is well-formed.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto size(aabb<Value, N> const& box) noexcept
  -> nexenne::math::vector<Value, N> {
  return box.max() - box.min();
}

/**
 * @brief Half of the size along each axis: \c (max - min) / 2.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 * @param box Input box.
 *
 * @return Per-axis half-extent.
 *
 * @pre None.
 * @post The result is non-negative on every axis when \p box is well-formed.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto half_size(aabb<Real, N> const& box) noexcept
  -> nexenne::math::vector<Real, N> {
  return (box.max() - box.min()) * Real{0.5};
}

/**
 * @brief Area of a 2D box.
 *
 * @tparam Value Component type.
 * @param box 2D box.
 *
 * @return Width times height.
 *
 * @pre None.
 * @post The result is non-negative when \p box is well-formed.
 */
template <nexenne::math::arithmetic Value>
[[nodiscard]] constexpr auto area(aabb<Value, 2> const& box) noexcept -> Value {
  auto const s{size(box)};
  return s.x() * s.y();
}

/**
 * @brief Perimeter of a 2D box: \c 2 * (width + height).
 *
 * @tparam Value Component type.
 * @param box 2D box.
 *
 * @return The perimeter.
 *
 * @pre None.
 * @post The result is non-negative when \p box is well-formed.
 */
template <nexenne::math::arithmetic Value>
[[nodiscard]] constexpr auto perimeter(aabb<Value, 2> const& box) noexcept -> Value {
  auto const s{size(box)};
  return Value{2} * (s.x() + s.y());
}

/**
 * @brief Volume of a 3D box: \c width * height * depth.
 *
 * @tparam Value Component type.
 * @param box 3D box.
 *
 * @return The volume.
 *
 * @pre None.
 * @post The result is non-negative when \p box is well-formed.
 */
template <nexenne::math::arithmetic Value>
[[nodiscard]] constexpr auto volume(aabb<Value, 3> const& box) noexcept -> Value {
  auto const s{size(box)};
  return s.x() * s.y() * s.z();
}

/**
 * @brief Surface area of a 3D box: \c 2 * (wh + hd + wd).
 *
 * @tparam Value Component type.
 * @param box 3D box.
 *
 * @return The surface area.
 *
 * @pre None.
 * @post The result is non-negative when \p box is well-formed.
 */
template <nexenne::math::arithmetic Value>
[[nodiscard]] constexpr auto surface_area(aabb<Value, 3> const& box) noexcept -> Value {
  auto const s{size(box)};
  return Value{2} * (s.x() * s.y() + s.y() * s.z() + s.x() * s.z());
}

/**
 * @brief Reports whether the box has zero or negative size on any axis.
 *
 * Equivalent to "is this not a valid region?". A box from \c empty_aabb stays
 * empty until a point is added.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param box Input box.
 *
 * @return \c true when \c min[i] > max[i] on any axis.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto empty(aabb<Value, N> const& box) noexcept -> bool {
  for (std::size_t i{0}; i < N; ++i) {
    if (box.min()[i] > box.max()[i]) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Reports whether \p box contains the point \p p (boundary inclusive).
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param box Bounding box.
 * @param p Point to test.
 *
 * @return \c true when \c box.min()[i] <= p[i] <= box.max()[i] on every axis.
 *
 * @pre \p box is well-formed (\c min <= max component-wise).
 * @post None.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
contains_point(aabb<Value, N> const& box, nexenne::math::vector<Value, N> const& p) noexcept
  -> bool {
  for (std::size_t i{0}; i < N; ++i) {
    if (p[i] < box.min()[i] || p[i] > box.max()[i]) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Reports whether \p outer fully contains \p inner.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param outer Outer box.
 * @param inner Inner box.
 *
 * @return \c true when every point of \p inner lies inside \p outer.
 *
 * @pre \p outer and \p inner are well-formed.
 * @post None.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
contains_aabb(aabb<Value, N> const& outer, aabb<Value, N> const& inner) noexcept -> bool {
  return contains_point(outer, inner.min()) && contains_point(outer, inner.max());
}

/**
 * @brief Returns \p box expanded uniformly by \p amount on every face.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 * @param box Input box.
 * @param amount Distance to push each face outward (negative shrinks).
 *
 * @return The expanded box.
 *
 * @pre None.
 * @post The result stays well-formed when \p box is well-formed and \p amount is
 *       non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto expand(aabb<Real, N> const& box, Real const amount) noexcept
  -> aabb<Real, N> {
  auto delta{nexenne::math::vector<Real, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    delta[i] = amount;
  }
  return aabb<Real, N>{box.min() - delta, box.max() + delta};
}

/**
 * @brief Returns the smallest box containing \p box and the point \p p.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param box Input box.
 * @param p Point to include.
 *
 * @return The expanded box.
 *
 * @pre \p box is well-formed.
 * @post The result is well-formed and contains both \p box and \p p.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
expand_to_include(aabb<Value, N> const& box, nexenne::math::vector<Value, N> const& p) noexcept
  -> aabb<Value, N> {
  return aabb<Value, N>{
    nexenne::math::component_min(box.min(), p), nexenne::math::component_max(box.max(), p)
  };
}

/**
 * @brief Smallest box containing both \p a and \p b.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param a First box.
 * @param b Second box.
 *
 * @return The union box.
 *
 * @pre \p a and \p b are well-formed.
 * @post The result is well-formed and contains both \p a and \p b.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto union_of(aabb<Value, N> const& a, aabb<Value, N> const& b) noexcept
  -> aabb<Value, N> {
  return aabb<Value, N>{
    nexenne::math::component_min(a.min(), b.min()), nexenne::math::component_max(a.max(), b.max())
  };
}

/**
 * @brief Intersection box of \p a and \p b.
 *
 * When the boxes do not overlap the result is empty in the sense that \c empty
 * returns \c true. Useful both for the contact region and as a clipping block.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param a First box.
 * @param b Second box.
 *
 * @return The intersection box, possibly empty.
 *
 * @pre \p a and \p b are well-formed.
 * @post \c empty() returns \c true for the result when \p a and \p b do not
 *       overlap.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
intersection_of(aabb<Value, N> const& a, aabb<Value, N> const& b) noexcept -> aabb<Value, N> {
  return aabb<Value, N>{
    nexenne::math::component_max(a.min(), b.min()), nexenne::math::component_min(a.max(), b.max())
  };
}

/**
 * @brief Reports whether two boxes overlap (boundary touching counts).
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param a First box.
 * @param b Second box.
 *
 * @return \c true when the boxes overlap on every axis.
 *
 * @pre \p a and \p b are well-formed.
 * @post None.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto intersects(aabb<Value, N> const& a, aabb<Value, N> const& b) noexcept
  -> bool {
  for (std::size_t i{0}; i < N; ++i) {
    if (a.max()[i] < b.min()[i] || a.min()[i] > b.max()[i]) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Closest point inside \p box to the point \p p.
 *
 * When \p p is inside, returns \p p. Otherwise clamps per axis to the boundary.
 *
 * @tparam Value Component type.
 * @tparam N Dimension.
 * @param box Box.
 * @param p Query point.
 *
 * @return Closest point on or in the box.
 *
 * @pre \p box is well-formed.
 * @post \c contains_point(box, result) is \c true.
 */
template <nexenne::math::arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
closest_point(aabb<Value, N> const& box, nexenne::math::vector<Value, N> const& p) noexcept
  -> nexenne::math::vector<Value, N> {
  auto result{nexenne::math::vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = nexenne::math::clamp(p[i], box.min()[i], box.max()[i]);
  }
  return result;
}

/**
 * @brief Squared distance from \p p to the closest point in \p box.
 *
 * Avoids the square root and returns \c 0 when \p p is inside.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 * @param box Box.
 * @param p Query point.
 *
 * @return The squared distance.
 *
 * @pre \p box is well-formed.
 * @post The result is non-negative; \c 0 when \p p is inside \p box.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance_squared(aabb<Real, N> const& box, nexenne::math::vector<Real, N> const& p) noexcept
  -> Real {
  return nexenne::math::distance_squared(closest_point(box, p), p);
}

/**
 * @brief Euclidean distance from \p p to the closest point in \p box.
 *
 * Returns \c 0 when \p p is inside.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 * @param box Box.
 * @param p Query point.
 *
 * @return The distance.
 *
 * @pre \p box is well-formed.
 * @post The result is non-negative; \c 0 when \p p is inside \p box.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance(aabb<Real, N> const& box, nexenne::math::vector<Real, N> const& p) noexcept -> Real {
  return nexenne::math::sqrt(distance_squared(box, p));
}

}  // namespace nexenne::geometry
