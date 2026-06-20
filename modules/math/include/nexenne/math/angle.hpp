#pragma once

/**
 * @file
 * @brief Strong angle types and the algebra and trig built on them.
 *
 * The strong types \c radians and \c degrees prevent the most common angle-unit
 * bug: passing degrees to a function that wants radians (or vice versa). They
 * store a single floating-point value and are trivially copyable, so they pass
 * through registers exactly like the underlying primitive. All arithmetic is
 * \c constexpr and \c noexcept. The trig wrappers are \c noexcept but not yet
 * \c constexpr, because the underlying \c std::sin / \c std::cos / \c std::tan
 * are not \c constexpr in C++23.
 */

#include <cmath>
#include <compare>
#include <concepts>

#include <nexenne/math/constants.hpp>
#include <nexenne/math/scalar.hpp>

namespace nexenne::math {

/**
 * @brief Angle expressed in radians.
 *
 * Wraps a single floating-point value with explicit construction to prevent
 * silent conversion from a raw scalar.
 *
 * @tparam Real Floating-point type satisfying \c std::floating_point.
 */
template <std::floating_point Real>
class radians {
public:
  using value_type = Real;  ///< The underlying floating-point scalar type.

private:
  value_type m_value{};

public:
  /**
   * @brief Constructs a zero angle.
   *
   * @pre None.
   * @post The wrapped value is zero.
   */
  constexpr radians() noexcept = default;

  /**
   * @brief Constructs a radian angle from a raw value.
   *
   * @param v Angle value, in radians.
   *
   * @pre None.
   * @post The wrapped value equals \p v.
   */
  constexpr explicit radians(value_type const v) noexcept : m_value{v} {}

  /**
   * @brief Accesses the wrapped angle value.
   *
   * @return Const reference to the value, in radians.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type const& {
    return m_value;
  }

  /**
   * @brief Accesses the wrapped angle value for mutation.
   *
   * @return Mutable reference to the value, in radians.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() noexcept -> value_type& {
    return m_value;
  }

  /**
   * @brief Compares two radian angles by value.
   *
   * @param lhs First angle.
   * @param rhs Second angle.
   *
   * @return Three-way comparison result of the wrapped values.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(radians const& lhs, radians const& rhs) noexcept = default;
};

/**
 * @brief Angle expressed in degrees.
 *
 * Wraps a single floating-point value with explicit construction to prevent
 * silent conversion from a raw scalar.
 *
 * @tparam Real Floating-point type satisfying \c std::floating_point.
 */
template <std::floating_point Real>
class degrees {
public:
  using value_type = Real;  ///< The underlying floating-point scalar type.

private:
  value_type m_value{};

public:
  /**
   * @brief Constructs a zero angle.
   *
   * @pre None.
   * @post The wrapped value is zero.
   */
  constexpr degrees() noexcept = default;

  /**
   * @brief Constructs a degree angle from a raw value.
   *
   * @param v Angle value, in degrees.
   *
   * @pre None.
   * @post The wrapped value equals \p v.
   */
  constexpr explicit degrees(value_type const v) noexcept : m_value{v} {}

  /**
   * @brief Accesses the wrapped angle value.
   *
   * @return Const reference to the value, in degrees.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type const& {
    return m_value;
  }

  /**
   * @brief Accesses the wrapped angle value for mutation.
   *
   * @return Mutable reference to the value, in degrees.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() noexcept -> value_type& {
    return m_value;
  }

  /**
   * @brief Compares two degree angles by value.
   *
   * @param lhs First angle.
   * @param rhs Second angle.
   *
   * @return Three-way comparison result of the wrapped values.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(degrees const& lhs, degrees const& rhs) noexcept = default;
};

/**
 * @brief Sums two radian angles.
 *
 * @tparam Real Floating-point type.
 * @param lhs First angle.
 * @param rhs Second angle.
 *
 * @return Sum of the two angles, in radians.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator+(radians<Real> const lhs, radians<Real> const rhs) noexcept -> radians<Real> {
  return radians<Real>{lhs.value() + rhs.value()};
}

/**
 * @brief Subtracts two radian angles.
 *
 * @tparam Real Floating-point type.
 * @param lhs Minuend.
 * @param rhs Subtrahend.
 *
 * @return Difference of the two angles, in radians.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator-(radians<Real> const lhs, radians<Real> const rhs) noexcept -> radians<Real> {
  return radians<Real>{lhs.value() - rhs.value()};
}

/**
 * @brief Negates a radian angle.
 *
 * @tparam Real Floating-point type.
 * @param a Angle to negate.
 *
 * @return The negation of \p a.
 *
 * @pre None.
 * @post Magnitude is preserved, sign is flipped.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto operator-(radians<Real> const a) noexcept -> radians<Real> {
  return radians<Real>{-a.value()};
}

/**
 * @brief Scales a radian angle by a scalar.
 *
 * @tparam Real Floating-point type.
 * @param a Angle.
 * @param scalar Scaling factor.
 *
 * @return Scaled angle.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(radians<Real> const a, Real const scalar) noexcept -> radians<Real> {
  return radians<Real>{a.value() * scalar};
}

/**
 * @brief Scales a radian angle by a scalar (scalar on the left).
 *
 * @tparam Real Floating-point type.
 * @param scalar Scaling factor.
 * @param a Angle.
 *
 * @return Scaled angle.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(Real const scalar, radians<Real> const a) noexcept -> radians<Real> {
  return radians<Real>{scalar * a.value()};
}

/**
 * @brief Divides a radian angle by a scalar.
 *
 * @tparam Real Floating-point type.
 * @param a Angle.
 * @param scalar Divisor.
 *
 * @return Scaled angle.
 *
 * @pre \p scalar is non-zero and finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator/(radians<Real> const a, Real const scalar) noexcept -> radians<Real> {
  return radians<Real>{a.value() / scalar};
}

/**
 * @brief Ratio of two radian angles, as a dimensionless scalar.
 *
 * @tparam Real Floating-point type.
 * @param lhs Numerator angle.
 * @param rhs Denominator angle.
 *
 * @return \c lhs.value() / rhs.value().
 *
 * @pre \p rhs has a non-zero, finite value.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator/(radians<Real> const lhs, radians<Real> const rhs) noexcept -> Real {
  return lhs.value() / rhs.value();
}

// The same algebra for degrees.

/**
 * @brief Sums two degree angles.
 *
 * @tparam Real Floating-point type.
 * @param lhs First angle.
 * @param rhs Second angle.
 *
 * @return Sum of the two angles, in degrees.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator+(degrees<Real> const lhs, degrees<Real> const rhs) noexcept -> degrees<Real> {
  return degrees<Real>{lhs.value() + rhs.value()};
}

/**
 * @brief Subtracts two degree angles.
 *
 * @tparam Real Floating-point type.
 * @param lhs Minuend.
 * @param rhs Subtrahend.
 *
 * @return Difference of the two angles, in degrees.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator-(degrees<Real> const lhs, degrees<Real> const rhs) noexcept -> degrees<Real> {
  return degrees<Real>{lhs.value() - rhs.value()};
}

/**
 * @brief Negates a degree angle.
 *
 * @tparam Real Floating-point type.
 * @param a Angle to negate.
 *
 * @return The negation of \p a.
 *
 * @pre None.
 * @post Magnitude is preserved, sign is flipped.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto operator-(degrees<Real> const a) noexcept -> degrees<Real> {
  return degrees<Real>{-a.value()};
}

/**
 * @brief Scales a degree angle by a scalar.
 *
 * @tparam Real Floating-point type.
 * @param a Angle.
 * @param scalar Scaling factor.
 *
 * @return Scaled angle.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(degrees<Real> const a, Real const scalar) noexcept -> degrees<Real> {
  return degrees<Real>{a.value() * scalar};
}

/**
 * @brief Scales a degree angle by a scalar (scalar on the left).
 *
 * @tparam Real Floating-point type.
 * @param scalar Scaling factor.
 * @param a Angle.
 *
 * @return Scaled angle.
 *
 * @pre Both inputs are finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator*(Real const scalar, degrees<Real> const a) noexcept -> degrees<Real> {
  return degrees<Real>{scalar * a.value()};
}

/**
 * @brief Divides a degree angle by a scalar.
 *
 * @tparam Real Floating-point type.
 * @param a Angle.
 * @param scalar Divisor.
 *
 * @return Scaled angle.
 *
 * @pre \p scalar is non-zero and finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator/(degrees<Real> const a, Real const scalar) noexcept -> degrees<Real> {
  return degrees<Real>{a.value() / scalar};
}

/**
 * @brief Ratio of two degree angles, as a dimensionless scalar.
 *
 * @tparam Real Floating-point type.
 * @param lhs Numerator angle.
 * @param rhs Denominator angle.
 *
 * @return \c lhs.value() / rhs.value().
 *
 * @pre \p rhs has a non-zero, finite value.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
operator/(degrees<Real> const lhs, degrees<Real> const rhs) noexcept -> Real {
  return lhs.value() / rhs.value();
}

/**
 * @brief Converts degrees to radians.
 *
 * @tparam Real Floating-point type.
 * @param d Angle in degrees.
 *
 * @return The same angle expressed in radians.
 *
 * @pre \c d.value() is finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_radians(degrees<Real> const d) noexcept -> radians<Real> {
  return radians<Real>{d.value() * deg_to_rad_v<Real>};
}

/**
 * @brief Converts a raw scalar in degrees to radians.
 *
 * Convenience overload for code that has not yet promoted scalars to strong
 * types. Prefer the \c degrees overload at API boundaries.
 *
 * @tparam Real Floating-point type.
 * @param d Raw scalar in degrees.
 *
 * @return The same angle expressed in radians.
 *
 * @pre \p d is finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_radians(Real const d) noexcept -> radians<Real> {
  return radians<Real>{d * deg_to_rad_v<Real>};
}

/**
 * @brief Converts radians to degrees.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return The same angle expressed in degrees.
 *
 * @pre \c r.value() is finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_degrees(radians<Real> const r) noexcept -> degrees<Real> {
  return degrees<Real>{r.value() * rad_to_deg_v<Real>};
}

/**
 * @brief Converts a raw scalar in radians to degrees.
 *
 * Convenience overload for code that has not yet promoted scalars to strong
 * types. Prefer the \c radians overload at API boundaries.
 *
 * @tparam Real Floating-point type.
 * @param r Raw scalar in radians.
 *
 * @return The same angle expressed in degrees.
 *
 * @pre \p r is finite.
 * @post Result is finite.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_degrees(Real const r) noexcept -> degrees<Real> {
  return degrees<Real>{r * rad_to_deg_v<Real>};
}

// The trig wrappers are not yet constexpr because std::sin/cos/tan are runtime.

/**
 * @brief Sine of \p r.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return \c sin(r.value()).
 *
 * @pre \c r.value() is finite.
 * @post Result is in [-1, 1].
 */
template <std::floating_point Real>
[[nodiscard]] auto sin(radians<Real> const r) noexcept -> Real {
  return std::sin(r.value());
}

/**
 * @brief Cosine of \p r.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return \c cos(r.value()).
 *
 * @pre \c r.value() is finite.
 * @post Result is in [-1, 1].
 */
template <std::floating_point Real>
[[nodiscard]] auto cos(radians<Real> const r) noexcept -> Real {
  return std::cos(r.value());
}

/**
 * @brief Tangent of \p r.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return \c tan(r.value()).
 *
 * @pre \c r.value() is finite and not an odd multiple of pi/2.
 * @post Result is finite when the precondition holds.
 */
template <std::floating_point Real>
[[nodiscard]] auto tan(radians<Real> const r) noexcept -> Real {
  return std::tan(r.value());
}

/**
 * @brief Wraps an angle to the half-open interval [-pi, pi).
 *
 * Useful for keeping cumulative rotations bounded so that comparisons and
 * interpolation stay numerically well-behaved.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Angle in radians, wrapped to [-pi, pi).
 *
 * @pre \c r.value() is finite.
 * @post Result lies in [-pi, pi).
 *
 * @note Reduction is meaningful only while the input still resolves the period.
 *       Past about 1e15 radians the spacing between representable doubles exceeds
 *       2*pi, so no reduction can recover a precise residue and the result is
 *       correspondingly coarse (but the computation stays defined, no overflow).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto wrap_signed(radians<Real> const r) noexcept -> radians<Real> {
  // Floor-modulo over the period (via scalar::mod, which reduces with floor and
  // so handles any finite magnitude without the long long overflow a truncating
  // cast would have). Shift into [0, 2pi) first, reduce, then shift back onto
  // [-pi, pi). The final guard fixes the floating-point boundary case: when the
  // input is a hair below a +pi multiple, the reduced value can round up to
  // exactly +pi, the excluded endpoint, so subtract one period to keep it in
  // [-pi, pi).
  auto const two_pi{tau_v<Real>};
  auto wrapped{mod(r.value() + pi_v<Real>, two_pi) - pi_v<Real>};
  if (wrapped >= pi_v<Real>) {
    wrapped -= two_pi;
  } else if (wrapped < -pi_v<Real>) {
    wrapped += two_pi;
  }
  return radians<Real>{wrapped};
}

/**
 * @brief Wraps an angle to the half-open interval [0, 2*pi).
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Angle in radians, wrapped to [0, 2*pi).
 *
 * @pre \c r.value() is finite.
 * @post Result lies in [0, 2*pi).
 *
 * @note Reduction is meaningful only while the input still resolves the period;
 *       past about 1e15 radians the result is coarse (see \c wrap_signed), though
 *       still defined.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto wrap_unsigned(radians<Real> const r) noexcept -> radians<Real> {
  // Floor-modulo over the period (scalar::mod), with a boundary guard: for a tiny
  // negative input, mod(value, 2pi) = value + 2pi can round up to exactly 2pi
  // (the excluded upper bound) because value is below the ulp of 2pi, so subtract
  // one period to keep the result in [0, 2pi).
  auto const two_pi{tau_v<Real>};
  auto wrapped{mod(r.value(), two_pi)};
  if (wrapped >= two_pi) {
    wrapped -= two_pi;
  } else if (wrapped < Real{0}) {
    wrapped += two_pi;
  }
  return radians<Real>{wrapped};
}

/// @brief Single-precision angle in radians.
using radians_f = radians<float>;
/// @brief Double-precision angle in radians.
using radians_d = radians<double>;
/// @brief Single-precision angle in degrees.
using degrees_f = degrees<float>;
/// @brief Double-precision angle in degrees.
using degrees_d = degrees<double>;

}  // namespace nexenne::math
