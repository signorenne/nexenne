#pragma once

/**
 * @file
 * @brief Pose aggregates and shape transforms: position, rotation, and scale.
 *
 * The math module builds the matrices (\c translation3, \c rotation3,
 * \c scale3, \c look_at). This header adds the "thing with a pose" aggregates
 * that bundle a position, a rotation, and a scale into one value, the conversions
 * to a matrix, and the application of a pose to a point, a direction, or a whole
 * shape:
 *
 *   - \c transform2d : a 2D pose (position + angle + scale).
 *   - \c transform3d : a 3D pose (position + quaternion + scale).
 *   - \c to_matrix : the pose as its homogeneous matrix.
 *   - \c transform_point / \c transform_direction : apply a pose to a vector.
 *   - \c transform(pose, shape) : apply a pose to a primitive (sphere, box,
 *     triangle, circle, obb, segment, capsule), with an axis-aligned box mapping
 *     to an oriented box because a rotation tilts it.
 *   - \c decompose_2 / \c decompose_3 : recover a pose from a matrix.
 *
 * Composition order is the conventional scale, then rotate, then translate
 * (\c translation * rotation * scale, applied right to left to a column vector).
 *
 * \c constexpr coverage follows the math module: the 3D path is fully
 * \c constexpr (the 3D matrix builders and quaternion rotation are), while the
 * 2D path and \c decompose_2 are runtime-only because \c rotation2 and the
 * angle recovery need \c std::sin / \c std::cos / \c std::atan2, none
 * \c constexpr in C++23.
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>

#include <nexenne/geometry/aabb.hpp>
#include <nexenne/geometry/capsule.hpp>
#include <nexenne/geometry/circle.hpp>
#include <nexenne/geometry/error.hpp>
#include <nexenne/geometry/obb.hpp>
#include <nexenne/geometry/segment.hpp>
#include <nexenne/geometry/sphere.hpp>
#include <nexenne/geometry/triangle.hpp>
#include <nexenne/math/angle.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/transform.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::geometry {

using nexenne::math::matrix;
using nexenne::math::quaternion;
using nexenne::math::radians;
using nexenne::math::vector;

/**
 * @brief 2D pose: position, rotation angle, and scale.
 *
 * Composition order: scale, then rotate about the origin, then translate.
 * Convert to a matrix with \c to_matrix.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class transform2d {
private:
  vector<Real, 2> m_position{Real{0}, Real{0}};
  radians<Real> m_rotation{};
  vector<Real, 2> m_scale{Real{1}, Real{1}};

public:
  using value_type = Real;

  /// @brief Constructs the identity pose (origin, zero angle, unit scale).
  constexpr transform2d() noexcept = default;

  /**
   * @brief Constructs a pose from a position, rotation, and scale.
   *
   * @param position World position.
   * @param rotation Rotation angle, counter-clockwise.
   * @param scale Per-axis scale.
   */
  constexpr transform2d(
    vector<Real, 2> position, radians<Real> rotation, vector<Real, 2> scale
  ) noexcept
      : m_position{position}, m_rotation{rotation}, m_scale{scale} {}

  /**
   * @brief World position.
   *
   * @return Const reference to the stored position.
   */
  [[nodiscard]] constexpr auto position() const noexcept -> vector<Real, 2> const& {
    return m_position;
  }

  /**
   * @brief Rotation angle.
   *
   * @return Const reference to the stored rotation angle.
   */
  [[nodiscard]] constexpr auto rotation() const noexcept -> radians<Real> const& {
    return m_rotation;
  }

  /**
   * @brief Per-axis scale.
   *
   * @return Const reference to the stored scale.
   */
  [[nodiscard]] constexpr auto scale() const noexcept -> vector<Real, 2> const& {
    return m_scale;
  }

  /**
   * @brief Mutable world position.
   *
   * @return Reference to the stored position.
   */
  [[nodiscard]] constexpr auto position() noexcept -> vector<Real, 2>& {
    return m_position;
  }

  /**
   * @brief Mutable rotation angle.
   *
   * @return Reference to the stored rotation angle.
   */
  [[nodiscard]] constexpr auto rotation() noexcept -> radians<Real>& {
    return m_rotation;
  }

  /**
   * @brief Mutable per-axis scale.
   *
   * @return Reference to the stored scale.
   */
  [[nodiscard]] constexpr auto scale() noexcept -> vector<Real, 2>& {
    return m_scale;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(transform2d const&, transform2d const&) noexcept = default;

  /**
   * @brief The identity pose.
   *
   * @return The default pose: origin position, zero angle, unit scale.
   *
   * @pre None.
   * @post The result equals a default-constructed \c transform2d.
   */
  [[nodiscard]] static constexpr auto identity() noexcept -> transform2d {
    return transform2d{};
  }
};

using transform2d_f = transform2d<float>;
using transform2d_d = transform2d<double>;

static_assert(std::is_trivially_copyable_v<transform2d_f>);
static_assert(std::is_standard_layout_v<transform2d_f>);

/**
 * @brief 3D pose: position, rotation quaternion, and scale.
 *
 * Composition order: scale, then rotate, then translate.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class transform3d {
private:
  vector<Real, 3> m_position{Real{0}, Real{0}, Real{0}};
  quaternion<Real> m_rotation{};
  vector<Real, 3> m_scale{Real{1}, Real{1}, Real{1}};

public:
  using value_type = Real;

  /// @brief Constructs the identity pose (origin, identity rotation, unit scale).
  constexpr transform3d() noexcept = default;

  /**
   * @brief Constructs a pose from a position, rotation, and scale.
   *
   * @param position World position.
   * @param rotation Orientation, expected unit length.
   * @param scale Per-axis scale.
   */
  constexpr transform3d(
    vector<Real, 3> position, quaternion<Real> rotation, vector<Real, 3> scale
  ) noexcept
      : m_position{position}, m_rotation{rotation}, m_scale{scale} {}

  /**
   * @brief World position.
   *
   * @return Const reference to the stored position.
   */
  [[nodiscard]] constexpr auto position() const noexcept -> vector<Real, 3> const& {
    return m_position;
  }

  /**
   * @brief Orientation quaternion.
   *
   * @return Const reference to the stored rotation.
   */
  [[nodiscard]] constexpr auto rotation() const noexcept -> quaternion<Real> const& {
    return m_rotation;
  }

  /**
   * @brief Per-axis scale.
   *
   * @return Const reference to the stored scale.
   */
  [[nodiscard]] constexpr auto scale() const noexcept -> vector<Real, 3> const& {
    return m_scale;
  }

  /**
   * @brief Mutable world position.
   *
   * @return Reference to the stored position.
   */
  [[nodiscard]] constexpr auto position() noexcept -> vector<Real, 3>& {
    return m_position;
  }

  /**
   * @brief Mutable orientation quaternion.
   *
   * @return Reference to the stored rotation.
   */
  [[nodiscard]] constexpr auto rotation() noexcept -> quaternion<Real>& {
    return m_rotation;
  }

  /**
   * @brief Mutable per-axis scale.
   *
   * @return Reference to the stored scale.
   */
  [[nodiscard]] constexpr auto scale() noexcept -> vector<Real, 3>& {
    return m_scale;
  }

  /// @brief Component-wise equality and ordering, defaulted.
  [[nodiscard]] friend constexpr auto
  operator<=>(transform3d const&, transform3d const&) noexcept = default;

  /**
   * @brief The identity pose.
   *
   * @return The default pose: origin position, identity rotation, unit scale.
   *
   * @pre None.
   * @post The result equals a default-constructed \c transform3d.
   */
  [[nodiscard]] static constexpr auto identity() noexcept -> transform3d {
    return transform3d{};
  }
};

using transform3d_f = transform3d<float>;
using transform3d_d = transform3d<double>;

static_assert(std::is_trivially_copyable_v<transform3d_f>);
static_assert(std::is_standard_layout_v<transform3d_f>);

/**
 * @brief Converts a \c transform2d to its 3x3 homogeneous matrix.
 *
 * @tparam Real Component type.
 * @param t Pose.
 *
 * @return The matrix \c translation2 * rotation2 * scale2.
 *
 * @pre None.
 * @post The result composes scale, then rotation, then translation.
 *
 * @note Runtime only: \c rotation2 needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_matrix(transform2d<Real> const t) noexcept -> matrix<Real, 3> {
  return nexenne::math::translation2(t.position()) * nexenne::math::rotation2(t.rotation())
         * nexenne::math::scale2(t.scale());
}

/**
 * @brief Applies a 2D pose to a point (scale, rotate, translate).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param p Point.
 *
 * @return The transformed point.
 *
 * @pre None.
 * @post The result is \p p mapped through \p t.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform_point(transform2d<Real> const t, vector<Real, 2> const p) noexcept
  -> vector<Real, 2> {
  // Scale, then rotate by the angle, then translate, directly: this is the hot
  // path, so it avoids building and multiplying the 3x3 matrix. The result equals
  // to_matrix(t) applied to p.
  auto const c{std::cos(t.rotation().value())};
  auto const s{std::sin(t.rotation().value())};
  auto const sx{p.x() * t.scale().x()};
  auto const sy{p.y() * t.scale().y()};
  return vector<Real, 2>{t.position().x() + c * sx - s * sy, t.position().y() + s * sx + c * sy};
}

/**
 * @brief Applies the linear part of a 2D pose to a direction (no translation).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param d Direction.
 *
 * @return The transformed direction.
 *
 * @pre None.
 * @post The result is \p d under the linear part of \p t; translation is not
 *       applied.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform_direction(transform2d<Real> const t, vector<Real, 2> const d) noexcept
  -> vector<Real, 2> {
  // Scale then rotate directly (no matrix build, no translation), the hot path.
  // The result equals the linear part of to_matrix(t) applied to d.
  auto const c{std::cos(t.rotation().value())};
  auto const s{std::sin(t.rotation().value())};
  auto const sx{d.x() * t.scale().x()};
  auto const sy{d.y() * t.scale().y()};
  return vector<Real, 2>{c * sx - s * sy, s * sx + c * sy};
}

/**
 * @brief Converts a \c transform3d to its 4x4 homogeneous matrix.
 *
 * @tparam Real Component type.
 * @param t Pose.
 *
 * @return The matrix \c translation3 * rotation3 * scale3.
 *
 * @pre None.
 * @post The result composes scale, then rotation, then translation.
 *
 * @note Fully \c constexpr: the 3D builders and quaternion rotation are.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_matrix(transform3d<Real> const t) noexcept -> matrix<Real, 4> {
  return nexenne::math::translation3(t.position()) * nexenne::math::rotation3(t.rotation())
         * nexenne::math::scale3(t.scale());
}

/**
 * @brief Applies a 3D pose to a point (scale, rotate, translate).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param p Point.
 *
 * @return The transformed point.
 *
 * @pre None.
 * @post The result is \p p mapped through \p t.
 *
 * @note Applies scale, then a quaternion rotation, then translation directly
 *       instead of building and multiplying the 4x4 matrix, since transforming
 *       points is the hot path. The result equals \c to_matrix(t) applied to
 *       \p p.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
transform_point(transform3d<Real> const t, vector<Real, 3> const p) noexcept -> vector<Real, 3> {
  auto const scaled{
    vector<Real, 3>{p.x() * t.scale().x(), p.y() * t.scale().y(), p.z() * t.scale().z()}
  };
  return t.position() + nexenne::math::rotate(t.rotation(), scaled);
}

/**
 * @brief Applies the linear part of a 3D pose to a direction (no translation).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param d Direction.
 *
 * @return The transformed direction.
 *
 * @pre None.
 * @post The result is \p d under the linear part of \p t; translation is not
 *       applied.
 *
 * @note Applies scale then rotation directly (no matrix build, no translation),
 *       since this is the hot path. The result equals the linear part of
 *       \c to_matrix(t) applied to \p d.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
transform_direction(transform3d<Real> const t, vector<Real, 3> const d) noexcept
  -> vector<Real, 3> {
  auto const scaled{
    vector<Real, 3>{d.x() * t.scale().x(), d.y() * t.scale().y(), d.z() * t.scale().z()}
  };
  return nexenne::math::rotate(t.rotation(), scaled);
}

// Applying a pose to a whole shape. A rotation tilts an axis-aligned box off the
// axes, so transforming an aabb yields an obb. The rounded shapes (sphere,
// circle) stay round only under uniform scale; under non-uniform scale the
// largest scale component is used, giving the smallest enclosing round shape
// (exact when the scale is uniform). An obb transform composes rotations and is
// exact for a rigid or uniformly scaled pose.

/**
 * @brief The largest absolute scale component of a pose.
 *
 * @tparam Real Component type.
 * @tparam N Dimension.
 * @param scale Per-axis scale vector.
 *
 * @return \c max over the axes of \c |scale[i]|.
 *
 * @pre None.
 * @post The result is non-negative.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto max_scale(vector<Real, N> const& scale) noexcept -> Real {
  auto m{Real{0}};
  for (auto i{std::size_t{0}}; i < N; ++i) {
    m = nexenne::math::max(m, nexenne::math::abs(scale[i]));
  }
  return m;
}

/**
 * @brief Applies a 3D pose to a sphere.
 *
 * The center is transformed as a point; the radius is scaled by the largest
 * scale component, so the result encloses the transformed ball exactly under
 * uniform scale and conservatively otherwise.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param s Sphere.
 *
 * @return The transformed (enclosing) sphere.
 *
 * @pre \c s.radius() is non-negative.
 * @post The result contains the image of \p s under \p t.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto transform(transform3d<Real> const t, sphere3<Real> const& s) noexcept
  -> sphere3<Real> {
  return sphere3<Real>{transform_point(t, s.center()), s.radius() * max_scale(t.scale())};
}

/**
 * @brief Applies a 3D pose to a triangle, vertex by vertex.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param tri Triangle.
 *
 * @return The transformed triangle (exact).
 *
 * @pre None.
 * @post Each vertex is the image of the corresponding vertex of \p tri.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
transform(transform3d<Real> const t, triangle3<Real> const& tri) noexcept -> triangle3<Real> {
  return triangle3<Real>{
    transform_point(t, tri.a()), transform_point(t, tri.b()), transform_point(t, tri.c())
  };
}

/**
 * @brief Applies a 3D pose to an axis-aligned box, producing an oriented box.
 *
 * A rotation tilts the box off the world axes, so the exact image is an \c obb3:
 * the box center maps to the obb center, the scaled half-extents to the obb
 * half-size, and the pose rotation to the obb orientation.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param box Axis-aligned box.
 *
 * @return The oriented box image of \p box (exact).
 *
 * @pre \p box is well-formed.
 * @post The result is the exact image of \p box under \p t.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto transform(transform3d<Real> const t, aabb<Real, 3> const& box) noexcept
  -> obb3<Real> {
  auto const half{half_size(box)};
  auto const scaled{vector<Real, 3>{
    half.x() * nexenne::math::abs(t.scale().x()),
    half.y() * nexenne::math::abs(t.scale().y()),
    half.z() * nexenne::math::abs(t.scale().z()),
  }};
  return obb3<Real>{transform_point(t, center(box)), scaled, t.rotation()};
}

/**
 * @brief Applies a 3D pose to an oriented box (enclosing under non-uniform scale).
 *
 * Composes the pose rotation onto the box orientation and scales the half-size so
 * the result always contains the transformed box. Exact for a rigid pose, a
 * uniform scale, or an axis-aligned box; for a non-uniform scale of an already
 * rotated box (whose exact image is a parallelepiped, not a box) the half-size is
 * the tight enclosing extent, so the result is conservative, never under-covering.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param box Oriented box.
 *
 * @return The transformed (enclosing) oriented box.
 *
 * @pre \c box.rotation() has unit length.
 * @post The center and orientation are exact; the result contains the image of
 *       \p box under \p t (exact half-size under a uniform scale or an
 *       axis-aligned box).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto transform(transform3d<Real> const t, obb3<Real> const& box) noexcept
  -> obb3<Real> {
  // Under a non-uniform pose scale the exact image of a rotated box is a
  // parallelepiped, not a box. Scaling each local half-extent by the matching
  // world scale component (the naive formula) applies the scale to the wrong
  // axes once the box is rotated and can return a box SMALLER than the image. To
  // stay conservative we return the tightest box with the composed orientation
  // whose half-extent along composed axis k is the support of the parallelepiped
  // along that axis:
  //   h'[k] = sum_j half[j] * |a_j . (scale (.) a_k)|,
  // where a_j is world axis j of the box (column j of its rotation matrix) and
  // (.) is the component-wise product. The dot picks up the scale-weighted
  // projection of local axis j onto local axis k; for a uniform scale it collapses
  // to half[k]*scale and for an axis-aligned box to half[k]*|scale[k]|, both exact.
  // This generalizes the abs(R) * half AABB bound (Ericson, RTCD 4.2.6) to a
  // diagonal scale sitting between the two rotations.
  auto const r{nexenne::math::to_matrix3(box.rotation())};
  auto const s{t.scale()};
  auto const h{box.half_size()};
  auto scaled{vector<Real, 3>{}};
  for (auto k{std::size_t{0}}; k < 3; ++k) {
    auto acc{Real{0}};
    for (auto j{std::size_t{0}}; j < 3; ++j) {
      auto m{Real{0}};
      for (auto i{std::size_t{0}}; i < 3; ++i) {
        m += s[i] * r(i, j) * r(i, k);
      }
      acc += h[j] * nexenne::math::abs(m);
    }
    scaled[k] = acc;
  }
  return obb3<Real>{transform_point(t, box.center()), scaled, t.rotation() * box.rotation()};
}

/**
 * @brief Applies a 3D pose to a segment, endpoint by endpoint.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param s Segment.
 *
 * @return The transformed segment (exact).
 *
 * @pre None.
 * @post Each endpoint is the image of the corresponding endpoint of \p s.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto transform(transform3d<Real> const t, segment3<Real> const& s) noexcept
  -> segment3<Real> {
  return segment3<Real>{transform_point(t, s.start()), transform_point(t, s.end())};
}

/**
 * @brief Applies a 3D pose to a capsule (enclosing under non-uniform scale).
 *
 * The two spine endpoints are transformed as points; the radius is scaled by the
 * largest scale component, so the result encloses the transformed capsule exactly
 * under uniform scale and conservatively otherwise (the sphere overload's recipe).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param c Capsule.
 *
 * @return The transformed (enclosing) capsule.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result contains the image of \p c under \p t.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto transform(transform3d<Real> const t, capsule3<Real> const& c) noexcept
  -> capsule3<Real> {
  return capsule3<Real>{
    transform_point(t, c.start()), transform_point(t, c.end()), c.radius() * max_scale(t.scale())
  };
}

/**
 * @brief Applies a 2D pose to a circle (enclosing under non-uniform scale).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param c Circle.
 *
 * @return The transformed (enclosing) circle.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result contains the image of \p c under \p t.
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, circle2<Real> const& c) noexcept
  -> circle2<Real> {
  return circle2<Real>{transform_point(t, c.center()), c.radius() * max_scale(t.scale())};
}

/**
 * @brief Applies a 2D pose to a triangle, vertex by vertex.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param tri Triangle.
 *
 * @return The transformed triangle (exact).
 *
 * @pre None.
 * @post Each vertex is the image of the corresponding vertex of \p tri.
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, triangle2<Real> const& tri) noexcept
  -> triangle2<Real> {
  return triangle2<Real>{
    transform_point(t, tri.a()), transform_point(t, tri.b()), transform_point(t, tri.c())
  };
}

/**
 * @brief Applies a 2D pose to an axis-aligned box, producing an oriented box.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param box Axis-aligned box.
 *
 * @return The oriented box image of \p box (exact).
 *
 * @pre \p box is well-formed.
 * @post The result is the exact image of \p box under \p t.
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, aabb<Real, 2> const& box) noexcept
  -> obb2<Real> {
  auto const half{half_size(box)};
  auto const scaled{vector<Real, 2>{
    half.x() * nexenne::math::abs(t.scale().x()), half.y() * nexenne::math::abs(t.scale().y())
  }};
  return obb2<Real>{transform_point(t, center(box)), scaled, t.rotation()};
}

/**
 * @brief Applies a 2D pose to an oriented box (enclosing under non-uniform scale).
 *
 * Adds the pose angle to the box angle and scales the half-size so the result
 * always contains the transformed box. Exact for a rigid pose, a uniform scale,
 * or an axis-aligned box; for a non-uniform scale of a rotated box the half-size
 * is the tight enclosing extent, so the result is conservative, never
 * under-covering.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param box Oriented box.
 *
 * @return The transformed (enclosing) oriented box.
 *
 * @pre None.
 * @post The center and angle are exact; the result contains the image of \p box
 *       under \p t (exact half-size under a uniform scale or an axis-aligned box).
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, obb2<Real> const& box) noexcept
  -> obb2<Real> {
  // Same conservative support bound as the 3D obb overload: the box world axes
  // are the columns of the 2D rotation [c -s; s c], a0 = (c, s) and a1 = (-s, c),
  // and h'[k] = sum_j half[j] * |a_j . (scale (.) a_k)| is the tight enclosing
  // half-extent along composed axis k. It collapses to half[k]*scale under a
  // uniform scale and to half[k]*|scale[k]| for an axis-aligned box.
  auto const c{std::cos(box.rotation().value())};
  auto const s{std::sin(box.rotation().value())};
  auto const a{std::array<vector<Real, 2>, 2>{vector<Real, 2>{c, s}, vector<Real, 2>{-s, c}}};
  auto const sc{t.scale()};
  auto const h{box.half_size()};
  auto scaled{vector<Real, 2>{}};
  for (auto k{std::size_t{0}}; k < 2; ++k) {
    auto acc{Real{0}};
    for (auto j{std::size_t{0}}; j < 2; ++j) {
      auto const m{sc.x() * a[j].x() * a[k].x() + sc.y() * a[j].y() * a[k].y()};
      acc += h[j] * nexenne::math::abs(m);
    }
    scaled[k] = acc;
  }
  return obb2<Real>{transform_point(t, box.center()), scaled, box.rotation() + t.rotation()};
}

/**
 * @brief Applies a 2D pose to a segment, endpoint by endpoint.
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param s Segment.
 *
 * @return The transformed segment (exact).
 *
 * @pre None.
 * @post Each endpoint is the image of the corresponding endpoint of \p s.
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, segment2<Real> const& s) noexcept
  -> segment2<Real> {
  return segment2<Real>{transform_point(t, s.start()), transform_point(t, s.end())};
}

/**
 * @brief Applies a 2D pose to a capsule (enclosing under non-uniform scale).
 *
 * The two spine endpoints are transformed as points; the radius is scaled by the
 * largest scale component, so the result encloses the transformed capsule exactly
 * under uniform scale and conservatively otherwise (the circle overload's recipe).
 *
 * @tparam Real Component type.
 * @param t Pose.
 * @param c Capsule.
 *
 * @return The transformed (enclosing) capsule.
 *
 * @pre \c c.radius() is non-negative.
 * @post The result contains the image of \p c under \p t.
 *
 * @note Runtime only: the 2D transform path needs \c std::sin / \c std::cos.
 */
template <std::floating_point Real>
[[nodiscard]] auto transform(transform2d<Real> const t, capsule2<Real> const& c) noexcept
  -> capsule2<Real> {
  return capsule2<Real>{
    transform_point(t, c.start()), transform_point(t, c.end()), c.radius() * max_scale(t.scale())
  };
}

/**
 * @brief Recovers a \c transform2d from a 3x3 homogeneous matrix.
 *
 * Inverts the standard \c translation2 * rotation2 * scale2 build: the
 * translation is the third column, the scale the lengths of the first two
 * columns, the angle the \c atan2 of the rotation part after dividing out scale.
 *
 * @tparam Real Component type.
 * @param m Input matrix, assumed built as \c translation2 * rotation2 * scale2
 *          with positive scale components.
 *
 * @return The decomposed pose,
 *         \c geometry_error::degenerate_primitive when a scale component is zero,
 *         or \c geometry_error::invalid_input when the linear part contains a
 *         reflection (negative determinant, that is a negative scale component).
 *
 * @pre \p m was built in the standard order with positive scale components, so
 *      the determinant of its linear part is positive.
 * @post On success \c to_matrix of the result reproduces \p m to within
 *       rounding.
 *
 * @note A negative scale component is a reflection that column lengths (always
 *       non-negative) cannot recover and a single rotation angle cannot
 *       represent, so it is reported rather than silently returning a wrong pose.
 * @note Runtime only: \c std::atan2 is not \c constexpr in C++23.
 */
template <std::floating_point Real>
[[nodiscard]] auto decompose_2(matrix<Real, 3> const& m) noexcept -> result<transform2d<Real>> {
  auto const sx{nexenne::math::length(vector<Real, 2>{m(0, 0), m(1, 0)})};
  auto const sy{nexenne::math::length(vector<Real, 2>{m(0, 1), m(1, 1)})};
  if (sx <= static_cast<Real>(1e-10) || sy <= static_cast<Real>(1e-10)) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  // A negative determinant of the linear part is a reflection: the recovered
  // (positive) scale and a single angle cannot reproduce it, so reject rather
  // than return a wrong pose that satisfies neither the @pre nor the round-trip.
  auto const det{m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)};
  if (det < Real{0}) {
    return std::unexpected{geometry_error::invalid_input};
  }
  auto const angle{std::atan2(m(1, 0) / sx, m(0, 0) / sx)};
  return transform2d<Real>{
    vector<Real, 2>{m(0, 2), m(1, 2)}, radians<Real>{angle}, vector<Real, 2>{sx, sy}
  };
}

/**
 * @brief Recovers a \c transform3d from a 4x4 homogeneous matrix.
 *
 * Inverts the standard \c translation3 * rotation3 * scale3 build: the
 * translation is the fourth column, the scale the lengths of the first three
 * columns, the rotation a trace-based quaternion extraction from the normalized
 * rotation part.
 *
 * @tparam Real Component type.
 * @param m Input matrix, assumed built as \c translation3 * rotation3 * scale3
 *          with positive scale components.
 *
 * @return The decomposed pose,
 *         \c geometry_error::degenerate_primitive when a scale component is zero,
 *         or \c geometry_error::invalid_input when the linear part contains a
 *         reflection (negative determinant, that is a negative scale component).
 *
 * @pre \p m was built in the standard order with positive scale components, so
 *      the determinant of its linear part is positive.
 * @post On success \c to_matrix of the result reproduces \p m to within
 *       rounding.
 *
 * @note A negative scale component is a reflection that column lengths (always
 *       non-negative) cannot recover and a quaternion cannot represent, so it is
 *       reported rather than silently returning a wrong pose.
 * @note Fully \c constexpr: only arithmetic and the constexpr \c sqrt; no trig.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto decompose_3(matrix<Real, 4> const& m) noexcept
  -> result<transform3d<Real>> {
  using nexenne::math::cross;
  using nexenne::math::dot;
  using nexenne::math::length;
  using nexenne::math::sqrt;

  auto const col0{vector<Real, 3>{m(0, 0), m(1, 0), m(2, 0)}};
  auto const col1{vector<Real, 3>{m(0, 1), m(1, 1), m(2, 1)}};
  auto const col2{vector<Real, 3>{m(0, 2), m(1, 2), m(2, 2)}};
  auto const sx{length(col0)};
  auto const sy{length(col1)};
  auto const sz{length(col2)};
  if (sx <= static_cast<Real>(1e-10) || sy <= static_cast<Real>(1e-10)
      || sz <= static_cast<Real>(1e-10)) {
    return std::unexpected{geometry_error::degenerate_primitive};
  }
  // A negative determinant of the linear part is a reflection: the recovered
  // (positive) scale and a quaternion cannot reproduce it, so reject rather than
  // return a wrong pose that satisfies neither the @pre nor the round-trip.
  if (dot(col0, cross(col1, col2)) < Real{0}) {
    return std::unexpected{geometry_error::invalid_input};
  }

  // Normalized rotation columns (scale divided out).
  auto const r0{col0 / sx};
  auto const r1{col1 / sy};
  auto const r2{col2 / sz};

  // Trace-based quaternion extraction from a column-major rotation whose columns
  // are r0, r1, r2. The four branches pick the numerically largest pivot.
  auto const trace{r0.x() + r1.y() + r2.z()};
  auto rotation{quaternion<Real>{}};
  if (trace > Real{0}) {
    auto const s{sqrt(trace + Real{1}) * Real{2}};
    auto const inv{Real{1} / s};
    rotation = quaternion<Real>{
      (r1.z() - r2.y()) * inv, (r2.x() - r0.z()) * inv, (r0.y() - r1.x()) * inv, Real{0.25} * s
    };
  } else if (r0.x() > r1.y() && r0.x() > r2.z()) {
    auto const s{sqrt(Real{1} + r0.x() - r1.y() - r2.z()) * Real{2}};
    auto const inv{Real{1} / s};
    rotation = quaternion<Real>{
      Real{0.25} * s, (r1.x() + r0.y()) * inv, (r2.x() + r0.z()) * inv, (r1.z() - r2.y()) * inv
    };
  } else if (r1.y() > r2.z()) {
    auto const s{sqrt(Real{1} + r1.y() - r0.x() - r2.z()) * Real{2}};
    auto const inv{Real{1} / s};
    rotation = quaternion<Real>{
      (r1.x() + r0.y()) * inv, Real{0.25} * s, (r2.y() + r1.z()) * inv, (r2.x() - r0.z()) * inv
    };
  } else {
    auto const s{sqrt(Real{1} + r2.z() - r0.x() - r1.y()) * Real{2}};
    auto const inv{Real{1} / s};
    rotation = quaternion<Real>{
      (r2.x() + r0.z()) * inv, (r2.y() + r1.z()) * inv, Real{0.25} * s, (r0.y() - r1.x()) * inv
    };
  }

  return transform3d<Real>{
    vector<Real, 3>{m(0, 3), m(1, 3), m(2, 3)}, rotation, vector<Real, 3>{sx, sy, sz}
  };
}

}  // namespace nexenne::geometry
