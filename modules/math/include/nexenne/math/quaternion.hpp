#pragma once

/**
 * @file
 * @brief Unit quaternion for 3D rotation.
 *
 * Storage is \c (x, y, z, w) where \c w is the real (scalar) part and
 * \c (x, y, z) the vector part, matching most established math libraries.
 * Identity is the default value (\c w=1, vector part zero). Rotating a vector
 * \c v is \c q * v * conjugate(q); for a unit quaternion the inverse equals the
 * conjugate, which \c rotate exploits (and it uses an even cheaper identity, see
 * there). Composition is the Hamilton product, applied right to left: \c q1 * q2
 * applies \p q2 then \p q1.
 *
 * Why quaternions for rotation. They compose and interpolate without gimbal lock,
 * use four numbers instead of a nine-number matrix, and renormalize trivially
 * (divide by length), so accumulated drift is cheap to correct. The half-angle
 * encoding (\c from_axis_angle) is what makes \c slerp a clean great-circle path
 * on the unit hypersphere.
 *
 * Construction: a deduction guide lets \c quaternion{x, y, z, w} compile without
 * an explicit template argument; the aliases \c quaternion_f / \c quaternion_d
 * are there for explicit code. Use \c from_axis_angle / \c from_euler /
 * \c from_two_vectors / \c look_at_rotation to build a rotation; the fallible
 * ones return \c result so an invalid input cannot silently produce a NaN-laden
 * quaternion. The components are read-only after construction (no mutable
 * accessor): build through a constructor or a factory.
 */

#include <cmath>
#include <compare>
#include <concepts>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/error.hpp>
#include <nexenne/math/matrix.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/math/vector_algorithms.hpp>

namespace nexenne::math {

/**
 * @brief Quaternion for 3D rotation, stored as (x, y, z, w).
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class quaternion {
public:
  using value_type = Real;  ///< The scalar component type.

private:
  value_type m_x{};
  value_type m_y{};
  value_type m_z{};
  value_type m_w{value_type{1}};

public:
  /**
   * @brief Constructs the identity quaternion.
   *
   * @pre None.
   * @post The scalar part is 1 and the vector part is zero.
   */
  constexpr quaternion() noexcept = default;

  /**
   * @brief Constructs a quaternion from its four components.
   *
   * @param x Vector part x component.
   * @param y Vector part y component.
   * @param z Vector part z component.
   * @param w Real (scalar) part.
   *
   * @pre None.
   * @post The components equal \p x, \p y, \p z, and \p w respectively.
   */
  constexpr quaternion(
    value_type const x, value_type const y, value_type const z, value_type const w
  ) noexcept
      : m_x{x}, m_y{y}, m_z{z}, m_w{w} {}

  /**
   * @brief Accesses the vector part x component.
   *
   * @return Const reference to the x component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto x() const noexcept -> value_type const& {
    return m_x;
  }

  /**
   * @brief Accesses the vector part y component.
   *
   * @return Const reference to the y component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto y() const noexcept -> value_type const& {
    return m_y;
  }

  /**
   * @brief Accesses the vector part z component.
   *
   * @return Const reference to the z component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto z() const noexcept -> value_type const& {
    return m_z;
  }

  /**
   * @brief Accesses the real (scalar) part.
   *
   * @return Const reference to the w component.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto w() const noexcept -> value_type const& {
    return m_w;
  }

  /**
   * @brief Lexicographically compares two quaternions component by component.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return Three-way comparison result over the components in order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(quaternion const& lhs, quaternion const& rhs) noexcept = default;

  /**
   * @brief Returns the identity quaternion.
   *
   * @return The quaternion with scalar part 1 and zero vector part.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto identity() noexcept -> quaternion {
    return quaternion{};
  }
};

/// @brief Single-precision quaternion.
using quaternion_f = quaternion<float>;
/// @brief Double-precision quaternion.
using quaternion_d = quaternion<double>;

/// @brief Deduces \c quaternion<Real> from four same-typed components.
template <std::floating_point Real>
quaternion(Real, Real, Real, Real) -> quaternion<Real>;

static_assert(std::is_trivially_copyable_v<quaternion_f>);
static_assert(std::is_standard_layout_v<quaternion_f>);
static_assert(sizeof(quaternion_f) == 4 * sizeof(float));

/**
 * @brief Hamilton product of two quaternions (rotation composition).
 *
 * \c q1 * q2 is the rotation that applies \p q2 first, then \p q1. The four
 * output components come from expanding the product of (w1 + vector1) and
 * (w2 + vector2) under the rule i^2 = j^2 = k^2 = ijk = -1: the scalar part is
 * \c w1*w2 - dot(v1, v2), and the vector part is \c w1*v2 + w2*v1 + cross(v1, v2).
 *
 * @tparam Real Component type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The composed quaternion.
 *
 * @pre Both inputs have finite components.
 * @post The result has finite components.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(quaternion<Real> const a, quaternion<Real> const b) noexcept -> quaternion<Real> {
  return quaternion<Real>{
    a.w() * b.x() + a.x() * b.w() + a.y() * b.z() - a.z() * b.y(),
    a.w() * b.y() - a.x() * b.z() + a.y() * b.w() + a.z() * b.x(),
    a.w() * b.z() + a.x() * b.y() - a.y() * b.x() + a.z() * b.w(),
    a.w() * b.w() - a.x() * b.x() - a.y() * b.y() - a.z() * b.z(),
  };
}

/**
 * @brief Scalar multiplication (quaternion on the left).
 *
 * @tparam Real Component type.
 * @param q Quaternion to scale.
 * @param scalar Factor applied to every component.
 *
 * @return The quaternion with each component of \p q multiplied by \p scalar.
 *
 * @pre \p q and \p scalar are finite.
 * @post The result has finite components.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(quaternion<Real> const q, Real const scalar) noexcept -> quaternion<Real> {
  return quaternion<Real>{q.x() * scalar, q.y() * scalar, q.z() * scalar, q.w() * scalar};
}

/**
 * @brief Scalar multiplication (scalar on the left).
 *
 * @tparam Real Component type.
 * @param scalar Factor applied to every component.
 * @param q Quaternion to scale.
 *
 * @return The quaternion with each component of \p q multiplied by \p scalar.
 *
 * @pre \p q and \p scalar are finite.
 * @post The result equals \c q * scalar.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(Real const scalar, quaternion<Real> const q) noexcept -> quaternion<Real> {
  return q * scalar;
}

/**
 * @brief Component-wise addition (used by nlerp).
 *
 * @tparam Real Component type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The quaternion whose components are the sums of those of \p a and \p b.
 *
 * @pre \p a and \p b have finite components.
 * @post The result has finite components and is generally not unit length.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator+(quaternion<Real> const a, quaternion<Real> const b) noexcept -> quaternion<Real> {
  return quaternion<Real>{a.x() + b.x(), a.y() + b.y(), a.z() + b.z(), a.w() + b.w()};
}

/**
 * @brief Unary negation.
 *
 * Negates all four components. The result represents the *same* rotation as
 * \p q, because \c q and \c -q are antipodal points on the unit hypersphere that
 * map to the same orientation.
 *
 * @tparam Real Component type.
 * @param q Quaternion to negate.
 *
 * @return The quaternion with every component of \p q negated.
 *
 * @pre \p q has finite components.
 * @post The result has finite components and the same magnitude as \p q.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto operator-(quaternion<Real> const q) noexcept -> quaternion<Real> {
  return quaternion<Real>{-q.x(), -q.y(), -q.z(), -q.w()};
}

/**
 * @brief Component-wise subtraction.
 *
 * @tparam Real Component type.
 * @param a Minuend.
 * @param b Subtrahend.
 *
 * @return The quaternion whose components are the differences of \p a and \p b.
 *
 * @pre \p a and \p b have finite components.
 * @post The result has finite components and is generally not unit length.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator-(quaternion<Real> const a, quaternion<Real> const b) noexcept -> quaternion<Real> {
  return quaternion<Real>{a.x() - b.x(), a.y() - b.y(), a.z() - b.z(), a.w() - b.w()};
}

/**
 * @brief Component-wise in-place addition.
 *
 * @tparam Real Component type.
 * @param a Quaternion mutated in place (left operand and destination).
 * @param b Quaternion added to \p a.
 *
 * @return Reference to \p a after the addition.
 *
 * @pre \p a and \p b have finite components.
 * @post \p a holds the component-wise sum of its prior value and \p b.
 */
template <std::floating_point Real>
constexpr auto
operator+=(quaternion<Real>& a, quaternion<Real> const b) noexcept -> quaternion<Real>& {
  a = a + b;
  return a;
}

/**
 * @brief Component-wise in-place subtraction.
 *
 * @tparam Real Component type.
 * @param a Quaternion mutated in place (minuend and destination).
 * @param b Quaternion subtracted from \p a.
 *
 * @return Reference to \p a after the subtraction.
 *
 * @pre \p a and \p b have finite components.
 * @post \p a holds the component-wise difference of its prior value and \p b.
 */
template <std::floating_point Real>
constexpr auto
operator-=(quaternion<Real>& a, quaternion<Real> const b) noexcept -> quaternion<Real>& {
  a = a - b;
  return a;
}

/**
 * @brief In-place scalar multiplication.
 *
 * @tparam Real Component type.
 * @param q Quaternion mutated in place.
 * @param scalar Factor applied to every component.
 *
 * @return Reference to \p q after scaling.
 *
 * @pre \p q and \p scalar are finite.
 * @post Every component of \p q is multiplied by \p scalar.
 */
template <std::floating_point Real>
constexpr auto operator*=(quaternion<Real>& q, Real const scalar) noexcept -> quaternion<Real>& {
  q = q * scalar;
  return q;
}

/**
 * @brief In-place Hamilton product (rotation composition).
 *
 * Equivalent to \c a = a*b, so \p a is post-multiplied by \p b.
 *
 * @tparam Real Component type.
 * @param a Quaternion mutated in place (left operand and destination).
 * @param b Right operand, applied first when rotating.
 *
 * @return Reference to \p a after the multiplication.
 *
 * @pre \p a and \p b have finite components.
 * @post \p a holds the Hamilton product of its prior value and \p b.
 */
template <std::floating_point Real>
constexpr auto
operator*=(quaternion<Real>& a, quaternion<Real> const b) noexcept -> quaternion<Real>& {
  a = a * b;
  return a;
}

/**
 * @brief Dot product of two quaternions treated as 4D vectors.
 *
 * @tparam Real Component type.
 * @param a Left operand.
 * @param b Right operand.
 *
 * @return The sum of the products of corresponding components.
 *
 * @pre \p a and \p b have finite components.
 * @post The result is finite; it equals \c cos(theta) when both inputs are unit
 *       length, where \c theta is the angle between them on the hypersphere.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
dot(quaternion<Real> const a, quaternion<Real> const b) noexcept -> Real {
  return a.x() * b.x() + a.y() * b.y() + a.z() * b.z() + a.w() * b.w();
}

/**
 * @brief Squared norm.
 *
 * Avoids the square root in \c length; prefer it for magnitude comparisons.
 *
 * @tparam Real Component type.
 * @param q Quaternion to measure.
 *
 * @return The sum of the squares of the four components.
 *
 * @pre \p q has finite components.
 * @post The result is finite and non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto length_squared(quaternion<Real> const q) noexcept -> Real {
  return dot(q, q);
}

/**
 * @brief Euclidean norm.
 *
 * @tparam Real Component type.
 * @param q Quaternion to measure.
 *
 * @return The square root of \c length_squared(q).
 *
 * @pre \p q has finite components.
 * @post The result is finite and non-negative; it is 1 for a unit quaternion.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto length(quaternion<Real> const q) noexcept -> Real {
  return sqrt(length_squared(q));
}

/**
 * @brief Scales \p q to unit length.
 *
 * @tparam Real Component type.
 * @param q Quaternion to normalize.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return The unit quaternion, or \c math_error::zero_length_vector when \p q is
 *         too short.
 *
 * @pre \p q has finite components.
 * @post On success the result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto normalize(
  quaternion<Real> const q, Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<quaternion<Real>> {
  auto const len_sq{length_squared(q)};
  if (len_sq <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  return q * (Real{1} / sqrt(len_sq));
}

/**
 * @brief Conjugate: negate the vector part, keep the scalar part.
 *
 * For a unit quaternion the conjugate equals the inverse (it represents the
 * opposite rotation).
 *
 * @tparam Real Component type.
 * @param q Quaternion to conjugate.
 *
 * @return The quaternion with the vector part of \p q negated.
 *
 * @pre \p q has finite components.
 * @post The result has the same magnitude as \p q.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto conjugate(quaternion<Real> const q) noexcept -> quaternion<Real> {
  return quaternion<Real>{-q.x(), -q.y(), -q.z(), q.w()};
}

/**
 * @brief Inverse: \c conjugate(q) / length_squared(q).
 *
 * Equal to \c conjugate(q) when \p q is unit length, but correct for any
 * magnitude (the conjugate undoes the rotation, the division undoes the scale).
 *
 * @tparam Real Component type.
 * @param q Quaternion to invert.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return The inverse quaternion, or \c math_error::zero_length_vector when \p q
 *         is too short.
 *
 * @pre \p q has finite components.
 * @post On success \c q * result equals the identity up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto inverse(
  quaternion<Real> const q, Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<quaternion<Real>> {
  auto const len_sq{length_squared(q)};
  if (len_sq <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  return conjugate(q) * (Real{1} / len_sq);
}

/**
 * @brief Builds a unit quaternion from an axis and an angle.
 *
 * A rotation by angle \c theta about a unit axis \c n is encoded with the
 * *half* angle: \c q = (sin(theta/2) * n, cos(theta/2)). The half-angle is what
 * makes \c q * v * conjugate(q) apply the full rotation, and what makes \c slerp
 * a constant-speed great-circle path. The axis is validated and normalized
 * first, so an invalid axis returns an error rather than a NaN quaternion.
 *
 * @tparam Real Component type.
 * @param axis Rotation axis. Need not be unit.
 * @param angle Rotation angle.
 * @param threshold Minimum squared length of the axis to accept. Default 1e-20.
 *
 * @return The unit quaternion, or \c math_error::zero_length_vector when \p axis
 *         is too short.
 *
 * @pre \c angle.value() is finite.
 * @post On success the result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] auto from_axis_angle(
  vector<Real, 3> const axis,
  radians<Real> const angle,
  Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<quaternion<Real>> {
  auto const unit_axis{normalize(axis, threshold)};
  if (!unit_axis) {
    return std::unexpected{unit_axis.error()};
  }
  auto const half{angle.value() * Real{0.5}};
  auto const s{std::sin(half)};
  auto const c{std::cos(half)};
  return quaternion<Real>{unit_axis->x() * s, unit_axis->y() * s, unit_axis->z() * s, c};
}

/**
 * @brief Builds a unit quaternion from intrinsic Tait-Bryan angles (Z-Y-X).
 *
 * Applies \p yaw about Z, then \p pitch about the new Y, then \p roll about the
 * new X (the aerospace convention). The closed form below is the expanded
 * product of the three half-angle axis quaternions.
 *
 * @tparam Real Component type.
 * @param roll Rotation about X.
 * @param pitch Rotation about Y.
 * @param yaw Rotation about Z.
 *
 * @return The unit quaternion representing the combined rotation.
 *
 * @pre All three angles are finite.
 * @post The result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] auto from_euler(
  radians<Real> const roll, radians<Real> const pitch, radians<Real> const yaw
) noexcept -> quaternion<Real> {
  auto const cr{std::cos(roll.value() * Real{0.5})};
  auto const sr{std::sin(roll.value() * Real{0.5})};
  auto const cp{std::cos(pitch.value() * Real{0.5})};
  auto const sp{std::sin(pitch.value() * Real{0.5})};
  auto const cy{std::cos(yaw.value() * Real{0.5})};
  auto const sy{std::sin(yaw.value() * Real{0.5})};

  // This is the expanded Hamilton product q = q_yaw(Z) * q_pitch(Y) * q_roll(X),
  // i.e. intrinsic Z-Y-X (aerospace), where each q_axis is the half-angle
  // quaternion (sin(a/2)*axis, cos(a/2)). Multiplying the three single-axis
  // quaternions out and collecting terms gives the eight products below; for
  // example the scalar part w = cr*cp*cy + sr*sp*sy and the x part picks up
  // +sr*cp*cy (roll about X) minus the cross term cr*sp*sy. Each component has
  // exactly one sign that differs from its neighbours - that sign pattern is the
  // whole content of the formula and the usual place a hand-port goes wrong, so
  // it is laid out explicitly rather than via three quaternion multiplies.
  // https://en.wikipedia.org/wiki/Conversion_between_quaternions_and_Euler_angles#Euler_angles_(in_3-2-1_sequence)_to_quaternion_conversion
  return quaternion<Real>{
    sr * cp * cy - cr * sp * sy,
    cr * sp * cy + sr * cp * sy,
    cr * cp * sy - sr * sp * cy,
    cr * cp * cy + sr * sp * sy,
  };
}

/**
 * @brief Builds the shortest-arc rotation that takes \p from onto \p to.
 *
 * The half-vector method: the unit bisector \c h = normalize(from + to) sits at
 * half the angle between the inputs, so the rotation is
 * \c (cross(from, h), dot(from, h)), already unit length. This is stable across
 * the whole range, unlike the \c (cross(from, to), 1 + dot(from, to)) form whose
 * \c sqrt(2(1+d)) normalization loses precision as the angle approaches pi (it is
 * off by ~1e-3 near 180 degrees). The genuinely antipodal case (\p from = -to,
 * where the bisector vanishes) is handled by rotating by pi about any axis
 * orthogonal to \p from.
 *
 * @tparam Real Floating-point component type.
 * @param from Source direction. Need not be unit.
 * @param to Target direction. Need not be unit.
 *
 * @return The unit quaternion rotating \p from onto \p to, or
 *         \c math_error::zero_length_vector when either input is too short.
 *
 * @pre \p from and \p to have finite components.
 * @post On success the result has unit length and rotates \p from onto \p to.
 */
template <std::floating_point Real>
[[nodiscard]] auto from_two_vectors(vector<Real, 3> const from, vector<Real, 3> const to) noexcept
  -> result<quaternion<Real>> {
  auto const from_unit{normalize(from)};
  if (!from_unit) {
    return std::unexpected{from_unit.error()};
  }
  auto const to_unit{normalize(to)};
  if (!to_unit) {
    return std::unexpected{to_unit.error()};
  }
  auto const f{*from_unit};
  auto const t{*to_unit};
  auto const half{f + t};
  auto const half_len_sq{length_squared(half)};
  // half_len_sq = 2(1 + dot) vanishes only when from and to are antipodal, where
  // the bisector and the rotation axis are undefined.
  if (half_len_sq < static_cast<Real>(1e-12)) {
    // Pick any axis orthogonal to from and rotate by pi. Cross with X unless from
    // is itself along X, in which case cross with Y.
    auto axis{cross(vector<Real, 3>{Real{1}, Real{0}, Real{0}}, f)};
    if (length_squared(axis) < static_cast<Real>(1e-12)) {
      axis = cross(vector<Real, 3>{Real{0}, Real{1}, Real{0}}, f);
    }
    auto const unit_axis{*normalize(axis)};
    // A 180-degree turn about a unit axis n is the half-angle quaternion
    // (sin(90)*n, cos(90)) = (n, 0): a pure quaternion with zero scalar part.
    // Because the axis is orthogonal to `from`, rotating `from` by pi about it
    // sends it to -from = to, as required.
    return quaternion<Real>{unit_axis.x(), unit_axis.y(), unit_axis.z(), Real{0}};
  }
  auto const h{half * (Real{1} / sqrt(half_len_sq))};  // unit bisector
  auto const c{cross(f, h)};
  return quaternion<Real>{c.x(), c.y(), c.z(), dot(f, h)};
}

/**
 * @brief An axis and an angle: the geometric form of a rotation.
 *
 * Returned by \c to_axis_angle. For the identity rotation the axis defaults to
 * +X and the angle is zero. Read-only after construction.
 *
 * @tparam Real Floating-point component type.
 */
template <std::floating_point Real>
class axis_angle {
public:
  using value_type = Real;  ///< The scalar component type.

private:
  vector<Real, 3> m_axis{Real{1}, Real{0}, Real{0}};
  radians<Real> m_angle{};

public:
  /**
   * @brief Constructs the default axis-angle (the +X axis, zero angle).
   *
   * @pre None.
   * @post The axis is +X and the angle is zero.
   */
  constexpr axis_angle() noexcept = default;

  /**
   * @brief Constructs an axis-angle from its parts.
   *
   * @param axis Rotation axis.
   * @param angle Rotation angle in radians.
   *
   * @pre None.
   * @post The axis equals \p axis and the angle equals \p angle.
   */
  constexpr axis_angle(vector<Real, 3> const axis, radians<Real> const angle) noexcept
      : m_axis{axis}, m_angle{angle} {}

  /**
   * @brief Accesses the rotation axis.
   *
   * @return Const reference to the rotation axis.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto axis() const noexcept -> vector<Real, 3> const& {
    return m_axis;
  }

  /**
   * @brief Accesses the rotation angle.
   *
   * @return Const reference to the rotation angle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto angle() const noexcept -> radians<Real> const& {
    return m_angle;
  }
};

/**
 * @brief Extracts the axis-angle representation of \p q.
 *
 * Inverts \c from_axis_angle: the angle is \c 2*acos(w) and the axis is the
 * vector part divided by \c sin(theta/2). Robust for any finite input: \c w is
 * clamped to [-1, 1] before \c acos (rounding can push a unit quaternion's w just
 * outside), and near the identity (where \c sin(theta/2) is tiny and the axis is
 * geometrically arbitrary) it falls back to +X. It therefore cannot produce NaN,
 * so it returns the bare value rather than a \c result.
 *
 * @tparam Real Floating-point component type.
 * @param q Quaternion (ideally unit length).
 *
 * @return The axis-angle pair. The axis is unit when the angle is non-zero.
 *
 * @pre \p q has finite components.
 * @post For an identity \p q the angle is zero and the axis is +X.
 */
template <std::floating_point Real>
[[nodiscard]] auto to_axis_angle(quaternion<Real> const q) noexcept -> axis_angle<Real> {
  auto const w_clamped{clamp(q.w(), Real{-1}, Real{1})};
  auto const angle{Real{2} * std::acos(w_clamped)};
  // sin_half = sin(theta/2) = |vector part| = sqrt(1 - w^2). The axis = vector/sin_half
  // is ill-defined only where sin_half ~ 0, i.e. w ~ +/-1 -> theta ~ 0 or 2*pi
  // (both the identity rotation up to sign); there the axis is arbitrary, so we
  // return +X. Note the antipodal end w ~ -1 is covered by the same test. Near
  // theta = pi the axis is perfectly well conditioned (sin_half ~ 1), so it needs
  // no special case despite being the "halfway" angle.
  auto const sin_half{sqrt(Real{1} - w_clamped * w_clamped)};
  if (sin_half < static_cast<Real>(1e-8)) {
    return axis_angle<Real>{vector<Real, 3>{Real{1}, Real{0}, Real{0}}, radians<Real>{angle}};
  }
  auto const inv{Real{1} / sin_half};
  return axis_angle<Real>{
    vector<Real, 3>{q.x() * inv, q.y() * inv, q.z() * inv},
    radians<Real>{angle},
  };
}

/**
 * @brief Builds a quaternion that orients -Z toward \p forward.
 *
 * Constructs a right-handed orthonormal basis (right, up, back = -forward) by
 * Gram-Schmidt from \p forward and \p up, so the resulting rotation maps the -Z
 * axis onto \p forward. It then converts that rotation matrix to a quaternion with
 * the standard trace formula (which picks the largest of the four
 * \c 1 +/- trace combinations to keep the square root well away from zero).
 * Both inputs are validated.
 *
 * @tparam Real Floating-point component type.
 * @param forward Desired forward direction. Need not be unit.
 * @param up World-space up direction. Need not be unit.
 *
 * @return The unit orientation quaternion;
 *         \c math_error::zero_length_vector when \p forward is too short, or
 *         \c math_error::parallel_vectors when \p up is parallel to \p forward.
 *
 * @pre \p forward and \p up have finite components and are not parallel.
 * @post On success the result has unit length and orients -Z toward \p forward.
 */
template <std::floating_point Real>
[[nodiscard]] auto look_at_rotation(
  vector<Real, 3> const forward, vector<Real, 3> const up
) noexcept -> result<quaternion<Real>> {
  auto const forward_unit{normalize(forward)};
  if (!forward_unit) {
    return std::unexpected{math_error::zero_length_vector};
  }
  auto const f{*forward_unit};
  // Right-handed camera basis: -Z is forward, so the +Z column is -forward
  // (\c back). The image of Z under the rotation is therefore back = -f, which
  // makes rotate(q, -Z) land on f.
  auto const back{-f};
  auto const right{normalize(cross(up, back))};
  if (!right) {
    return std::unexpected{math_error::parallel_vectors};
  }
  auto const r{*right};
  auto const u{cross(back, r)};

  // The orthonormal basis as a rotation matrix (columns r, u, back), converted to
  // a quaternion by the Shepperd/trace method. Read off the to_matrix3 form
  // backwards: the off-diagonal differences recover w times each axis,
  //   m21 - m12 = 4*w*x,  m02 - m20 = 4*w*y,  m10 - m01 = 4*w*z,
  // and the trace gives w directly, trace = 3 - 4(x^2+y^2+z^2) = 4*w^2 - 1, so
  // s = 2*sqrt(trace+1) = 4*w and w = s/4 = 0.25*s, x = (m21-m12)/s, etc. That
  // division by w is unstable when w ~ 0, so each branch instead solves for the
  // component (w, x, y, or z) that the diagonal shows is largest in magnitude,
  // keeping the divisor s = 4*|largest| safely away from zero; the other three
  // components follow from the corresponding sums/differences.
  // https://en.wikipedia.org/wiki/Rotation_matrix#Quaternion
  auto const m00{r.x()};
  auto const m01{u.x()};
  auto const m02{back.x()};
  auto const m10{r.y()};
  auto const m11{u.y()};
  auto const m12{back.y()};
  auto const m20{r.z()};
  auto const m21{u.z()};
  auto const m22{back.z()};

  auto const trace{m00 + m11 + m22};
  if (trace > Real{0}) {
    auto const s{sqrt(trace + Real{1}) * Real{2}};
    auto const inv{Real{1} / s};
    return quaternion<Real>{
      (m21 - m12) * inv, (m02 - m20) * inv, (m10 - m01) * inv, Real{0.25} * s
    };
  }
  if (m00 > m11 && m00 > m22) {
    auto const s{sqrt(Real{1} + m00 - m11 - m22) * Real{2}};
    auto const inv{Real{1} / s};
    return quaternion<Real>{
      Real{0.25} * s, (m01 + m10) * inv, (m02 + m20) * inv, (m21 - m12) * inv
    };
  }
  if (m11 > m22) {
    auto const s{sqrt(Real{1} + m11 - m00 - m22) * Real{2}};
    auto const inv{Real{1} / s};
    return quaternion<Real>{
      (m01 + m10) * inv, Real{0.25} * s, (m12 + m21) * inv, (m02 - m20) * inv
    };
  }
  auto const s{sqrt(Real{1} + m22 - m00 - m11) * Real{2}};
  auto const inv{Real{1} / s};
  return quaternion<Real>{(m02 + m20) * inv, (m12 + m21) * inv, Real{0.25} * s, (m10 - m01) * inv};
}

/**
 * @brief Converts a unit quaternion \p q to a 3x3 rotation matrix.
 *
 * Applies the standard quaternion-to-matrix expansion: each entry is built from
 * pairwise products of the components, using the unit-length identity to drop the
 * \c w^2 terms.
 *
 * @tparam Real Component type.
 * @param q Unit quaternion to convert.
 *
 * @return The equivalent 3x3 rotation matrix.
 *
 * @pre \p q has unit length.
 * @post The result is a proper rotation (orthonormal, determinant 1) when the
 *       precondition holds.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_matrix3(quaternion<Real> const q) noexcept -> matrix<Real, 3> {
  // Derivation. Writing the rotation R = q*v*conjugate(q) out in components gives,
  // for a quaternion (x, y, z, w), the diagonal entry R00 = w^2 + x^2 - y^2 - z^2
  // and off-diagonals like R01 = 2(xy - wz), R10 = 2(xy + wz) (the wz terms carry
  // the rotation's handedness; they flip sign across the diagonal). For a UNIT
  // quaternion w^2 + x^2 + y^2 + z^2 = 1, so w^2 + x^2 - y^2 - z^2 = 1 - 2(y^2+z^2):
  // that substitution is what removes w from the diagonal and leaves the familiar
  // 1 - 2(...) form below. Each named product (xx, wz, ...) is computed once and
  // reused across the (i,j) and (j,i) pair it appears in.
  // https://en.wikipedia.org/wiki/Quaternions_and_spatial_rotation#From_a_quaternion_to_an_orthogonal_matrix
  auto const xx{q.x() * q.x()};
  auto const yy{q.y() * q.y()};
  auto const zz{q.z() * q.z()};
  auto const xy{q.x() * q.y()};
  auto const xz{q.x() * q.z()};
  auto const yz{q.y() * q.z()};
  auto const wx{q.w() * q.x()};
  auto const wy{q.w() * q.y()};
  auto const wz{q.w() * q.z()};

  return make_matrix3<Real>(
    Real{1} - Real{2} * (yy + zz),
    Real{2} * (xy - wz),
    Real{2} * (xz + wy),
    Real{2} * (xy + wz),
    Real{1} - Real{2} * (xx + zz),
    Real{2} * (yz - wx),
    Real{2} * (xz - wy),
    Real{2} * (yz + wx),
    Real{1} - Real{2} * (xx + yy)
  );
}

/**
 * @brief Converts a unit quaternion \p q to a 4x4 rotation matrix.
 *
 * The 3x3 rotation in the upper-left block, identity elsewhere (no translation).
 *
 * @tparam Real Component type.
 * @param q Unit quaternion to convert.
 *
 * @return The 4x4 homogeneous rotation matrix.
 *
 * @pre \p q has unit length.
 * @post The upper-left 3x3 block is a proper rotation; the last row and column
 *       are the identity's when the precondition holds.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_matrix4(quaternion<Real> const q) noexcept -> matrix<Real, 4> {
  auto const m3{to_matrix3(q)};
  auto m{matrix<Real, 4>::identity()};
  for (std::size_t r{0}; r < 3; ++r) {
    for (std::size_t c{0}; c < 3; ++c) {
      m(r, c) = m3(r, c);
    }
  }
  return m;
}

/**
 * @brief Rotates \p v by the unit quaternion \p q.
 *
 * Uses the optimized identity \c v' = v + 2*w*(qv x v) + 2*qv x (qv x v), where
 * \c qv is the vector part. It expands \c q * v * conjugate(q) and cancels terms
 * using the unit-length constraint, so it costs two cross products instead of two
 * full quaternion multiplies.
 *
 * @tparam Real Component type.
 * @param q Unit quaternion.
 * @param v Vector to rotate.
 *
 * @return The rotated vector.
 *
 * @pre \p q has unit length.
 * @post The result is \p v rotated by \p q, preserving length when the
 *       precondition holds.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
rotate(quaternion<Real> const q, vector<Real, 3> const v) noexcept -> vector<Real, 3> {
  auto const qv{vector<Real, 3>{q.x(), q.y(), q.z()}};
  auto const t{Real{2} * cross(qv, v)};
  return v + q.w() * t + cross(qv, t);
}

/**
 * @brief Normalized linear interpolation between two unit quaternions.
 *
 * Linearly interpolates the four components and renormalizes. Cheap (no trig),
 * but the angular velocity is not constant across the arc; the artifact is
 * invisible for small arcs. Takes the shorter path by negating \p b when its dot
 * with \p a is negative.
 *
 * @tparam Real Component type.
 * @param a Start orientation.
 * @param b End orientation.
 * @param t Interpolation parameter in [0, 1].
 *
 * @return The normalized linear interpolation.
 *
 * @pre Both inputs are unit length.
 * @post The result has unit length up to rounding.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto nlerp(
  quaternion<Real> const a, quaternion<Real> const b, Real const t
) noexcept -> quaternion<Real> {
  auto const shortest{dot(a, b) < Real{0} ? -b : b};
  auto const mixed{a * (Real{1} - t) + shortest * t};
  // mixed is a convex blend of two same-hemisphere unit quaternions, so it is
  // never zero and the normalize always succeeds; value_or is belt-and-braces.
  return normalize(mixed).value_or(mixed);
}

/**
 * @brief Spherical linear interpolation between two unit quaternions.
 *
 * Constant angular velocity: it walks the great-circle arc between \p a and \p b
 * at a steady rate, weighting the endpoints by \c sin((1-t)*theta)/sin(theta) and
 * \c sin(t*theta)/sin(theta). Takes the shorter arc (negating \p b when needed),
 * and falls back to \c nlerp when the rotations are nearly aligned, where
 * \c sin(theta) is tiny and the division would be unstable.
 *
 * @tparam Real Component type.
 * @param a Start orientation.
 * @param b End orientation.
 * @param t Interpolation parameter in [0, 1].
 *
 * @return The spherically interpolated quaternion.
 *
 * @pre Both inputs are unit length.
 * @post The result has unit length up to rounding and follows the shorter arc.
 */
template <std::floating_point Real>
[[nodiscard]] auto slerp(quaternion<Real> const a, quaternion<Real> const b, Real const t) noexcept
  -> quaternion<Real> {
  auto cos_theta{dot(a, b)};
  auto target{b};
  if (cos_theta < Real{0}) {
    target = -b;
    cos_theta = -cos_theta;
  }
  if (cos_theta > static_cast<Real>(0.9995)) {
    return nlerp(a, target, t);
  }
  auto const theta{std::acos(cos_theta)};
  auto const sin_theta{std::sin(theta)};
  auto const wa{std::sin((Real{1} - t) * theta) / sin_theta};
  auto const wb{std::sin(t * theta) / sin_theta};
  return a * wa + target * wb;
}

}  // namespace nexenne::math
