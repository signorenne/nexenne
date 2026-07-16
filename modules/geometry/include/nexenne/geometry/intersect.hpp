#pragma once

/**
 * @file
 * @brief Cross-type intersection queries between geometry primitives.
 *
 * Predicates and parametric hit queries that bridge two different primitive
 * types, each named \c intersects(a, b). Three return shapes:
 *   - \c bool when only the overlap fact matters (sphere vs box, OBB vs OBB,
 *     capsule pairs);
 *   - \c std::optional<Real> when a ray-cast \c t parameter is the output (ray
 *     vs primitive), with \c nullopt for a miss;
 *   - \c std::optional<vector> when the hit point itself is the output (segment
 *     vs plane).
 *
 * Same-type predicates live in the per-primitive headers: \c intersects(aabb,
 * aabb) in aabb.hpp, \c intersects(sphere3, sphere3) in sphere.hpp, and so on.
 * Everything here is \c constexpr and \c noexcept except the 2D OBB queries,
 * which need \c std::sin / \c std::cos (the radians-angle rotation, runtime-only
 * in C++23, exactly as in obb.hpp).
 *
 * The OBB overlap tests use the Separating Axis Theorem (Ericson, "Real-Time
 * Collision Detection", chapter 4): 2D needs 4 candidate axes (two per box), 3D
 * needs 15 (three face normals per box plus the nine pairwise edge cross
 * products).
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <limits>
#include <optional>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/closest_point.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/plane.hpp>
#include <nexenne/geometry/ray.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

/**
 * @brief Ray vs plane intersection.
 *
 * @tparam Real Component type.
 * @param r Ray.
 * @param pl Plane.
 *
 * @return The smallest non-negative hit distance \c t, or \c nullopt when the
 *         ray is parallel to the plane or would hit it behind the origin.
 *
 * @pre \c r.direction() and \c pl.normal() have unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(ray<Real, 3> const& r, plane3<Real> const& pl) noexcept
  -> std::optional<Real> {
  // dot(n, dir) is the rate the signed plane distance changes along the ray;
  // when it is zero the ray runs parallel to the plane and never crosses.
  auto const denom{nexenne::math::dot(pl.normal(), r.direction())};
  if (nexenne::math::abs(denom) <= Real{0}) {
    return std::nullopt;
  }
  // t solves dot(n, origin + t*dir) + d == 0, i.e. the signed distance hits zero.
  auto const t{-(nexenne::math::dot(pl.normal(), r.origin()) + pl.d()) / denom};
  if (t < Real{0}) {
    return std::nullopt;
  }
  return t;
}

/**
 * @brief Ray vs axis-aligned box intersection by the slab method.
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param r Ray.
 * @param box Axis-aligned box.
 *
 * @return The smallest non-negative entry distance \c t, \c 0 when the origin is
 *         already inside, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length for \c t to read as a distance (a
 *      non-unit direction still gives a correct hit/miss).
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto intersects(ray<Real, N> const& r, aabb<Real, N> const& box) noexcept
  -> std::optional<Real> {
  // The box is the intersection of N axis-aligned "slabs" (one per axis). For
  // each slab the ray is inside for t in [t1, t2]; the ray hits the box only
  // where all N intervals overlap. Track the running intersection [t_near, t_far]
  // and reject the moment it becomes empty (t_near > t_far).
  auto t_near{-std::numeric_limits<Real>::infinity()};
  auto t_far{std::numeric_limits<Real>::infinity()};
  for (auto i{std::size_t{0}}; i < N; ++i) {
    auto const o{r.origin()[i]};
    auto const d{r.direction()[i]};
    if (d == Real{0}) {
      // Ray is parallel to this slab: a hit needs the origin already between the
      // slab's planes, otherwise it can never enter on this axis.
      if (o < box.min()[i] || o > box.max()[i]) {
        return std::nullopt;
      }
      continue;
    }
    auto const inv{Real{1} / d};
    auto t1{(box.min()[i] - o) * inv};
    auto t2{(box.max()[i] - o) * inv};
    if (t1 > t2) {
      auto const tmp{t1};
      t1 = t2;
      t2 = tmp;  // order the slab entry/exit regardless of ray direction sign
    }
    t_near = nexenne::math::max(t_near, t1);
    t_far = nexenne::math::min(t_far, t2);
    if (t_near > t_far) {
      return std::nullopt;
    }
  }
  if (t_far < Real{0}) {
    return std::nullopt;  // box is entirely behind the ray
  }
  return nexenne::math::max(t_near, Real{0});  // clamp: origin inside gives 0
}

/**
 * @brief Ray vs sphere intersection in geometric form.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param s Sphere.
 *
 * @return The smallest non-negative hit distance \c t (tangent contacts count),
 *         or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(ray<Real, 3> const& r, sphere3<Real> const& s) noexcept
  -> std::optional<Real> {
  // Substitute the ray into |p - center|^2 = r^2 to get t^2 + 2*b*t + c = 0 with
  // b = dot(m, dir) and c = |m|^2 - r^2, where m points from the center to the
  // origin. c > 0 means the origin is outside; with b > 0 the ray also points
  // away, so it cannot hit. A negative discriminant means the line misses
  // entirely. Otherwise the near root is -b - sqrt(discr); a negative root means
  // the origin is inside the sphere, so the entry distance is clamped to 0.
  auto const m{r.origin() - s.center()};
  auto const b{nexenne::math::dot(m, r.direction())};
  auto const c{nexenne::math::dot(m, m) - s.radius() * s.radius()};
  if (c > Real{0} && b > Real{0}) {
    return std::nullopt;
  }
  auto const discr{b * b - c};
  if (discr < Real{0}) {
    return std::nullopt;
  }
  auto const t{-b - nexenne::math::sqrt(discr)};
  return t < Real{0} ? Real{0} : t;
}

/**
 * @brief Ray vs 2D circle (disc) intersection, the 2D form of ray vs sphere.
 *
 * @tparam Real Component type.
 * @param r 2D ray.
 * @param c Circle.
 *
 * @return The smallest non-negative hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(ray<Real, 2> const& r, circle2<Real> const& c) noexcept
  -> std::optional<Real> {
  auto const m{r.origin() - c.center()};
  auto const b{nexenne::math::dot(m, r.direction())};
  auto const cc{nexenne::math::dot(m, m) - c.radius() * c.radius()};
  if (cc > Real{0} && b > Real{0}) {
    return std::nullopt;
  }
  auto const discr{b * b - cc};
  if (discr < Real{0}) {
    return std::nullopt;
  }
  auto const t{-b - nexenne::math::sqrt(discr)};
  return t < Real{0} ? Real{0} : t;
}

/**
 * @brief Ray vs triangle intersection by the Moller-Trumbore algorithm.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param t 3D triangle.
 *
 * @return The hit distance \c t (both face windings hit, no back-face culling),
 *         or \c nullopt on a miss.
 *
 * @pre The triangle is non-degenerate.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(ray<Real, 3> const& r, triangle<Real, 3> const& t) noexcept
  -> std::optional<Real> {
  // Moller and Trumbore (1997): solve origin + t*dir = a + u*edge1 + v*edge2 for
  // the barycentric (u, v) and the distance t in one shot via Cramer's rule. The
  // scalar triple products are arranged so the determinant a = dot(edge1, h) with
  // h = dir x edge2 appears once; |a| ~ 0 means the ray is parallel to the
  // triangle's plane. u and v are the barycentric coordinates: the hit is inside
  // the triangle when u >= 0, v >= 0, and u + v <= 1.
  auto const edge1{t.b() - t.a()};
  auto const edge2{t.c() - t.a()};
  auto const h{nexenne::math::cross(r.direction(), edge2)};
  auto const a{nexenne::math::dot(edge1, h)};
  // Reject a near-parallel ray, not just an exactly-zero determinant. \c a scales
  // like the product of the two edge lengths, so the parallel test is relative to
  // that product: a tiny determinant divided out (f = 1/a) would otherwise inflate
  // a grazing miss into a hit at a huge, noise-dominated t. Comparing squares
  // keeps the test allocation- and sqrt-free.
  auto const parallel_eps{static_cast<Real>(1e-8)};
  auto const scale_sq{nexenne::math::length_squared(edge1) * nexenne::math::length_squared(edge2)};
  if (a * a <= parallel_eps * parallel_eps * scale_sq) {
    return std::nullopt;
  }
  auto const f{Real{1} / a};
  auto const s{r.origin() - t.a()};
  auto const u{f * nexenne::math::dot(s, h)};
  if (u < Real{0} || u > Real{1}) {
    return std::nullopt;
  }
  auto const q{nexenne::math::cross(s, edge1)};
  auto const v{f * nexenne::math::dot(r.direction(), q)};
  if (v < Real{0} || u + v > Real{1}) {
    return std::nullopt;
  }
  auto const t_hit{f * nexenne::math::dot(edge2, q)};
  if (t_hit < Real{0}) {
    return std::nullopt;  // intersection is behind the ray origin
  }
  return t_hit;
}

/**
 * @brief Segment vs plane intersection.
 *
 * @tparam Real Component type.
 * @param seg 3D segment.
 * @param pl Plane.
 *
 * @return The intersection point, or \c nullopt when the segment is parallel to
 *         the plane or both endpoints lie on the same side.
 *
 * @pre \c pl.normal() has unit length.
 * @post When engaged the result lies on both \p seg and \p pl.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(segment<Real, 3> const& seg, plane3<Real> const& pl) noexcept
  -> std::optional<nexenne::math::vector<Real, 3>> {
  auto const dir{seg.end() - seg.start()};
  auto const denom{nexenne::math::dot(pl.normal(), dir)};
  if (nexenne::math::abs(denom) <= Real{0}) {
    return std::nullopt;
  }
  // t in [0, 1] keeps the crossing within the finite segment; outside means both
  // endpoints sit on the same side of the plane.
  auto const t{-(nexenne::math::dot(pl.normal(), seg.start()) + pl.d()) / denom};
  if (t < Real{0} || t > Real{1}) {
    return std::nullopt;
  }
  return seg.start() + dir * t;
}

/**
 * @brief Sphere vs axis-aligned box overlap.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param box 3D box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c s.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(sphere3<Real> const& s, aabb<Real, 3> const& box) noexcept
  -> bool {
  // The squared distance from the center to its closest point on the box, against
  // the squared radius: no sqrt needed.
  return distance_squared(box, s.center()) <= s.radius() * s.radius();
}

/**
 * @brief Circle vs axis-aligned box overlap, the 2D form of sphere vs box.
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param box 2D box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c c.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(circle2<Real> const& c, aabb<Real, 2> const& box) noexcept
  -> bool {
  return distance_squared(box, c.center()) <= c.radius() * c.radius();
}

/**
 * @brief Sphere vs plane overlap.
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param pl Plane.
 *
 * @return \c true when the sphere straddles or touches the plane.
 *
 * @pre \c pl.normal() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(sphere3<Real> const& s, plane3<Real> const& pl) noexcept
  -> bool {
  return distance(pl, s.center()) <= s.radius();
}

namespace detail {

/**
 * @brief Half-width of a box projected onto an axis (the SAT support radius).
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param half Box half-extents.
 * @param basis Box local axes.
 * @param axis Query axis (need not be unit length; the result scales with it).
 *
 * @return The projected half-width along \p axis.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto projected_radius(
  nexenne::math::vector<Real, N> const& half,
  std::array<nexenne::math::vector<Real, N>, N> const& basis,
  nexenne::math::vector<Real, N> const& axis
) noexcept -> Real {
  // The box's extent along an arbitrary axis is the sum over its own axes of the
  // half-extent times how much that axis aligns with the query axis. This is the
  // box's support width: exactly the radius term the SAT compares against.
  auto sum{Real{0}};
  for (auto i{std::size_t{0}}; i < N; ++i) {
    sum += half[i] * nexenne::math::abs(nexenne::math::dot(basis[i], axis));
  }
  return sum;
}

/**
 * @brief Local axes of a 2D oriented box.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The two unit axes of \p box's local frame.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto axes(obb2<Real> const& box) noexcept
  -> std::array<nexenne::math::vector<Real, 2>, 2> {
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  return std::array<nexenne::math::vector<Real, 2>, 2>{{{c, s}, {-s, c}}};
}

/**
 * @brief Local axes of a 3D oriented box.
 *
 * @tparam Real Component type.
 * @param box Oriented box.
 *
 * @return The three unit axes of \p box's local frame.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto axes(obb3<Real> const& box) noexcept
  -> std::array<nexenne::math::vector<Real, 3>, 3> {
  // The three local axes are the columns of the rotation matrix (column k is
  // R * e_k = rotate(q, e_k)). Building the matrix once shares the nine
  // quaternion products across all three axes, cheaper than three separate
  // rotate() sandwiches, and both boxes need this in every OBB-OBB test.
  auto const r{nexenne::math::to_matrix3(box.rotation())};
  return std::array<nexenne::math::vector<Real, 3>, 3>{{r[0], r[1], r[2]}};
}

/**
 * @brief The standard basis, the local axes of an axis-aligned box.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 *
 * @return The \p N standard basis vectors.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto identity_basis() noexcept
  -> std::array<nexenne::math::vector<Real, N>, N> {
  auto result{std::array<nexenne::math::vector<Real, N>, N>{}};
  for (auto i{std::size_t{0}}; i < N; ++i) {
    result[i][i] = Real{1};
  }
  return result;
}

/**
 * @brief One Separating Axis Theorem test along a candidate axis.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param center_delta Vector between the two box centers.
 * @param half_a First box half-extents.
 * @param basis_a First box axes.
 * @param half_b Second box half-extents.
 * @param basis_b Second box axes.
 * @param axis Candidate separating axis.
 *
 * @return \c true when the axis separates the boxes (so they do NOT overlap).
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto separated_on(
  nexenne::math::vector<Real, N> const& center_delta,
  nexenne::math::vector<Real, N> const& half_a,
  std::array<nexenne::math::vector<Real, N>, N> const& basis_a,
  nexenne::math::vector<Real, N> const& half_b,
  std::array<nexenne::math::vector<Real, N>, N> const& basis_b,
  nexenne::math::vector<Real, N> const& axis
) noexcept -> bool {
  // SAT: two convex boxes are disjoint iff some axis exists on which their
  // projections do not overlap. Projected onto axis, each box is an interval of
  // half-width projected_radius centered at its center's projection; they are
  // separated when the gap between centers exceeds the sum of the two half-widths.
  auto const center_dist{nexenne::math::abs(nexenne::math::dot(center_delta, axis))};
  auto const ra{projected_radius(half_a, basis_a, axis)};
  auto const rb{projected_radius(half_b, basis_b, axis)};
  return center_dist > ra + rb;
}

}  // namespace detail

/**
 * @brief Two 2D oriented boxes overlap (Separating Axis Theorem, 4 axes).
 *
 * @tparam Real Component type.
 * @param a First oriented box.
 * @param b Second oriented box.
 *
 * @return \c true when \p a and \p b overlap.
 *
 * @pre None.
 * @post None.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto intersects(obb2<Real> const& a, obb2<Real> const& b) noexcept -> bool {
  // In 2D the only candidate separating axes are the four box edge normals (two
  // per box). If none separates, the boxes overlap.
  auto const basis_a{detail::axes(a)};
  auto const basis_b{detail::axes(b)};
  auto const delta{b.center() - a.center()};
  for (auto i{std::size_t{0}}; i < 2; ++i) {
    if (detail::separated_on(delta, a.half_size(), basis_a, b.half_size(), basis_b, basis_a[i])) {
      return false;
    }
    if (detail::separated_on(delta, a.half_size(), basis_a, b.half_size(), basis_b, basis_b[i])) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Two 3D oriented boxes overlap (Separating Axis Theorem, 15 axes).
 *
 * @tparam Real Component type.
 * @param a First oriented box.
 * @param b Second oriented box.
 *
 * @return \c true when \p a and \p b overlap.
 *
 * @pre \c a.rotation() and \c b.rotation() have unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(obb3<Real> const& a, obb3<Real> const& b) noexcept -> bool {
  // 3D needs 15 candidate axes: the 6 face normals (3 per box) plus the 9 pairwise
  // edge cross products. The edge-cross axes catch edge-on-edge separations that
  // the face normals miss. A near-zero cross means the two edges are parallel, so
  // it carries no new separating direction and is skipped (a tiny noisy vector
  // would otherwise yield a spurious "separated" verdict).
  auto const basis_a{detail::axes(a)};
  auto const basis_b{detail::axes(b)};
  auto const delta{b.center() - a.center()};
  for (auto i{std::size_t{0}}; i < 3; ++i) {
    if (detail::separated_on(delta, a.half_size(), basis_a, b.half_size(), basis_b, basis_a[i])) {
      return false;
    }
    if (detail::separated_on(delta, a.half_size(), basis_a, b.half_size(), basis_b, basis_b[i])) {
      return false;
    }
  }
  for (auto i{std::size_t{0}}; i < 3; ++i) {
    for (auto j{std::size_t{0}}; j < 3; ++j) {
      auto const edge_cross{nexenne::math::cross(basis_a[i], basis_b[j])};
      auto const len_sq{nexenne::math::length_squared(edge_cross)};
      if (len_sq <= static_cast<Real>(1e-12)) {
        continue;  // parallel edges contribute no separating axis
      }
      // Normalize the edge-cross axis. The basis vectors are unit, so this length
      // is sin(angle between the edges), a value in [0, 1] independent of Real;
      // dividing by it keeps separated_on's projection arithmetic well conditioned
      // for float, where a tiny un-normalized axis would amplify rounding error.
      auto const axis{edge_cross / nexenne::math::sqrt(len_sq)};
      if (detail::separated_on(delta, a.half_size(), basis_a, b.half_size(), basis_b, axis)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Axis-aligned box vs 2D oriented box overlap.
 *
 * @tparam Real Component type.
 * @param box Axis-aligned box.
 * @param o Oriented box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed.
 * @post None.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto intersects(aabb<Real, 2> const& box, obb2<Real> const& o) noexcept -> bool {
  // The AABB is just an OBB with the identity basis, so the 2D SAT applies as-is.
  auto const basis_a{detail::identity_basis<Real, 2>()};
  auto const basis_b{detail::axes(o)};
  auto const a_half{half_size(box)};
  auto const delta{o.center() - center(box)};
  for (auto i{std::size_t{0}}; i < 2; ++i) {
    if (detail::separated_on(delta, a_half, basis_a, o.half_size(), basis_b, basis_a[i])) {
      return false;
    }
    if (detail::separated_on(delta, a_half, basis_a, o.half_size(), basis_b, basis_b[i])) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Axis-aligned box vs 3D oriented box overlap.
 *
 * @tparam Real Component type.
 * @param box Axis-aligned box.
 * @param o Oriented box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c o.rotation() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(aabb<Real, 3> const& box, obb3<Real> const& o) noexcept
  -> bool {
  auto const basis_a{detail::identity_basis<Real, 3>()};
  auto const basis_b{detail::axes(o)};
  auto const a_half{half_size(box)};
  auto const delta{o.center() - center(box)};
  for (auto i{std::size_t{0}}; i < 3; ++i) {
    if (detail::separated_on(delta, a_half, basis_a, o.half_size(), basis_b, basis_a[i])) {
      return false;
    }
    if (detail::separated_on(delta, a_half, basis_a, o.half_size(), basis_b, basis_b[i])) {
      return false;
    }
  }
  for (auto i{std::size_t{0}}; i < 3; ++i) {
    for (auto j{std::size_t{0}}; j < 3; ++j) {
      auto const edge_cross{nexenne::math::cross(basis_a[i], basis_b[j])};
      auto const len_sq{nexenne::math::length_squared(edge_cross)};
      if (len_sq <= static_cast<Real>(1e-12)) {
        continue;  // parallel edges contribute no separating axis
      }
      // Normalize the edge-cross axis. The basis vectors are unit, so this length
      // is sin(angle between the edges), a value in [0, 1] independent of Real;
      // dividing by it keeps separated_on's projection arithmetic well conditioned
      // for float, where a tiny un-normalized axis would amplify rounding error.
      auto const axis{edge_cross / nexenne::math::sqrt(len_sq)};
      if (detail::separated_on(delta, a_half, basis_a, o.half_size(), basis_b, axis)) {
        return false;
      }
    }
  }
  return true;
}

/**
 * @brief Ray vs 2D oriented box intersection.
 *
 * @tparam Real Component type.
 * @param r 2D ray.
 * @param box 2D oriented box.
 *
 * @return The hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto intersects(ray<Real, 2> const& r, obb2<Real> const& box) noexcept
  -> std::optional<Real> {
  // Rotating the ray into the box's local frame makes the box axis-aligned about
  // the origin, so the problem reduces to ray-vs-aabb. The rotation by -theta is
  // the inverse of the box orientation.
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  auto const offset{r.origin() - box.center()};
  auto const local_origin{nexenne::math::vector<Real, 2>{
    c * offset.x() + s * offset.y(), -s * offset.x() + c * offset.y()
  }};
  auto const local_direction{nexenne::math::vector<Real, 2>{
    c * r.direction().x() + s * r.direction().y(), -s * r.direction().x() + c * r.direction().y()
  }};
  auto const local_box{aabb<Real, 2>{-box.half_size(), box.half_size()}};
  return intersects(ray<Real, 2>{local_origin, local_direction}, local_box);
}

/**
 * @brief Ray vs 3D oriented box intersection.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param box 3D oriented box.
 *
 * @return The hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() and \c box.rotation() have unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(ray<Real, 3> const& r, obb3<Real> const& box) noexcept
  -> std::optional<Real> {
  // Same reduction as the 2D case: bring the ray into the box's local frame with
  // the inverse rotation (the conjugate of the unit quaternion), then the box is
  // axis-aligned and ray-vs-aabb finishes the job.
  auto const inv_rot{nexenne::math::conjugate(box.rotation())};
  auto const local_origin{nexenne::math::rotate(inv_rot, r.origin() - box.center())};
  auto const local_direction{nexenne::math::rotate(inv_rot, r.direction())};
  auto const local_box{aabb<Real, 3>{-box.half_size(), box.half_size()}};
  return intersects(ray<Real, 3>{local_origin, local_direction}, local_box);
}

/**
 * @brief A ray cast hit: the distance, the world hit point, and the surface
 *        normal there.
 *
 * The richer companion to the \c intersects(ray, shape) overloads that return
 * only the \c t parameter. The normal is the unit outward surface normal at the
 * hit, oriented to face the incoming ray (so \c dot(normal, ray.direction()) is
 * non-positive). When the ray origin starts inside the shape the hit is at the
 * origin with \c t == 0 and the normal faces back along the ray.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
struct ray_hit3 {
  using value_type = Real;
  using point_type = nexenne::math::vector<Real, 3>;

  Real t{};             ///< Hit distance along the ray (parametric, unit direction).
  point_type point{};   ///< World-space hit point: \c origin + t * direction.
  point_type normal{};  ///< Unit surface normal at the hit, facing the ray.
};

/**
 * @brief Ray cast against a plane, returning the hit point and normal.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param pl Plane.
 *
 * @return The hit record, or \c nullopt when the ray misses (parallel or behind).
 *
 * @pre \c r.direction() and \c pl.normal() have unit length.
 * @post On a hit \c normal faces the ray and \c point lies on the plane.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto raycast(ray<Real, 3> const& r, plane3<Real> const& pl) noexcept
  -> std::optional<ray_hit3<Real>> {
  auto const t{intersects(r, pl)};
  if (!t) {
    return std::nullopt;
  }
  auto normal{pl.normal()};
  if (nexenne::math::dot(normal, r.direction()) > Real{0}) {
    normal = -normal;  // face the incoming ray.
  }
  return ray_hit3<Real>{*t, r.origin() + r.direction() * *t, normal};
}

/**
 * @brief Ray cast against a sphere, returning the hit point and normal.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param s Sphere.
 *
 * @return The hit record, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post On a hit \c normal is the outward surface normal (or \c -direction when
 *       the origin is inside) and \c point lies on the sphere.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto raycast(ray<Real, 3> const& r, sphere3<Real> const& s) noexcept
  -> std::optional<ray_hit3<Real>> {
  auto const t{intersects(r, s)};
  if (!t) {
    return std::nullopt;
  }
  auto const point{r.origin() + r.direction() * *t};
  // A t of zero means the origin is inside, where the surface normal is not
  // defined, so the normal faces back along the ray.
  auto const normal{
    *t <= Real{0} ? -r.direction() : nexenne::math::normalize_or(point - s.center(), -r.direction())
  };
  return ray_hit3<Real>{*t, point, normal};
}

/**
 * @brief Ray cast against a triangle, returning the hit point and normal.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param tri 3D triangle.
 *
 * @return The hit record, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length and \p tri is non-degenerate.
 * @post On a hit \c normal is the triangle's unit normal facing the ray and
 *       \c point lies in the triangle.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto raycast(ray<Real, 3> const& r, triangle<Real, 3> const& tri) noexcept
  -> std::optional<ray_hit3<Real>> {
  auto const t{intersects(r, tri)};
  if (!t) {
    return std::nullopt;
  }
  auto normal{nexenne::math::normalize_or(
    nexenne::math::cross(tri.b() - tri.a(), tri.c() - tri.a()), -r.direction()
  )};
  if (nexenne::math::dot(normal, r.direction()) > Real{0}) {
    normal = -normal;  // face the incoming ray regardless of winding.
  }
  return ray_hit3<Real>{*t, r.origin() + r.direction() * *t, normal};
}

/**
 * @brief Ray cast against an axis-aligned box, returning the hit point and normal.
 *
 * The slab method, tracking which slab last bounded the entry: that axis and the
 * side entered from give the face normal. An origin already inside the box hits at
 * \c t == 0 with the normal facing back along the ray.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param box Axis-aligned box.
 *
 * @return The hit record, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post On a hit \c normal is an axis-aligned unit face normal facing the ray.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto raycast(ray<Real, 3> const& r, aabb<Real, 3> const& box) noexcept
  -> std::optional<ray_hit3<Real>> {
  auto t_near{-std::numeric_limits<Real>::infinity()};
  auto t_far{std::numeric_limits<Real>::infinity()};
  auto hit_axis{std::size_t{0}};
  auto hit_negative{false};  // true when the entry face is the box's max side.
  for (auto i{std::size_t{0}}; i < 3; ++i) {
    auto const o{r.origin()[i]};
    auto const d{r.direction()[i]};
    if (d == Real{0}) {
      if (o < box.min()[i] || o > box.max()[i]) {
        return std::nullopt;
      }
      continue;
    }
    auto const inv{Real{1} / d};
    auto t1{(box.min()[i] - o) * inv};
    auto t2{(box.max()[i] - o) * inv};
    auto from_max{d < Real{0}};  // entering through the max plane when moving in -d.
    if (t1 > t2) {
      auto const tmp{t1};
      t1 = t2;
      t2 = tmp;
    }
    if (t1 > t_near) {
      t_near = t1;
      hit_axis = i;
      hit_negative = from_max;
    }
    t_far = nexenne::math::min(t_far, t2);
    if (t_near > t_far) {
      return std::nullopt;
    }
  }
  if (t_far < Real{0}) {
    return std::nullopt;
  }
  auto const t{nexenne::math::max(t_near, Real{0})};
  auto const point{r.origin() + r.direction() * t};
  auto normal{nexenne::math::vector<Real, 3>{}};
  if (t_near < Real{0}) {
    normal = -r.direction();  // origin inside: no entry face, face back.
  } else {
    normal[hit_axis] = hit_negative ? Real{1} : Real{-1};
  }
  return ray_hit3<Real>{t, point, normal};
}

/**
 * @brief Ray cast against a 3D oriented box, returning the hit point and normal.
 *
 * Brings the ray into the box's local frame (the inverse rotation), casts against
 * the axis-aligned box there, then rotates the hit point and normal back to world
 * space.
 *
 * @tparam Real Component type.
 * @param r 3D ray.
 * @param box 3D oriented box.
 *
 * @return The hit record, or \c nullopt on a miss.
 *
 * @pre \c r.direction() and \c box.rotation() have unit length.
 * @post On a hit \c normal is a unit face normal of the oriented box, facing the
 *       ray.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto raycast(ray<Real, 3> const& r, obb3<Real> const& box) noexcept
  -> std::optional<ray_hit3<Real>> {
  auto const inv_rot{nexenne::math::conjugate(box.rotation())};
  auto const local_origin{nexenne::math::rotate(inv_rot, r.origin() - box.center())};
  auto const local_direction{nexenne::math::rotate(inv_rot, r.direction())};
  auto const local_box{aabb<Real, 3>{-box.half_size(), box.half_size()}};
  auto const hit{raycast(ray<Real, 3>{local_origin, local_direction}, local_box)};
  if (!hit) {
    return std::nullopt;
  }
  return ray_hit3<Real>{
    hit->t,
    box.center() + nexenne::math::rotate(box.rotation(), hit->point),
    nexenne::math::rotate(box.rotation(), hit->normal),
  };
}

/**
 * @brief Capsule vs sphere overlap.
 *
 * @tparam Real Component type.
 * @param cap Capsule.
 * @param s Sphere.
 *
 * @return \c true when the two overlap.
 *
 * @pre \c cap.radius() and \c s.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(capsule<Real, 3> const& cap, sphere3<Real> const& s) noexcept -> bool {
  // A capsule is the spine grown by its radius, so capsule-vs-sphere is "is the
  // sphere center within (cap.radius + s.radius) of the spine".
  auto const r_sum{cap.radius() + s.radius()};
  return distance_squared(axis(cap), s.center()) <= r_sum * r_sum;
}

/**
 * @brief 2D capsule vs circle overlap.
 *
 * @tparam Real Component type.
 * @param cap 2D capsule.
 * @param c Circle.
 *
 * @return \c true when the two overlap.
 *
 * @pre \c cap.radius() and \c c.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(capsule<Real, 2> const& cap, circle2<Real> const& c) noexcept -> bool {
  auto const r_sum{cap.radius() + c.radius()};
  return distance_squared(axis(cap), c.center()) <= r_sum * r_sum;
}

/**
 * @brief Two capsules overlap.
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param a First capsule.
 * @param b Second capsule.
 *
 * @return \c true when the two overlap.
 *
 * @pre \c a.radius() and \c b.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
intersects(capsule<Real, N> const& a, capsule<Real, N> const& b) noexcept -> bool {
  // The closest spine-to-spine distance grown by both radii: overlap when the
  // spines come within the sum of the radii.
  auto const [pa, pb]{closest_points(a, b)};
  auto const r_sum{a.radius() + b.radius()};
  return nexenne::math::distance_squared(pa, pb) <= r_sum * r_sum;
}

// Reversed-argument forwarders. Each asymmetric pair above is defined in one
// argument order; these thin wrappers make \c intersects symmetric so generic
// code (double dispatch over a shape variant) need not remember the blessed order
// per pair. Every wrapper just swaps the arguments onto the primary overload.

/**
 * @brief Axis-aligned box vs sphere overlap (forwards to \c intersects(sphere, box)).
 *
 * @tparam Real Component type.
 * @param box 3D box.
 * @param s Sphere.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c s.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(aabb<Real, 3> const& box, sphere3<Real> const& s) noexcept
  -> bool {
  return intersects(s, box);
}

/**
 * @brief Axis-aligned box vs circle overlap (forwards to \c intersects(circle, box)).
 *
 * @tparam Real Component type.
 * @param box 2D box.
 * @param c Circle.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c c.radius() is non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(aabb<Real, 2> const& box, circle2<Real> const& c) noexcept
  -> bool {
  return intersects(c, box);
}

/**
 * @brief Plane vs sphere overlap (forwards to \c intersects(sphere, plane)).
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param s Sphere.
 *
 * @return \c true when the sphere straddles or touches the plane.
 *
 * @pre \c pl.normal() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(plane3<Real> const& pl, sphere3<Real> const& s) noexcept
  -> bool {
  return intersects(s, pl);
}

/**
 * @brief 2D oriented box vs axis-aligned box overlap (forwards to \c intersects(aabb, obb)).
 *
 * @tparam Real Component type.
 * @param o Oriented box.
 * @param box Axis-aligned box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed.
 * @post None.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto intersects(obb2<Real> const& o, aabb<Real, 2> const& box) noexcept -> bool {
  return intersects(box, o);
}

/**
 * @brief 3D oriented box vs axis-aligned box overlap (forwards to \c intersects(aabb, obb)).
 *
 * @tparam Real Component type.
 * @param o Oriented box.
 * @param box Axis-aligned box.
 *
 * @return \c true when the two overlap.
 *
 * @pre \p box is well-formed and \c o.rotation() has unit length.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(obb3<Real> const& o, aabb<Real, 3> const& box) noexcept
  -> bool {
  return intersects(box, o);
}

/**
 * @brief Sphere vs capsule overlap (forwards to \c intersects(capsule, sphere)).
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param cap Capsule.
 *
 * @return \c true when the two overlap.
 *
 * @pre \c cap.radius() and \c s.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(sphere3<Real> const& s, capsule<Real, 3> const& cap) noexcept -> bool {
  return intersects(cap, s);
}

/**
 * @brief Circle vs 2D capsule overlap (forwards to \c intersects(capsule, circle)).
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param cap 2D capsule.
 *
 * @return \c true when the two overlap.
 *
 * @pre \c cap.radius() and \c c.radius() are non-negative.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(circle2<Real> const& c, capsule<Real, 2> const& cap) noexcept -> bool {
  return intersects(cap, c);
}

/**
 * @brief Plane vs ray intersection (forwards to \c intersects(ray, plane)).
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param r Ray.
 *
 * @return The smallest non-negative hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() and \c pl.normal() have unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(plane3<Real> const& pl, ray<Real, 3> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, pl);
}

/**
 * @brief Axis-aligned box vs ray intersection (forwards to \c intersects(ray, box)).
 *
 * @tparam Real Component type.
 * @tparam N Dimension (2 or 3).
 * @param box Axis-aligned box.
 * @param r Ray.
 *
 * @return The smallest non-negative entry distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length for \c t to read as a distance.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto intersects(aabb<Real, N> const& box, ray<Real, N> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, box);
}

/**
 * @brief Sphere vs ray intersection (forwards to \c intersects(ray, sphere)).
 *
 * @tparam Real Component type.
 * @param s Sphere.
 * @param r 3D ray.
 *
 * @return The smallest non-negative hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(sphere3<Real> const& s, ray<Real, 3> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, s);
}

/**
 * @brief Circle vs ray intersection (forwards to \c intersects(ray, circle)).
 *
 * @tparam Real Component type.
 * @param c Circle.
 * @param r 2D ray.
 *
 * @return The smallest non-negative hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(circle2<Real> const& c, ray<Real, 2> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, c);
}

/**
 * @brief Triangle vs ray intersection (forwards to \c intersects(ray, triangle)).
 *
 * @tparam Real Component type.
 * @param t 3D triangle.
 * @param r 3D ray.
 *
 * @return The hit distance \c t, or \c nullopt on a miss.
 *
 * @pre The triangle is non-degenerate.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(triangle<Real, 3> const& t, ray<Real, 3> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, t);
}

/**
 * @brief 2D oriented box vs ray intersection (forwards to \c intersects(ray, obb)).
 *
 * @tparam Real Component type.
 * @param box 2D oriented box.
 * @param r 2D ray.
 *
 * @return The hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() has unit length.
 * @post When engaged the result is non-negative.
 *
 * @note Runtime only: depends on \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto intersects(obb2<Real> const& box, ray<Real, 2> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, box);
}

/**
 * @brief 3D oriented box vs ray intersection (forwards to \c intersects(ray, obb)).
 *
 * @tparam Real Component type.
 * @param box 3D oriented box.
 * @param r 3D ray.
 *
 * @return The hit distance \c t, or \c nullopt on a miss.
 *
 * @pre \c r.direction() and \c box.rotation() have unit length.
 * @post When engaged the result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto intersects(obb3<Real> const& box, ray<Real, 3> const& r) noexcept
  -> std::optional<Real> {
  return intersects(r, box);
}

/**
 * @brief Plane vs segment intersection (forwards to \c intersects(segment, plane)).
 *
 * @tparam Real Component type.
 * @param pl Plane.
 * @param seg 3D segment.
 *
 * @return The intersection point, or \c nullopt when parallel or same-side.
 *
 * @pre \c pl.normal() has unit length.
 * @post When engaged the result lies on both \p seg and \p pl.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
intersects(plane3<Real> const& pl, segment<Real, 3> const& seg) noexcept
  -> std::optional<nexenne::math::vector<Real, 3>> {
  return intersects(seg, pl);
}

}  // namespace nexenne::geometry
