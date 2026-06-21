#pragma once

/**
 * @file
 * @brief Explicit Euler-angle conventions and conversions to quaternion.
 *
 * "Euler angles" is ambiguous without a rotation order, so this header fixes the
 * convention as an explicit \c euler_order enum and converts to a \c quaternion
 * under each one. Orders follow the right-hand rule and are *intrinsic*
 * (body-fixed): \c xyz rotates about the body X axis first, then the once-rotated
 * Y, then the twice-rotated Z. Reverse the order for the extrinsic (world-fixed)
 * reading, so intrinsic \c xyz equals extrinsic \c zyx. All angles are in radians.
 * The aerospace yaw-pitch-roll convention is intrinsic \c zyx with z = yaw,
 * y = pitch, x = roll, exposed directly as \c from_ypr.
 */

#include <cmath>
#include <concepts>
#include <cstdint>

#include <nexenne/math/quaternion.hpp>

namespace nexenne::math {

/// @brief Intrinsic (body-fixed) rotation order for Euler-angle composition.
enum class euler_order : std::uint8_t {
  xyz,
  xzy,
  yxz,
  yzx,
  zxy,
  zyx
};

/**
 * @brief Per-axis rotation angles, in radians.
 *
 * Carries the three angles; pair it with an \c euler_order to give them meaning.
 * Constructed positionally as \c euler_angles{x, y, z} (pass 0 for an unused
 * axis) and read through the \c x() / \c y() / \c z() accessors; the angles are
 * fixed at construction.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class euler_angles {
public:
  using value_type = Real;  ///< The scalar component type.

private:
  value_type m_x{};
  value_type m_y{};
  value_type m_z{};

public:
  /**
   * @brief Constructs a zero rotation (all three angles zero).
   *
   * @pre None.
   * @post Every angle is zero.
   */
  constexpr euler_angles() noexcept = default;

  /**
   * @brief Constructs from the three per-axis angles, in radians.
   *
   * @param x Rotation about the X axis.
   * @param y Rotation about the Y axis.
   * @param z Rotation about the Z axis.
   *
   * @pre None.
   * @post The X, Y, and Z angles equal \p x, \p y, and \p z respectively.
   */
  constexpr euler_angles(value_type const x, value_type const y, value_type const z) noexcept
      : m_x{x}, m_y{y}, m_z{z} {}

  /**
   * @brief Accesses the rotation about the X axis, in radians.
   *
   * @return Const reference to the X angle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() const noexcept -> value_type const& {
    return m_x;
  }

  /**
   * @brief Accesses the rotation about the Y axis, in radians.
   *
   * @return Const reference to the Y angle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() const noexcept -> value_type const& {
    return m_y;
  }

  /**
   * @brief Accesses the rotation about the Z axis, in radians.
   *
   * @return Const reference to the Z angle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() const noexcept -> value_type const& {
    return m_z;
  }
};

namespace detail {

/**
 * @brief Builds the half-angle quaternion for a rotation about a unit basis axis.
 *
 * @tparam Real Floating-point component type.
 * @param angle Rotation angle in radians.
 * @param ax Axis x component (0 or 1 for a basis axis).
 * @param ay Axis y component.
 * @param az Axis z component.
 *
 * @return The unit quaternion for the rotation.
 *
 * @pre The axis (ax, ay, az) is unit length.
 * @post The result has unit length.
 */
template <std::floating_point Real>
[[nodiscard]] auto axis_quat(Real const angle, Real const ax, Real const ay, Real const az) noexcept
  -> quaternion<Real> {
  // A rotation by `angle` about the unit axis (ax, ay, az) is encoded as the
  // HALF-angle quaternion (sin(angle/2) * axis, cos(angle/2)). The half-angle is
  // what makes the sandwich product q * v * conjugate(q) rotate v by the full
  // angle (the q and conjugate(q) each contribute angle/2). See from_axis_angle
  // in quaternion.hpp for the full derivation.
  auto const half{angle * Real{0.5}};
  auto const s{std::sin(half)};
  // The quaternion ctor is (x, y, z, w); w is the scalar part cos(half).
  return quaternion<Real>{ax * s, ay * s, az * s, std::cos(half)};
}

}  // namespace detail

/**
 * @brief Converts Euler angles in the chosen order to a unit quaternion.
 *
 * Builds one half-angle quaternion per axis and composes them in the sequence
 * named by \p order. The composition is intrinsic: for \c xyz the product
 * \c qx * qy * qz applies the rotations as intrinsic x, then y, then z (each about
 * the previously rotated frame). Equivalently, since the Hamilton product acts
 * right to left, it rotates a vector about the fixed world z first, then y, then
 * x, which is the extrinsic zyx reading of the same orientation.
 *
 * @tparam Real Floating-point component type.
 * @param angles Per-axis rotation angles, in radians.
 * @param order Rotation order selecting how the per-axis rotations compose.
 *
 * @return The unit quaternion representing the combined rotation.
 *
 * @pre All components of \p angles are finite.
 * @post The result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_quaternion(euler_angles<Real> const angles, euler_order const order) noexcept
  -> quaternion<Real> {
  auto const qx{detail::axis_quat(angles.x(), Real{1}, Real{0}, Real{0})};
  auto const qy{detail::axis_quat(angles.y(), Real{0}, Real{1}, Real{0})};
  auto const qz{detail::axis_quat(angles.z(), Real{0}, Real{0}, Real{1})};

  // Why the product reads left-to-right for an INTRINSIC sequence. A rotation
  // about a body-fixed (already-rotated) axis post-multiplies the accumulated
  // orientation: if the frame is currently Q and we then turn about its own X by
  // qx, the new orientation is Q * qx. Starting from identity and applying
  // intrinsic x, then y, then z gives ((I * qx) * qy) * qz = qx*qy*qz - the same
  // order as written. (Equivalently, because the Hamilton product applies to a
  // vector right-to-left, qx*qy*qz rotates a vector about the fixed WORLD z
  // first, then y, then x, which is the extrinsic zyx reading of the same
  // orientation. Intrinsic xyz == extrinsic zyx.)
  // https://en.wikipedia.org/wiki/Davenport_chained_rotations#Intrinsic_rotations
  switch (order) {
    case euler_order::xyz:
      return qx * qy * qz;
    case euler_order::xzy:
      return qx * qz * qy;
    case euler_order::yxz:
      return qy * qx * qz;
    case euler_order::yzx:
      return qy * qz * qx;
    case euler_order::zxy:
      return qz * qx * qy;
    case euler_order::zyx:
      return qz * qy * qx;
  }
  return qx * qy * qz;
}

/**
 * @brief Aerospace yaw-pitch-roll to quaternion.
 *
 * Maps the three aerospace angles onto intrinsic \c zyx Euler angles (z = yaw,
 * y = pitch, x = roll) and delegates to \c to_quaternion.
 *
 * @tparam Real Floating-point component type.
 * @param yaw Rotation about Z, in radians.
 * @param pitch Rotation about Y, in radians.
 * @param roll Rotation about X, in radians.
 *
 * @return The unit quaternion representing the yaw-pitch-roll rotation.
 *
 * @pre \p yaw, \p pitch, and \p roll are finite.
 * @post The result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] auto
from_ypr(Real const yaw, Real const pitch, Real const roll) noexcept -> quaternion<Real> {
  return to_quaternion(euler_angles<Real>{roll, pitch, yaw}, euler_order::zyx);
}

}  // namespace nexenne::math
