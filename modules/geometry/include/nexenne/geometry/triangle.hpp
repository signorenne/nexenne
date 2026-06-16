#pragma once

/**
 * @file
 * @brief Triangle (triangle): three vertices in 2D or 3D, with its queries.
 *
 * Vertices are stored in winding order \c a, \c b, \c c. In 2D, counter-
 * clockwise vertices give a positive signed area and clockwise a negative one.
 * Templated on \p N so one type covers both dimensions. Everything is
 * \c constexpr and \c noexcept.
 *
 * Dimension-specific queries: \c signed_area and \c contains_point are 2D only;
 * \c normal is 3D only (and \c result-returning, since a degenerate triangle has
 * no normal). \c centroid, \c area, and \c bounding_aabb work in both.
 *
 * Aliases: \c triangle2 / \c triangle3, each with \c _f (float) and \c _d
 * (double).
 */

#include <concepts>
#include <cstddef>
#include <expected>
#include <type_traits>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/error.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Triangle stored as three vertices in winding order.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Dimension.
 */
template <std::floating_point Real, std::size_t N>
class triangle {
public:
  using value_type = Real;
  using point_type = nexenne::math::vector<value_type, N>;

private:
  point_type m_a{};
  point_type m_b{};
  point_type m_c{};

public:
  /// @brief Constructs a degenerate triangle with all vertices at the origin.
  constexpr triangle() noexcept = default;

  /**
   * @brief Constructs a triangle from three vertices.
   *
   * @param a First vertex.
   * @param b Second vertex.
   * @param c Third vertex.
   */
  constexpr triangle(point_type a, point_type b, point_type c) noexcept : m_a{a}, m_b{b}, m_c{c} {}

  /**
   * @brief First vertex.
   *
   * @return Const reference to the stored first vertex.
   */
  [[nodiscard]] constexpr auto a() const noexcept -> point_type const& {
    return m_a;
  }

  /**
   * @brief Second vertex.
   *
   * @return Const reference to the stored second vertex.
   */
  [[nodiscard]] constexpr auto b() const noexcept -> point_type const& {
    return m_b;
  }

  /**
   * @brief Third vertex.
   *
   * @return Const reference to the stored third vertex.
   */
  [[nodiscard]] constexpr auto c() const noexcept -> point_type const& {
    return m_c;
  }

  /**
   * @brief Mutable first vertex.
   *
   * @return Reference to the stored first vertex.
   */
  [[nodiscard]] constexpr auto a() noexcept -> point_type& {
    return m_a;
  }

  /**
   * @brief Mutable second vertex.
   *
   * @return Reference to the stored second vertex.
   */
  [[nodiscard]] constexpr auto b() noexcept -> point_type& {
    return m_b;
  }

  /**
   * @brief Mutable third vertex.
   *
   * @return Reference to the stored third vertex.
   */
  [[nodiscard]] constexpr auto c() noexcept -> point_type& {
    return m_c;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(triangle const&, triangle const&) noexcept = default;
};

template <std::floating_point Real>
using triangle2 = triangle<Real, 2>;
template <std::floating_point Real>
using triangle3 = triangle<Real, 3>;

using triangle2_f = triangle2<float>;
using triangle2_d = triangle2<double>;
using triangle3_f = triangle3<float>;
using triangle3_d = triangle3<double>;

static_assert(std::is_trivially_copyable_v<triangle2_f>);
static_assert(sizeof(triangle2_f) == 6 * sizeof(float));
static_assert(sizeof(triangle3_f) == 9 * sizeof(float));

/**
 * @brief Arithmetic mean of the three vertices.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param t Triangle.
 *
 * @return \c (a + b + c) / 3.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto centroid(triangle<Real, N> const& t
) noexcept -> nexenne::math::vector<Real, N> {
  return (t.a() + t.b() + t.c()) * (Real{1} / Real{3});
}

/**
 * @brief Signed 2D area: positive when \c (a, b, c) is counter-clockwise.
 *
 * Computes \c cross(b - a, c - a) / 2 with the 2D pseudo-cross, so it doubles as
 * an orientation test.
 *
 * @tparam Real Component type.
 * @param t 2D triangle.
 *
 * @return The signed area.
 *
 * @pre None.
 * @post Positive for counter-clockwise winding, negative for clockwise, zero
 *       when degenerate.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto signed_area(triangle<Real, 2> const& t) noexcept -> Real {
  return nexenne::math::cross(t.b() - t.a(), t.c() - t.a()) * Real{0.5};
}

/**
 * @brief Unsigned 2D area: \c |signed_area|.
 *
 * @tparam Real Component type.
 * @param t 2D triangle.
 *
 * @return The non-negative area.
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(triangle<Real, 2> const& t) noexcept -> Real {
  return nexenne::math::abs(signed_area(t));
}

/**
 * @brief Unsigned 3D area: \c length(cross(b - a, c - a)) / 2.
 *
 * @tparam Real Component type.
 * @param t 3D triangle.
 *
 * @return The non-negative area.
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto area(triangle<Real, 3> const& t) noexcept -> Real {
  return nexenne::math::length(nexenne::math::cross(t.b() - t.a(), t.c() - t.a())) * Real{0.5};
}

/**
 * @brief Unit face normal of a 3D triangle.
 *
 * The normalized cross product \c (b - a) x (c - a). By the right-hand rule, if
 * \c (a, b, c) is counter-clockwise viewed from the normal's tip, the normal
 * points outward. A degenerate (collinear or coincident) triangle is reported
 * rather than returning a NaN-laden normal.
 *
 * @tparam Real Component type.
 * @param t 3D triangle.
 *
 * @return The unit normal on success, or
 *         \c geometry_error::degenerate_primitive when the vertices are
 *         collinear.
 *
 * @pre None. Degenerate triangles are detected and reported.
 * @post On success the returned vector has unit length.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto normal(triangle<Real, 3> const& t
) noexcept -> result<nexenne::math::vector<Real, 3>> {
  auto const n{nexenne::math::normalize(nexenne::math::cross(t.b() - t.a(), t.c() - t.a()))};
  if (!n) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  return *n;
}

/**
 * @brief Reports whether \p p lies inside or on the boundary of \p t.
 *
 * Uses the barycentric-orientation test: \p p is inside when the three
 * sub-triangles \c (p, a, b), \c (p, b, c), \c (p, c, a) share an orientation.
 * Handles either winding order; boundary inclusive.
 *
 * @tparam Real Component type.
 * @param t 2D triangle.
 * @param p Point to test.
 *
 * @return \c true when \p p is inside the closed triangle.
 *
 * @pre None.
 * @post None.
 *
 * @warning A degenerate (zero-area) triangle is treated as containing its whole
 *          supporting line or point: for a collinear or single-point triangle
 *          every edge cross product is zero, so no sign disagreement is possible
 *          and every point on the supporting line (or every point at all, for a
 *          single-point triangle) reports \c true. Points off the supporting line
 *          are still correctly excluded. Reject zero-area triangles beforehand
 *          (their \c signed_area is zero) if that matters to the caller.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto contains_point(
  triangle<Real, 2> const& t, nexenne::math::vector<Real, 2> const& p
) noexcept -> bool {
  // The 2D cross of (v1 - v0, v2 - v0) is positive when v2 sits to the left of
  // the directed edge v0 -> v1, negative to the right, zero on the line. Taking
  // p as v0 against each triangle edge gives p's side of that edge.
  auto const edge_sign{
    [](
      nexenne::math::vector<Real, 2> const& v0,
      nexenne::math::vector<Real, 2> const& v1,
      nexenne::math::vector<Real, 2> const& v2
    ) noexcept -> Real { return nexenne::math::cross(v1 - v0, v2 - v0); }
  };
  auto const d1{edge_sign(p, t.a(), t.b())};
  auto const d2{edge_sign(p, t.b(), t.c())};
  auto const d3{edge_sign(p, t.c(), t.a())};
  // p is inside the closed triangle when it is on the same side of all three
  // edges, i.e. the three signs never disagree. Testing "not (some negative and
  // some positive)" rather than "all positive" makes the result independent of
  // the triangle's winding (works for both orientations) and inclusive of the
  // boundary (a zero agrees with either sign). See Ericson, RTCD section 3.4.
  auto const has_neg{d1 < Real{0} || d2 < Real{0} || d3 < Real{0}};
  auto const has_pos{d1 > Real{0} || d2 > Real{0} || d3 > Real{0}};
  return !(has_neg && has_pos);
}

/**
 * @brief Closest point on or inside the triangle to \p p.
 *
 * Walks the triangle's vertex, edge, and face Voronoi regions with the orientation
 * test of Ericson's closest-point-on-triangle routine (RTCD section 5.1.5): three
 * edge-direction dot products place \p p against each feature, returning the first
 * matching vertex, edge projection, or the interior barycentric blend. Works in
 * 2D and 3D since it only uses dot products. A degenerate (collinear) triangle
 * still returns a point on one of its edges.
 *
 * See Christer Ericson, "Real-Time Collision Detection", section 5.1.5.
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param t Triangle.
 * @param p Query point.
 *
 * @return Closest point on or in the triangle.
 *
 * @pre None. Degenerate triangles are handled.
 * @post The result lies in the closed triangle.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto closest_point(
  triangle<Real, N> const& t, nexenne::math::vector<Real, N> const& p
) noexcept -> nexenne::math::vector<Real, N> {
  using nexenne::math::dot;
  auto const& a{t.a()};
  auto const& b{t.b()};
  auto const& c{t.c()};
  auto const ab{b - a};
  auto const ac{c - a};

  auto const d1{dot(ab, p - a)};
  auto const d2{dot(ac, p - a)};
  if (d1 <= Real{0} && d2 <= Real{0}) {
    return a;  // vertex region of a.
  }
  auto const d3{dot(ab, p - b)};
  auto const d4{dot(ac, p - b)};
  if (d3 >= Real{0} && d4 <= d3) {
    return b;  // vertex region of b.
  }
  auto const vc{d1 * d4 - d3 * d2};
  if (vc <= Real{0} && d1 >= Real{0} && d3 <= Real{0}) {
    return a + ab * (d1 / (d1 - d3));  // edge region ab.
  }
  auto const d5{dot(ab, p - c)};
  auto const d6{dot(ac, p - c)};
  if (d6 >= Real{0} && d5 <= d6) {
    return c;  // vertex region of c.
  }
  auto const vb{d5 * d2 - d1 * d6};
  if (vb <= Real{0} && d2 >= Real{0} && d6 <= Real{0}) {
    return a + ac * (d2 / (d2 - d6));  // edge region ac.
  }
  auto const va{d3 * d6 - d5 * d4};
  if (va <= Real{0} && (d4 - d3) >= Real{0} && (d5 - d6) >= Real{0}) {
    return b + (c - b) * ((d4 - d3) / ((d4 - d3) + (d5 - d6)));  // edge region bc.
  }
  auto const denom{Real{1} / (va + vb + vc)};  // face interior.
  return a + ab * (vb * denom) + ac * (vc * denom);
}

/**
 * @brief Smallest axis-aligned box around a triangle.
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param t Triangle.
 *
 * @return Tight box of \p t.
 *
 * @pre None.
 * @post The result is well-formed and contains all three vertices.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto bounding_aabb(triangle<Real, N> const& t) noexcept -> aabb<Real, N> {
  return aabb<Real, N>{
    nexenne::math::component_min(nexenne::math::component_min(t.a(), t.b()), t.c()),
    nexenne::math::component_max(nexenne::math::component_max(t.a(), t.b()), t.c())
  };
}

}  // namespace nexenne::geometry
