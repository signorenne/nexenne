#pragma once

/**
 * @file
 * @brief 4x4 projection matrix builders (perspective and orthographic) for 3D
 *        graphics pipelines.
 *
 * Conventions: a right-handed coordinate system (+X right, +Y up, +Z out of the
 * screen, so the viewer looks down -Z); a view-space to clip-space mapping; and a
 * clip-space depth range of [-1, 1] (OpenGL-style) for \c perspective and
 * \c ortho, or [0, 1] (Vulkan and Direct3D) for the \c _zo variants. Every matrix
 * is column-major to match \c nexenne::math::matrix, so it uploads to the GPU
 * with no transpose. The field-of-view argument to \c perspective is the
 * *vertical* angle in radians; use the angle.hpp helpers to convert from degrees.
 */

#include <cmath>
#include <concepts>

#include <nexenne/math/matrix.hpp>

namespace nexenne::math {

/**
 * @brief Right-handed perspective projection with clip z in [-1, 1] (OpenGL).
 *
 * Builds the column-major view-to-clip matrix for a symmetric frustum with
 * vertical field of view \p fovy_rad. The viewer looks down -Z.
 *
 * @tparam Real Floating-point component type.
 * @param fovy_rad Vertical field of view, in radians.
 * @param aspect Aspect ratio (width divided by height).
 * @param near_z Distance to the near clip plane.
 * @param far_z Distance to the far clip plane.
 *
 * @return The 4x4 perspective projection matrix with clip depth in [-1, 1].
 *
 * @pre \p fovy_rad lies strictly within (0, pi).
 * @pre \p aspect is strictly positive.
 * @pre \p near_z and \p far_z are distinct, with \p far_z greater than \p near_z.
 * @post The returned matrix maps the frustum to the OpenGL clip cube.
 *
 * @note Not \c constexpr: it calls libm \c std::tan, which is not \c constexpr
 *       in C++23 (GCC accepts it as a builtin, clang does not). The orthographic
 *       builders use no transcendental and are \c constexpr.
 */
template <std::floating_point Real>
[[nodiscard]] auto perspective(
  Real const fovy_rad, Real const aspect, Real const near_z, Real const far_z
) noexcept -> matrix<Real, 4> {
  // f = cot(fovy/2) is the focal length: it scales y so a point at the top of the
  // frustum lands at clip y = 1, and f/aspect does the same for x. The m(3,2) =
  // -1 copies -z into the clip w, so the GPU's later divide by w performs the
  // perspective foreshortening. Row 2 remaps view-space z in [near, far] onto the
  // [-1, 1] clip range (hence the (near - far) denominator).
  auto const f{Real{1} / std::tan(fovy_rad / Real{2})};
  auto const range_inv{Real{1} / (near_z - far_z)};

  matrix<Real, 4> m{};
  m(0, 0) = f / aspect;
  m(1, 1) = f;
  m(2, 2) = (far_z + near_z) * range_inv;
  m(2, 3) = Real{2} * far_z * near_z * range_inv;
  m(3, 2) = Real{-1};
  return m;
}

/**
 * @brief Right-handed perspective projection with clip z in [0, 1] (Vulkan/D3D).
 *
 * The same symmetric frustum as \c perspective, but the depth range maps to
 * [0, 1] for Vulkan and Direct3D pipelines. The viewer looks down -Z.
 *
 * @tparam Real Floating-point component type.
 * @param fovy_rad Vertical field of view, in radians.
 * @param aspect Aspect ratio (width divided by height).
 * @param near_z Distance to the near clip plane.
 * @param far_z Distance to the far clip plane.
 *
 * @return The 4x4 perspective projection matrix with clip depth in [0, 1].
 *
 * @pre \p fovy_rad lies strictly within (0, pi).
 * @pre \p aspect is strictly positive.
 * @pre \p near_z and \p far_z are distinct, with \p far_z greater than \p near_z.
 * @post The returned matrix maps the frustum to the [0, 1] depth clip volume.
 *
 * @note Not \c constexpr: it calls libm \c std::tan (see \c perspective).
 */
template <std::floating_point Real>
[[nodiscard]] auto perspective_zo(
  Real const fovy_rad, Real const aspect, Real const near_z, Real const far_z
) noexcept -> matrix<Real, 4> {
  // Same as perspective() except row 2 maps z onto [0, 1] instead of [-1, 1], so
  // the depth coefficients lose the factor that produced the negative half.
  auto const f{Real{1} / std::tan(fovy_rad / Real{2})};
  auto const range_inv{Real{1} / (near_z - far_z)};

  matrix<Real, 4> m{};
  m(0, 0) = f / aspect;
  m(1, 1) = f;
  m(2, 2) = far_z * range_inv;
  m(2, 3) = far_z * near_z * range_inv;
  m(3, 2) = Real{-1};
  return m;
}

/**
 * @brief Right-handed orthographic projection with clip z in [-1, 1] (OpenGL).
 *
 * Builds the column-major view-to-clip matrix for the axis-aligned box bounded by
 * the six clip planes. The viewer looks down -Z.
 *
 * @tparam Real Floating-point component type.
 * @param left Left clip plane in view space.
 * @param right Right clip plane in view space.
 * @param bottom Bottom clip plane in view space.
 * @param top Top clip plane in view space.
 * @param near_z Distance to the near clip plane.
 * @param far_z Distance to the far clip plane.
 *
 * @return The 4x4 orthographic projection matrix with clip depth in [-1, 1].
 *
 * @pre \p left differs from \p right, \p bottom from \p top, \p near_z from
 *      \p far_z.
 * @post The returned matrix maps the box to the OpenGL clip cube.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ortho(
  Real const left,
  Real const right,
  Real const bottom,
  Real const top,
  Real const near_z,
  Real const far_z
) noexcept -> matrix<Real, 4> {
  // Orthographic is just an affine remap: scale each axis so the box edges land
  // on the clip-cube edges, then translate so the box center lands at the origin.
  // The diagonal carries the scales, the last column carries the translations.
  matrix<Real, 4> m{};
  m(0, 0) = Real{2} / (right - left);
  m(1, 1) = Real{2} / (top - bottom);
  m(2, 2) = Real{-2} / (far_z - near_z);
  m(0, 3) = -(right + left) / (right - left);
  m(1, 3) = -(top + bottom) / (top - bottom);
  m(2, 3) = -(far_z + near_z) / (far_z - near_z);
  m(3, 3) = Real{1};
  return m;
}

/**
 * @brief Right-handed orthographic projection with clip z in [0, 1] (Vulkan/D3D).
 *
 * The same axis-aligned box as \c ortho, but the depth range maps to [0, 1] for
 * Vulkan and Direct3D pipelines. The viewer looks down -Z.
 *
 * @tparam Real Floating-point component type.
 * @param left Left clip plane in view space.
 * @param right Right clip plane in view space.
 * @param bottom Bottom clip plane in view space.
 * @param top Top clip plane in view space.
 * @param near_z Distance to the near clip plane.
 * @param far_z Distance to the far clip plane.
 *
 * @return The 4x4 orthographic projection matrix with clip depth in [0, 1].
 *
 * @pre \p left differs from \p right, \p bottom from \p top, \p near_z from
 *      \p far_z.
 * @post The returned matrix maps the box to the [0, 1] depth clip volume.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ortho_zo(
  Real const left,
  Real const right,
  Real const bottom,
  Real const top,
  Real const near_z,
  Real const far_z
) noexcept -> matrix<Real, 4> {
  // As ortho() but the z axis maps onto [0, 1], so its scale and offset halve.
  matrix<Real, 4> m{};
  m(0, 0) = Real{2} / (right - left);
  m(1, 1) = Real{2} / (top - bottom);
  m(2, 2) = Real{-1} / (far_z - near_z);
  m(0, 3) = -(right + left) / (right - left);
  m(1, 3) = -(top + bottom) / (top - bottom);
  m(2, 3) = -near_z / (far_z - near_z);
  m(3, 3) = Real{1};
  return m;
}

}  // namespace nexenne::math
