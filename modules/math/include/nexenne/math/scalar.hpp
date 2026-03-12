#pragma once

/**
 * @file
 * @brief Scalar math utilities for integral and floating-point types.
 *
 * Sections: selection (min, max, clamp, saturate, step, move_toward); sign and
 * magnitude (abs, sign, copysign); powers (square, cube, with sqrt and friends
 * in power.hpp); interpolation (lerp, inverse_lerp, remap, smoothstep); equality
 * and classification (almost_equal, approximately_zero, isnan, isinf, isfinite);
 * rounding (trunc, floor, ceil, round, fract); and modulo and wrapping (mod,
 * repeat, ping_pong, wrap).
 *
 * Where it makes sense functions are constrained on \c arithmetic (integer plus
 * floating point) or \c signed_arithmetic; transcendental and floating-only
 * operations stay on \c std::floating_point. All functions are \c constexpr and
 * \c noexcept; rounding falls back to libm at runtime via \c if \c consteval,
 * since the standard does not make \c std::floor and friends \c constexpr until
 * C++26.
 */

#include <cmath>
#include <concepts>
#include <limits>

#include <nexenne/math/concepts.hpp>

namespace nexenne::math {

/**
 * @brief Smaller of two values.
 *
 * Takes both operands by value (cheaper than \c std::min for arithmetic types,
 * which takes them by reference) and returns by value.
 *
 * @tparam Value Arithmetic type.
 * @param a First value.
 * @param b Second value.
 *
 * @return \p a when \c a<b, else \p b.
 *
 * @pre Neither value is NaN, otherwise the choice is implementation-defined.
 * @post Result equals \p a or \p b.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto min(Value const a, Value const b) noexcept -> Value {
  return a < b ? a : b;
}

/**
 * @brief Larger of two values.
 *
 * @tparam Value Arithmetic type.
 * @param a First value.
 * @param b Second value.
 *
 * @return \p a when \c a>b, else \p b.
 *
 * @pre Neither value is NaN, otherwise the choice is implementation-defined.
 * @post Result equals \p a or \p b.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto max(Value const a, Value const b) noexcept -> Value {
  return a > b ? a : b;
}

/**
 * @brief Clamps \p value to the closed interval [\p lo, \p hi].
 *
 * NaN inputs are propagated for floating-point types: the comparisons return
 * false in both directions and \p value is returned unchanged.
 *
 * @tparam Value Arithmetic type.
 * @param value The value to clamp.
 * @param lo Lower bound of the interval, inclusive.
 * @param hi Upper bound of the interval, inclusive.
 *
 * @return \p value, \p lo, or \p hi.
 *
 * @pre \p lo is less than or equal to \p hi.
 * @post Returned value lies in [\p lo, \p hi] when neither bound is NaN.
 *
 * @warning Behavior is unspecified when \p lo is greater than \p hi.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto
clamp(Value const value, Value const lo, Value const hi) noexcept -> Value {
  return value < lo ? lo : (value > hi ? hi : value);
}

/**
 * @brief Clamps \p value to the closed interval [0, 1].
 *
 * Equivalent to \c clamp(value, 0, 1).
 *
 * @tparam Real Floating-point type.
 * @param value The value to clamp.
 *
 * @return \p value clamped to [0, 1].
 *
 * @pre None.
 * @post Returned value lies in [0, 1] when \p value is not NaN.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto saturate(Real const value) noexcept -> Real {
  return clamp(value, Real{0}, Real{1});
}

/**
 * @brief Step function.
 *
 * Returns 0 when \p value is strictly less than \p edge, and 1 otherwise.
 *
 * @tparam Real Floating-point type.
 * @param edge The threshold.
 * @param value The value to test.
 *
 * @return 0 or 1.
 *
 * @pre Neither input is NaN.
 * @post Result is exactly 0 or exactly 1.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto step(Real const edge, Real const value) noexcept -> Real {
  return value < edge ? Real{0} : Real{1};
}

/**
 * @brief Moves \p current toward \p target by at most \p max_delta.
 *
 * Frame-rate-independent equivalent of \c lerp for "approach a target at a fixed
 * maximum speed". The result is \p target when within \p max_delta, otherwise
 * \p current adjusted by \p max_delta toward \p target.
 *
 * @tparam Real Floating-point type.
 * @param current Current value.
 * @param target Target value.
 * @param max_delta Maximum step magnitude. Must be non-negative.
 *
 * @return Updated value.
 *
 * @pre \p max_delta is finite and non-negative.
 * @post Result is between \p current and \p target inclusive.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
move_toward(Real const current, Real const target, Real const max_delta) noexcept -> Real {
  // A non-positive step means "do not move": without this guard a negative
  // max_delta would step the wrong way (current - |max_delta|), away from target.
  if (max_delta <= Real{0}) {
    return current;
  }
  auto const delta{target - current};
  auto const distance{delta < Real{0} ? -delta : delta};
  if (distance <= max_delta) {
    return target;
  }
  return current + (delta < Real{0} ? -max_delta : max_delta);
}

/**
 * @brief Absolute value of \p value.
 *
 * @tparam Value Signed arithmetic type.
 * @param value Input value.
 *
 * @return The magnitude of \p value.
 *
 * @pre None.
 * @post Result is non-negative when \p value is not NaN.
 */
template <signed_arithmetic Value>
[[nodiscard]] constexpr auto abs(Value const value) noexcept -> Value {
  return value < Value{0} ? -value : value;
}

/**
 * @brief Sign of \p value.
 *
 * Returns -1, 0, or +1 in the value type. Zero (positive or negative) returns 0.
 *
 * @tparam Value Signed arithmetic type.
 * @param value Input value.
 *
 * @return -1, 0, or +1.
 *
 * @pre \p value is not NaN.
 * @post Result is in {-1, 0, +1}.
 */
template <signed_arithmetic Value>
[[nodiscard]] constexpr auto sign(Value const value) noexcept -> Value {
  return value < Value{0} ? Value{-1} : value > Value{0} ? Value{+1} : Value{0};
}

/**
 * @brief Combines the magnitude of \p value with the sign of \p sign_source.
 *
 * Drop-in for \c std::copysign that stays \c constexpr.
 *
 * @tparam Real Floating-point type.
 * @param value Provides the magnitude.
 * @param sign_source Provides the sign.
 *
 * @return Value with magnitude of \p value and sign of \p sign_source.
 *
 * @pre Both inputs are finite.
 * @post Result has the magnitude of \p value.
 *
 * @note Does not preserve the IEEE-754 sign bit of negative zero in
 *       \p sign_source; treats -0.0 as zero.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto copysign(Real const value, Real const sign_source) noexcept -> Real {
  auto const magnitude{value < Real{0} ? -value : value};
  return sign_source < Real{0} ? -magnitude : magnitude;
}

/**
 * @brief Squares \p value.
 *
 * Computes \p value times itself with no intermediate widening.
 *
 * @tparam Value Arithmetic type.
 * @param value The value to square.
 *
 * @return The square of \p value.
 *
 * @pre None.
 * @post Returned value equals \p value multiplied by itself, modulo IEEE-754
 *       rounding for floats.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto square(Value const value) noexcept -> Value {
  return static_cast<Value>(value * value);
}

/**
 * @brief Cubes \p value.
 *
 * @tparam Value Arithmetic type.
 * @param value The value to cube.
 *
 * @return The cube of \p value.
 *
 * @pre None.
 * @post Returned value equals \p value times \p value times \p value, modulo
 *       IEEE-754 rounding for floats.
 *
 * @note Sign is preserved for signed types: \c cube(-x) equals \c -cube(x).
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto cube(Value const value) noexcept -> Value {
  return static_cast<Value>(value * value * value);
}

/**
 * @brief Linearly interpolates between \p lhs and \p rhs by parameter \p t.
 *
 * Computes \c lhs+t*(rhs-lhs) (one multiply-add, the fast form). At \c t equal
 * to 0 this returns \p lhs exactly. At \c t equal to 1 it returns \p rhs only up
 * to rounding, not bit-exactly: unlike \c std::lerp this form is neither
 * endpoint-exact nor monotonic, so when \c |lhs| is far larger than \c |rhs| the
 * \c t=1 result can differ from \p rhs (catastrophic cancellation in
 * \c rhs-lhs). Use \c std::lerp where exactness at \c t=1 or monotonicity is
 * required. Values of \p t outside [0, 1] extrapolate past the endpoints.
 *
 * @tparam Real Floating-point type.
 * @param lhs Value returned when \p t equals 0.
 * @param rhs Value approached when \p t equals 1.
 * @param t Interpolation parameter, typically in [0, 1].
 *
 * @return The interpolated value.
 *
 * @pre \p lhs, \p rhs, and \p t are finite.
 * @post Returned value equals \p lhs exactly when \p t is 0, and \p rhs up to
 *       rounding when \p t is 1; lies between \p lhs and \p rhs for \p t in
 *       (0, 1).
 *
 * @par Example
 * \code
 *   constexpr auto half{nexenne::math::lerp(0.0, 10.0, 0.5)};
 *   static_assert(half == 5.0);
 * \endcode
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto lerp(Real const lhs, Real const rhs, Real const t) noexcept -> Real {
  return Real{lhs + t * (rhs - lhs)};
}

/**
 * @brief Inverse of \c lerp: given a value in [lo, hi], returns its parameter
 *        \c t in [0, 1].
 *
 * @tparam Real Floating-point type.
 * @param lo Lower endpoint.
 * @param hi Upper endpoint.
 * @param value Value to map back to a parameter.
 *
 * @return Parameter \c t in [0, 1] when \p value is in [lo, hi].
 *
 * @pre \p lo is not equal to \p hi.
 * @post Returned value is finite.
 *
 * @warning Division by zero when \p lo equals \p hi.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
inverse_lerp(Real const lo, Real const hi, Real const value) noexcept -> Real {
  return Real{(value - lo) / (hi - lo)};
}

/**
 * @brief Linearly remaps \p value from one interval to another.
 *
 * Maps \p value in [from_lo, from_hi] to the same relative position in
 * [to_lo, to_hi].
 *
 * @tparam Real Floating-point type.
 * @param value Value in the source range.
 * @param from_lo Lower endpoint of the source range.
 * @param from_hi Upper endpoint of the source range.
 * @param to_lo Lower endpoint of the destination range.
 * @param to_hi Upper endpoint of the destination range.
 *
 * @return Remapped value.
 *
 * @pre \p from_lo is not equal to \p from_hi.
 * @post Returned value is finite when the precondition holds.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto remap(
  Real const value, Real const from_lo, Real const from_hi, Real const to_lo, Real const to_hi
) noexcept -> Real {
  return lerp(to_lo, to_hi, inverse_lerp(from_lo, from_hi, value));
}

/**
 * @brief Hermite-style smooth step from \p edge0 to \p edge1.
 *
 * Returns 0 for inputs at or below \p edge0, 1 for inputs at or above \p edge1,
 * and a smooth cubic curve between them with zero derivative at each endpoint.
 * The function is the standard \c 3*t^2-2*t^3 Hermite smoothstep.
 *
 * @tparam Real Floating-point type.
 * @param edge0 Lower edge.
 * @param edge1 Upper edge.
 * @param value Value to smooth.
 *
 * @return Smoothed value in [0, 1].
 *
 * @pre \p edge0 is less than \p edge1.
 * @post Returned value lies in [0, 1].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
smoothstep(Real const edge0, Real const edge1, Real const value) noexcept -> Real {
  // Normalize the input to t in [0, 1], then shape it with the cubic Hermite
  // polynomial 3t^2 - 2t^3. That cubic is the unique one with values 0 at t=0 and
  // 1 at t=1 and zero first derivative at both ends, so the curve eases in and
  // out smoothly (no velocity discontinuity at the edges, unlike a raw lerp).
  auto const t{saturate(inverse_lerp(edge0, edge1, value))};
  return Real{t * t * (Real{3} - Real{2} * t)};
}

/**
 * @brief Tolerance-aware equality for floats.
 *
 * Treats \p a and \p b as equal when the absolute difference is within
 * \p abs_tol or within \p rel_tol times the larger magnitude. The combined check
 * handles small near-zero values (absolute test) and large values (relative
 * test) correctly.
 *
 * @tparam Real Floating-point type.
 * @param a First value.
 * @param b Second value.
 * @param abs_tol Absolute tolerance. Default is 1e-6 in \p Real.
 * @param rel_tol Relative tolerance. Default is 1e-5 in \p Real.
 *
 * @return \c true when the values are within tolerance.
 *
 * @pre All inputs are finite.
 * @post Returns \c false when either operand is NaN.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto almost_equal(
  Real const a,
  Real const b,
  Real const abs_tol = static_cast<Real>(1e-6),
  Real const rel_tol = static_cast<Real>(1e-5)
) noexcept -> bool {
  // Two tests OR-ed together because neither alone is right everywhere. The
  // absolute test (diff <= abs_tol) is needed near zero, where a relative test
  // would demand impossible precision (the relative gap of two tiny numbers can
  // be huge). The relative test (diff <= rel_tol * larger-magnitude) is needed
  // for large values, where floating-point spacing is itself larger than any
  // fixed absolute tolerance. Either passing means "close enough".
  auto const diff{abs(a - b)};
  auto const scale{max(abs(a), abs(b))};
  return diff <= abs_tol || diff <= rel_tol * scale;
}

/**
 * @brief Reports whether \p value is within \p tolerance of zero.
 *
 * Convenience wrapper over \c abs(value)<=tolerance. Useful at API boundaries
 * where "this dot product is effectively zero" needs to be stated cleanly.
 *
 * @tparam Real Floating-point type.
 * @param value Value to test.
 * @param tolerance Absolute tolerance. Default is 1e-6.
 *
 * @return \c true when the absolute value is at or below \p tolerance.
 *
 * @pre \p tolerance is non-negative and finite.
 * @post Returns \c false when \p value is NaN.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto approximately_zero(
  Real const value, Real const tolerance = static_cast<Real>(1e-6)
) noexcept -> bool {
  return abs(value) <= tolerance;
}

/**
 * @brief Reports whether \p value is NaN.
 *
 * Implemented via the IEEE-754 property that NaN is the only value that compares
 * unequal to itself. Works at compile time.
 *
 * @tparam Real Floating-point type.
 * @param value Input value.
 *
 * @return \c true when \p value is NaN.
 *
 * @pre None.
 * @post Returns \c true exactly for NaN inputs.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto isnan(Real const value) noexcept -> bool {
  return value != value;
}

/**
 * @brief Reports whether \p value is positive or negative infinity.
 *
 * @tparam Real Floating-point type.
 * @param value Input value.
 *
 * @return \c true when \p value is +inf or -inf.
 *
 * @pre None.
 * @post Returns \c false for NaN and finite inputs.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto isinf(Real const value) noexcept -> bool {
  return value == std::numeric_limits<Real>::infinity()
         || value == -std::numeric_limits<Real>::infinity();
}

/**
 * @brief Reports whether \p value is finite (neither NaN nor infinity).
 *
 * @tparam Real Floating-point type.
 * @param value Input value.
 *
 * @return \c true when \p value is a finite real number.
 *
 * @pre None.
 * @post Returns \c false for NaN and infinity, \c true otherwise.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto isfinite(Real const value) noexcept -> bool {
  return !isnan(value) && !isinf(value);
}

/**
 * @brief Truncates \p value toward zero.
 *
 * \c constexpr at compile time, dispatches to \c std::trunc at runtime.
 *
 * @tparam Real Floating-point type.
 * @param value Value to truncate.
 *
 * @return Integral value of \p value with the fractional part removed.
 *
 * @pre \p value is finite.
 * @post Result has the same sign as \p value and magnitude no greater.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto trunc(Real const value) noexcept -> Real {
  if consteval {
    // Any value of magnitude >= 2/epsilon has no fractional bits left, so it is
    // already integral; return it unchanged. This both is correct and avoids the
    // undefined cast of an out-of-range float to long long for huge inputs (a
    // value above LLONG_MAX would otherwise be UB).
    constexpr Real integral_threshold{Real{2} / std::numeric_limits<Real>::epsilon()};
    if (value >= integral_threshold || value <= -integral_threshold) {
      return value;
    }
    return static_cast<Real>(static_cast<long long>(value));
  } else {
    return std::trunc(value);
  }
}

/**
 * @brief Floor of \p value: the greatest integer not greater than \p value.
 *
 * @tparam Real Floating-point type.
 * @param value Value to floor.
 *
 * @return Floored value.
 *
 * @pre \p value is finite.
 * @post Result is less than or equal to \p value and within 1 of it.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto floor(Real const value) noexcept -> Real {
  if consteval {
    auto const truncated{trunc(value)};
    return truncated > value ? truncated - Real{1} : truncated;
  } else {
    return std::floor(value);
  }
}

/**
 * @brief Ceiling of \p value: the smallest integer not less than \p value.
 *
 * @tparam Real Floating-point type.
 * @param value Value to ceil.
 *
 * @return Ceiled value.
 *
 * @pre \p value is finite.
 * @post Result is greater than or equal to \p value and within 1 of it.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ceil(Real const value) noexcept -> Real {
  if consteval {
    auto const truncated{trunc(value)};
    return truncated < value ? truncated + Real{1} : truncated;
  } else {
    return std::ceil(value);
  }
}

/**
 * @brief Rounds \p value to the nearest integer, ties away from zero.
 *
 * @tparam Real Floating-point type.
 * @param value Value to round.
 *
 * @return Rounded value.
 *
 * @pre \p value is finite.
 * @post Result is within 0.5 of \p value.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto round(Real const value) noexcept -> Real {
  // Truncate toward zero, then step out by one when the discarded fraction is at
  // least a half (ties away from zero). The naive floor(x + 0.5) is wrong:
  // adding 0.5 to the largest value below 0.5 rounds up to exactly 1.0, so it
  // would round 0.49999999999999994 to 1 instead of 0 (a double-rounding error).
  // Comparing the fraction avoids that and stays within 0.5 of the input.
  auto const truncated{trunc(value)};
  auto const fraction{value - truncated};
  if (value >= Real{0}) {
    return fraction >= Real{0.5} ? truncated + Real{1} : truncated;
  }
  return fraction <= Real{-0.5} ? truncated - Real{1} : truncated;
}

/**
 * @brief Fractional part of \p value, in [0, 1) for non-negative inputs.
 *
 * Equivalent to \c value-floor(value). For negative inputs the result is still
 * in [0, 1) due to the floor-based definition, not (-1, 0] as \c std::modf
 * returns.
 *
 * @tparam Real Floating-point type.
 * @param value Input value.
 *
 * @return Fractional part in [0, 1).
 *
 * @pre \p value is finite.
 * @post Result lies in [0, 1).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fract(Real const value) noexcept -> Real {
  // value - floor(value) is mathematically in [0, 1), but for a tiny negative
  // input the subtraction rounds up to exactly 1 (1 - 1e-20 == 1.0 in double),
  // breaking the half-open postcondition and any caller indexing with it. Pull
  // that boundary case back to 0.
  auto const f{value - floor(value)};
  return f >= Real{1} ? Real{0} : f;
}

/**
 * @brief Floor-based floating-point modulo.
 *
 * Returns \c a-b*floor(a/b). The sign of the result follows the sign of \p b
 * (unlike \c std::fmod which follows the sign of \p a). Matches the Python
 * modulo convention.
 *
 * @tparam Real Floating-point type.
 * @param a Dividend.
 * @param b Divisor.
 *
 * @return \p a modulo \p b.
 *
 * @pre \p b is non-zero and finite.
 * @post Result has the sign of \p b and magnitude less than the magnitude of
 *       \p b.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto mod(Real const a, Real const b) noexcept -> Real {
  // Floor-based modulo: subtract the largest multiple of b not exceeding a.
  // Because floor rounds toward negative infinity (not toward zero like the
  // truncated division behind std::fmod), the remainder always takes the sign of
  // b, which is what cyclic quantities (angles, tile indices) want.
  auto const r{a - b * floor(a / b)};
  // The result is mathematically in [0, b) (or (b, 0] for b < 0), but rounding
  // can land it on the excluded endpoint b: for a tiny a of opposite sign,
  // a - b*(-1) rounds to b. Pull that back to 0 so the half-open postcondition
  // holds (and callers like repeat/wrap/fract that index with it stay in range).
  if ((b > Real{0} && r >= b) || (b < Real{0} && r <= b)) {
    return Real{0};
  }
  return r;
}

/**
 * @brief Wraps \p value into [0, length) by floor-modulo.
 *
 * Equivalent to \c mod(value, length). Useful for cyclic indices, tile-map wrap,
 * and similar.
 *
 * @tparam Real Floating-point type.
 * @param value Value to wrap.
 * @param length Period. Must be positive.
 *
 * @return Wrapped value in [0, length).
 *
 * @pre \p length is positive and finite.
 * @post Result lies in [0, length).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto repeat(Real const value, Real const length) noexcept -> Real {
  return mod(value, length);
}

/**
 * @brief Wraps \p value into [lo, hi) by floor-modulo.
 *
 * Generalized \c repeat over an arbitrary interval.
 *
 * @tparam Real Floating-point type.
 * @param value Value to wrap.
 * @param lo Lower bound of the interval, inclusive.
 * @param hi Upper bound of the interval, exclusive.
 *
 * @return Wrapped value in [lo, hi).
 *
 * @pre \p lo is strictly less than \p hi.
 * @post Result lies in [lo, hi).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto wrap(Real const value, Real const lo, Real const hi) noexcept -> Real {
  return lo + repeat(value - lo, hi - lo);
}

/**
 * @brief Triangle-wave-style "ping pong" wrap.
 *
 * Maps a monotonic input to a triangle wave that bounces between 0 and \p length
 * and back. The result is 0 at \c value=0, climbs to \p length at
 * \c value=length, returns to 0 at \c value=2*length, and so on.
 *
 * @tparam Real Floating-point type.
 * @param value Input value, typically time.
 * @param length Half-period of the triangle wave. Must be positive.
 *
 * @return Triangle value in [0, length].
 *
 * @pre \p length is positive and finite.
 * @post Result lies in [0, length].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ping_pong(Real const value, Real const length) noexcept -> Real {
  auto const cycle{repeat(value, Real{2} * length)};
  return cycle <= length ? cycle : Real{2} * length - cycle;
}

}  // namespace nexenne::math
