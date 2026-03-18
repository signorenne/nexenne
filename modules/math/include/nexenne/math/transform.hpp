#pragma once

/**
 * @file
 * @brief 2D and 3D affine transform matrix builders, plus a view matrix.
 *
 * Construction primitives for homogeneous transform matrices:
 *   - \c translation2 / \c rotation2 / \c scale2 return a \c matrix<Real, 3>
 *     (2D homogeneous, third row (0, 0, 1)).
 *   - \c translation3 / \c scale3 / \c rotation3 / \c rotation3_axis_angle
 *     return a \c matrix<Real, 4> (3D homogeneous, fourth row (0, 0, 0, 1)).
 *   - \c look_at builds the right-handed world-to-view basis-change matrix.
 *   - \c transform_point / \c transform_direction apply a 4x4 matrix to a point
 *     (with perspective divide) or a free vector (rotation and scale only).
 *
 * Conventions match the rest of the module: column-major storage (matrix.hpp),
 * a right-handed coordinate system, and a view frame whose forward axis is -Z.
 *
 * Projection matrices (perspective, orthographic) are not here: they live in
 * projection.hpp. The quaternion form of an orientation that looks down a
 * direction is \c look_at_rotation in quaternion.hpp; \c look_at below instead
 * produces the full 4x4 view matrix (rotation plus the eye translation).
 */

#include <concepts>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/quaternion.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::math {

/**
 * @brief 3x3 homogeneous 2D translation matrix.
 *
 * @tparam Real Floating-point component type.
 * @param offset Translation in 2D.
 *
 * @return The 3x3 matrix mapping (x, y, 1) to (x + offset.x(), y + offset.y(), 1).
 *
 * @pre None.
 * @post The translation occupies the third column.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto translation2(vector<Real, 2> const offset
) noexcept -> matrix<Real, 3> {
  auto m{matrix<Real, 3>::identity()};
  m(0, 2) = offset.x();
  m(1, 2) = offset.y();
  return m;
}

/**
 * @brief 3x3 homogeneous 2D rotation about the origin.
 *
 * @tparam Real Floating-point component type.
 * @param angle Rotation angle, counter-clockwise positive.
 *
 * @return The 3x3 rotation matrix.
 *
 * @pre \c angle.value() is finite.
 * @post The upper-left 2x2 block is orthonormal.
 */
template <std::floating_point Real>
[[nodiscard]] auto rotation2(radians<Real> const angle) noexcept -> matrix<Real, 3> {
  auto const c{cos(angle)};
  auto const s{sin(angle)};
  auto m{matrix<Real, 3>::identity()};
  m(0, 0) = c;
  m(0, 1) = -s;
  m(1, 0) = s;
  m(1, 1) = c;
  return m;
}

/**
 * @brief 3x3 homogeneous 2D scale about the origin.
 *
 * @tparam Real Floating-point component type.
 * @param factors Per-axis scale factors.
 *
 * @return The 3x3 diagonal scale matrix (with 1 in the homogeneous slot).
 *
 * @pre None.
 * @post The scale factors occupy the leading diagonal.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto scale2(vector<Real, 2> const factors) noexcept -> matrix<Real, 3> {
  auto m{matrix<Real, 3>::identity()};
  m(0, 0) = factors.x();
  m(1, 1) = factors.y();
  return m;
}

/**
 * @brief 4x4 homogeneous 3D translation matrix.
 *
 * @tparam Real Floating-point component type.
 * @param offset Translation in 3D.
 *
 * @return The 4x4 matrix mapping (x, y, z, 1) to (x+ox, y+oy, z+oz, 1).
 *
 * @pre None.
 * @post The translation occupies the fourth column.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto translation3(vector<Real, 3> const offset
) noexcept -> matrix<Real, 4> {
  auto m{matrix<Real, 4>::identity()};
  m(0, 3) = offset.x();
  m(1, 3) = offset.y();
  m(2, 3) = offset.z();
  return m;
}

/**
 * @brief 4x4 homogeneous 3D scale about the origin.
 *
 * @tparam Real Floating-point component type.
 * @param factors Per-axis scale factors.
 *
 * @return The 4x4 diagonal scale matrix.
 *
 * @pre None.
 * @post The scale factors occupy the leading diagonal.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto scale3(vector<Real, 3> const factors) noexcept -> matrix<Real, 4> {
  auto m{matrix<Real, 4>::identity()};
  m(0, 0) = factors.x();
  m(1, 1) = factors.y();
  m(2, 2) = factors.z();
  return m;
}

/**
 * @brief 4x4 rotation matrix from a unit quaternion.
 *
 * Thin wrapper over \c to_matrix4 for use alongside the other transform
 * builders; see quaternion.hpp for the conversion itself.
 *
 * @tparam Real Floating-point component type.
 * @param q Unit quaternion.
 *
 * @return The 4x4 rotation matrix.
 *
 * @pre \p q has unit length.
 * @post The upper-left 3x3 block is orthonormal.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto rotation3(quaternion<Real> const q) noexcept -> matrix<Real, 4> {
  return to_matrix4(q);
}

/**
 * @brief 4x4 rotation matrix from an axis-angle pair.
 *
 * Builds the rotation through \c from_axis_angle, which validates that \p axis is
 * non-zero, then converts to a matrix.
 *
 * @tparam Real Floating-point component type.
 * @param axis Rotation axis (need not be unit).
 * @param angle Rotation angle.
 *
 * @return The 4x4 rotation matrix, or \c math_error::zero_length_vector when
 *         \p axis is too short.
 *
 * @pre \c angle.value() is finite.
 * @post On success the upper-left 3x3 block is orthonormal.
 */
template <std::floating_point Real>
[[nodiscard]] auto rotation3_axis_angle(
  vector<Real, 3> const axis, radians<Real> const angle
) noexcept -> result<matrix<Real, 4>> {
  auto const q{from_axis_angle(axis, angle)};
  if (!q) {
    return std::unexpected{q.error()};
  }
  return to_matrix4(*q);
}

/**
 * @brief Right-handed view matrix that looks from \p eye toward \p target.
 *
 * Builds the world-to-view basis change: the matrix that moves \p eye to the
 * origin and orients the frame so its -Z axis points from \p eye to \p target,
 * with \p up fixing the roll. The basis is right = normalize(forward x up) and
 * true-up = right x forward (re-orthogonalized, so \p up need not be exactly
 * perpendicular). The rows are the new basis axes and the last column is the
 * negated projection of \p eye onto each, i.e. the rotation transpose times
 * -eye. This is the conventional gluLookAt construction.
 *
 * @tparam Real Floating-point component type.
 * @param eye Origin of the new frame in world space.
 * @param target Point the new -Z axis points toward.
 * @param up World-space up direction (need not be unit).
 *
 * @return The 4x4 view matrix; \c math_error::zero_length_vector when \p eye and
 *         \p target coincide; \c math_error::parallel_vectors when \p up is
 *         parallel to (target - eye), which includes a zero \p up (its cross with
 *         the forward direction vanishes the same way).
 *
 * @pre \p eye and \p target are distinct, and \p up is non-zero and not parallel
 *      to (target - eye).
 * @post On success the matrix maps \p eye to the origin and the view direction
 *       to -Z.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto look_at(
  vector<Real, 3> const eye, vector<Real, 3> const target, vector<Real, 3> const up
) noexcept -> result<matrix<Real, 4>> {
  auto const forward{normalize(target - eye)};
  if (!forward) {
    return std::unexpected{math_error::zero_length_vector};
  }
  auto const right{normalize(cross(*forward, up))};
  if (!right) {
    return std::unexpected{math_error::parallel_vectors};
  }
  auto const true_up{cross(*right, *forward)};
  // The basis axes go in the rows; the last column is the rotation transpose
  // applied to -eye. The bottom row (0, 0, 0, 1) comes from the identity.
  auto m{matrix<Real, 4>::identity()};
  m(0, 0) = right->x();
  m(0, 1) = right->y();
  m(0, 2) = right->z();
  m(0, 3) = -dot(*right, eye);
  m(1, 0) = true_up.x();
  m(1, 1) = true_up.y();
  m(1, 2) = true_up.z();
  m(1, 3) = -dot(true_up, eye);
  m(2, 0) = -forward->x();
  m(2, 1) = -forward->y();
  m(2, 2) = -forward->z();
  m(2, 3) = dot(*forward, eye);
  return m;
}

/**
 * @brief Transforms a 3D point by a 4x4 matrix, with the perspective divide.
 *
 * Treats \p p as the homogeneous point (x, y, z, 1), multiplies, and divides the
 * result by its w component so projection matrices map correctly. When w is 1 (a
 * pure affine transform) or 0 (a direction at infinity) the divide is skipped.
 *
 * @tparam Real Floating-point component type.
 * @param m 4x4 transform matrix.
 * @param p 3D point.
 *
 * @return The transformed 3D point.
 *
 * @pre \p m is affine (bottom row (0, 0, 0, 1)) or the perspective divide is
 *      wanted.
 * @post The perspective divide is applied whenever the homogeneous w is neither
 *       0 nor 1.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
transform_point(matrix<Real, 4> const& m, vector<Real, 3> const p) noexcept -> vector<Real, 3> {
  // Reuse the matrix*vector product, which combines the columns with a packed
  // mul-add (it vectorizes; a hand-rolled row-wise dot product would stride
  // across the column-major storage and fall back to scalar code). The 1 in the
  // w slot picks up the translation column.
  auto const h{m * vector<Real, 4>{p.x(), p.y(), p.z(), Real{1}}};
  if (h.w() == Real{1} || h.w() == Real{0}) {
    return vector<Real, 3>{h.x(), h.y(), h.z()};
  }
  auto const inv_w{Real{1} / h.w()};
  return vector<Real, 3>{h.x() * inv_w, h.y() * inv_w, h.z() * inv_w};
}

/**
 * @brief Transforms a 3D direction (free vector) by a 4x4 matrix.
 *
 * Treats \p d as the homogeneous vector (x, y, z, 0): translation is ignored and
 * only the upper-left 3x3 block is applied. The result is not re-normalized, so
 * if \p m carries scale the length changes accordingly.
 *
 * @tparam Real Floating-point component type.
 * @param m 4x4 transform matrix.
 * @param d 3D direction.
 *
 * @return The transformed 3D direction.
 *
 * @pre None.
 * @post Translation is ignored; only the upper-left 3x3 block of \p m is applied.
 *
 * @note Correct for tangents and other along-the-surface directions, but not for
 *       surface normals under non-uniform scale: a normal must be transformed by
 *       the inverse transpose of the upper-left 3x3 block to stay perpendicular.
 *       Use this only when \p m is a rigid or uniform-scale transform, or pass an
 *       already inverse-transposed matrix for normals.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
transform_direction(matrix<Real, 4> const& m, vector<Real, 3> const d) noexcept -> vector<Real, 3> {
  // Same packed matrix*vector product as transform_point, but the 0 in the w slot
  // drops the translation column, so only the upper-left 3x3 block applies.
  auto const h{m * vector<Real, 4>{d.x(), d.y(), d.z(), Real{0}}};
  return vector<Real, 3>{h.x(), h.y(), h.z()};
}

}  // namespace nexenne::math
