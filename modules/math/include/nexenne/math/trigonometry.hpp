#pragma once

/**
 * @file
 * @brief Trigonometry: paired sincos, polynomial approximations, a runtime LUT,
 *        fast inverse trig, and angle utilities.
 *
 * Three implementation strategies are exposed, each with a clear cost profile:
 *
 * - \c sincos / \c sin / \c cos / \c tan (the last three in angle.hpp):
 *   full-precision libm. The default. Use unless profiling says otherwise.
 * - \c fast_sin / \c fast_cos / \c fast_tan / \c fast_asin / \c fast_acos /
 *   \c fast_atan / \c fast_atan2: polynomial approximations, accuracy around
 *   3e-7 for small angles in [-pi, pi], degrading per period as range reduction
 *   loses bits. Cache-friendly, no LUT, \c constexpr. Marked clearly as
 *   approximate.
 * - \c lut_sin / \c lut_cos: runtime lookup with linear interpolation, accuracy
 *   around 4.7e-6 at the default 1024-entry table. The 4 KB float table fits in L1
 *   but warming it adds real cost. Generally slower than libm on modern CPUs;
 *   included for constrained targets and callers who have measured a win.
 *
 * Angle utilities: \c angle_diff (shortest signed difference, handles
 * wraparound) and \c lerp_angle (angle-aware lerp that takes the short way
 * around).
 */

#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>

namespace nexenne::math {

/**
 * @brief A sine and a cosine of the same angle, returned together.
 *
 * Why this type exists: the dominant trig use (rotation matrices, quaternions,
 * polar-to-cartesian) needs both \c sin and \c cos of one angle, and computing
 * them together is cheaper than separately. \c poly_sincos range-reduces the
 * argument once and evaluates both polynomials on the shared reduced value, and
 * libm \c std::sin / \c std::cos with the same argument fuse into a single
 * hardware sincos. Returning the pair makes that sharing the default and the
 * call site readable (\c .sin() / \c .cos() rather than two calls).
 *
 * On a value: at \c -O2 the optimizer already common-subexpression-eliminates a
 * separate \c fast_sin(a) plus \c fast_cos(a) back into one computation, so this
 * type is not a speed-up the compiler cannot find on the inlined fast path. Its
 * guarantee bites where CSE does not reach: an unoptimized build, a call split
 * across translation units, and the libm sincos fusion. It is a trivially
 * copyable aggregate the size of two \p Real values, returned in registers, so
 * the readability and the guarantee cost nothing.
 *
 * The fields are read-only after construction: a computed sine/cosine pair is a
 * result, not a mutable slot, so only const accessors are exposed.
 *
 * @tparam Real Floating-point type.
 */
template <std::floating_point Real>
class sin_cos {
public:
  using value_type = Real;  ///< The underlying floating-point scalar type.

private:
  value_type m_sin{};
  value_type m_cos{};

public:
  /**
   * @brief Constructs a zero sine/cosine pair.
   *
   * @pre None.
   * @post Both the sine and cosine fields are zero.
   */
  constexpr sin_cos() noexcept = default;

  /**
   * @brief Constructs a sine/cosine pair from its two values.
   *
   * @param sin Sine value.
   * @param cos Cosine value.
   *
   * @pre None.
   * @post The sine field equals \p sin and the cosine field equals \p cos.
   */
  constexpr sin_cos(value_type const sin, value_type const cos) noexcept : m_sin{sin}, m_cos{cos} {}

  /**
   * @brief Accesses the sine value.
   *
   * @return Const reference to the sine value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto sin() const noexcept -> value_type const& {
    return m_sin;
  }

  /**
   * @brief Accesses the cosine value.
   *
   * @return Const reference to the cosine value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto cos() const noexcept -> value_type const& {
    return m_cos;
  }
};

namespace detail {

/**
 * @brief Constexpr round-to-nearest, ties away from zero.
 *
 * @tparam Real Floating-point type.
 * @param x Value to round.
 *
 * @return Nearest integer to \p x as a \c long \c long.
 *
 * @pre \p x is finite and within the range of \c long \c long.
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto round_nearest(Real const x) noexcept -> long long {
  return x >= Real{0} ? static_cast<long long>(x + Real{0.5})
                      : -static_cast<long long>(-x + Real{0.5});
}

/**
 * @brief Polynomial sine on the reduced interval [-pi/4, pi/4].
 *
 * Degree-7 Taylor polynomial in Horner form. Error on the order of 3e-7 over
 * [-pi/4, pi/4] (the dropped x^9/9! term at the interval edge).
 *
 * @tparam Real Floating-point type.
 * @param x Reduced angle in [-pi/4, pi/4].
 *
 * @return Approximate \c sin(x).
 *
 * @pre \p x is in [-pi/4, pi/4].
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto poly_sin_reduced(Real const x) noexcept -> Real {
  // The Maclaurin series sin(x) = x - x^3/3! + x^5/5! - x^7/7! evaluated in
  // Horner form (factor out x, then x^2 repeatedly) to minimize multiplies and
  // rounding. The denominators are the odd factorials 6, 120, 5040. Truncating
  // after x^7 leaves a next term x^9/9!, which is ~3e-7 at the interval edge
  // x=pi/4, so the reduction to [-pi/4, pi/4] is what keeps this accurate.
  auto const x2{x * x};
  // Horner from the highest term down; bit-identical to the nested form, within
  // the column limit.
  auto p{Real{1} / Real{5040}};
  p = Real{1} / Real{120} - x2 * p;
  p = Real{1} / Real{6} - x2 * p;
  p = Real{1} - x2 * p;
  return x * p;
}

/**
 * @brief Polynomial cosine on the reduced interval [-pi/4, pi/4].
 *
 * Degree-8 Taylor polynomial in Horner form. Error about 2.5e-8 over
 * [-pi/4, pi/4].
 *
 * @tparam Real Floating-point type.
 * @param x Reduced angle in [-pi/4, pi/4].
 *
 * @return Approximate \c cos(x).
 *
 * @pre \p x is in [-pi/4, pi/4].
 * @post None.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto poly_cos_reduced(Real const x) noexcept -> Real {
  // The Maclaurin series cos(x) = 1 - x^2/2! + x^4/4! - x^6/6! + x^8/8! in Horner
  // form. The denominators are the even factorials 2, 24, 720, 40320. The first
  // dropped term is x^10/10! = (pi/4)^10/3628800 ~ 2.5e-8 at x=pi/4, so over the
  // reduced interval this is good to about 2.5e-8.
  auto const x2{x * x};
  // Horner from the highest term down; bit-identical to the nested form, within
  // the column limit.
  auto p{Real{1} / Real{40320}};
  p = Real{1} / Real{720} - x2 * p;
  p = Real{1} / Real{24} - x2 * p;
  p = Real{1} / Real{2} - x2 * p;
  return Real{1} - x2 * p;
}

/**
 * @brief Sine and cosine by range-reducing to [-pi/4, pi/4] plus quadrant
 *        identities.
 *
 * @tparam Real Floating-point type.
 * @param x Angle in radians.
 *
 * @return Approximate sine/cosine pair for \p x.
 *
 * @pre \p x is finite and within a few thousand multiples of pi.
 * @post Both fields lie in [-1, 1] to within rounding error.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto poly_sincos(Real const x) noexcept -> sin_cos<Real> {
  // Guard the round_nearest cast against overflow for out-of-contract huge
  // angles: x / (pi/2) above the long long range is undefined when cast. The LUT
  // path reduces unconditionally; here a predicted-not-taken branch keeps the
  // common small-angle path free of the extra division, reducing modulo a full
  // turn (which preserves sin and cos) only when x is enormous.
  auto reduced_x{x};
  if (x > Real{1e15} || x < Real{-1e15}) {
    reduced_x = mod(x, tau_v<Real>);
  }
  // Range reduction: find the nearest multiple of pi/2 and subtract it, so the
  // remainder lies in [-pi/4, pi/4] where the short polynomials are accurate.
  // k counts how many quarter-turns we removed.
  auto const k{round_nearest(reduced_x / half_pi_v<Real>)};
  auto const reduced{reduced_x - static_cast<Real>(k) * half_pi_v<Real>};
  auto const s{poly_sin_reduced(reduced)};
  auto const c{poly_cos_reduced(reduced)};
  // Each quarter-turn rotates (sin, cos) by 90 degrees, cycling with period 4.
  // The co-function identities sin(t+pi/2)=cos t and cos(t+pi/2)=-sin t give, for
  // q = k mod 4: q0 -> (s, c); q1 -> (c, -s); q2 -> (-s, -c); q3 -> (-c, s). The
  // ((k%4)+4)%4 keeps q in [0, 3] for negative k (C++ % can be negative).
  auto const q{static_cast<int>(((k % 4) + 4) % 4)};
  switch (q) {
    case 0:
      return sin_cos<Real>{s, c};
    case 1:
      return sin_cos<Real>{c, -s};
    case 2:
      return sin_cos<Real>{-s, -c};
    default:
      return sin_cos<Real>{-c, s};
  }
}

}  // namespace detail

/**
 * @brief Computes \c sin and \c cos of \p r in a single call.
 *
 * Calls libm \c std::sin and \c std::cos, which the compiler is allowed to fuse
 * into a single hardware sincos (and routinely does). Use this when both values
 * are needed for the same angle, for example inside rotation construction.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Aggregate of \c sin(r) and \c cos(r).
 *
 * @pre \c r.value() is finite.
 * @post Both fields lie in [-1, 1].
 */
template <std::floating_point Real>
[[nodiscard]] auto sincos(radians<Real> const r) noexcept -> sin_cos<Real> {
  return sin_cos<Real>{std::sin(r.value()), std::cos(r.value())};
}

/**
 * @brief Approximate \c sin of \p r using a constexpr polynomial.
 *
 * Range-reduces to [-pi/4, pi/4] and applies a degree-7 Taylor polynomial in
 * Horner form. Error on the order of 3e-7 for small inputs; degrades by roughly
 * one decimal per period as the range reduction loses precision for larger
 * magnitudes.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Approximate \c sin(r).
 *
 * @pre \c r.value() is finite and within a few thousand multiples of pi.
 * @post Result lies in [-1, 1] to within rounding error.
 *
 * @warning Accuracy is intentionally lower than libm. For full precision use
 *          \c sin from angle.hpp.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_sin(radians<Real> const r) noexcept -> Real {
  return detail::poly_sincos(r.value()).sin();
}

/**
 * @brief Approximate \c cos of \p r using a constexpr polynomial.
 *
 * Same range reduction and polynomial as \c fast_sin.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Approximate \c cos(r).
 *
 * @pre \c r.value() is finite and within a few thousand multiples of pi.
 * @post Result lies in [-1, 1] to within rounding error.
 *
 * @warning Accuracy is intentionally lower than libm.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_cos(radians<Real> const r) noexcept -> Real {
  return detail::poly_sincos(r.value()).cos();
}

/**
 * @brief Paired approximate \c sin and \c cos of \p r.
 *
 * Same cost as \c fast_sin alone: the range reduction and polynomial evaluation
 * are shared.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Aggregate of approximate \c sin(r) and \c cos(r).
 *
 * @pre \c r.value() is finite and within a few thousand multiples of pi.
 * @post Both fields lie in [-1, 1] to within rounding error.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_sincos(radians<Real> const r) noexcept -> sin_cos<Real> {
  return detail::poly_sincos(r.value());
}

/**
 * @brief Approximate \c tan of \p r, computed as \c fast_sin/fast_cos.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Approximate \c tan(r).
 *
 * @pre \c r.value() is finite and not too close to an odd multiple of pi/2.
 * @post Result is finite when the precondition holds.
 *
 * @warning The result diverges near odd multiples of pi/2; the polynomial cosine
 *          can be small but nonzero there.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_tan(radians<Real> const r) noexcept -> Real {
  auto const sc{detail::poly_sincos(r.value())};
  return sc.sin() / sc.cos();
}

/**
 * @brief Approximate \c asin of \p x, in radians.
 *
 * Abramowitz and Stegun 4.4.46 degree-7 polynomial. Maximum error around 5e-8
 * over [-1, 1].
 *
 * @tparam Real Floating-point type.
 * @param x Value in [-1, 1].
 *
 * @return Approximate \c asin(x) as a radians strong type.
 *
 * @pre \p x is in [-1, 1].
 * @post Result lies in [-pi/2, pi/2].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_asin(Real const x) noexcept -> radians<Real> {
  // asin has infinite slope at x = +/-1, which a plain polynomial cannot follow.
  // The Abramowitz and Stegun 4.4.46 form factors that singularity out as
  // asin(x) = pi/2 - sqrt(1 - x) * P(x), where P is a smooth degree-7 polynomial
  // (the sqrt carries the vertical tangent, P is the easy part). We evaluate on
  // |x| and restore the sign at the end, since asin is odd. Coefficients are the
  // A&S fit; max error about 5e-8 over [-1, 1]. The magnitude is clamped to 1
  // first: callers routinely pass acos(dot) where rounding nudges the dot a hair
  // past 1, which would make sqrt(1 - ax) take a negative argument and return NaN.
  auto const ax{min(abs(x), Real{1})};
  // Horner from the highest coefficient down; bit-identical to the nested form,
  // within the column limit.
  auto p{Real{-0.0012624911}};
  p = Real{0.0066700901} + ax * p;
  p = Real{-0.0170881256} + ax * p;
  p = Real{0.0308918810} + ax * p;
  p = Real{-0.0501743046} + ax * p;
  p = Real{0.0889789874} + ax * p;
  p = Real{-0.2145988016} + ax * p;
  p = Real{1.5707963050} + ax * p;
  auto const v{half_pi_v<Real> - sqrt(Real{1} - ax) * p};
  return radians<Real>{x < Real{0} ? -v : v};
}

/**
 * @brief Approximate \c acos of \p x, in radians.
 *
 * Implemented as \c pi/2 - fast_asin(x). Same accuracy and cost.
 *
 * @tparam Real Floating-point type.
 * @param x Value in [-1, 1].
 *
 * @return Approximate \c acos(x) as a radians strong type.
 *
 * @pre \p x is in [-1, 1].
 * @post Result lies in [0, pi].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_acos(Real const x) noexcept -> radians<Real> {
  return radians<Real>{half_pi_v<Real> - fast_asin(x).value()};
}

/**
 * @brief Approximate \c atan of \p x, in radians.
 *
 * Degree-11 odd polynomial (the six terms x, x^3, ..., x^11) on [-1, 1], with the
 * \c atan(1/x) reduction for |x|>1. Maximum error around 1.7e-6 across the real
 * line.
 *
 * @tparam Real Floating-point type.
 * @param x Value.
 *
 * @return Approximate \c atan(x) as a radians strong type.
 *
 * @pre \p x is finite.
 * @post Result lies in (-pi/2, pi/2).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_atan(Real const x) noexcept -> radians<Real> {
  // The polynomial is only fit on [-1, 1]. For |x| > 1 use the identity
  // atan(x) = pi/2 - atan(1/x) to fold the input back into [0, 1], evaluate
  // there, then unfold. atan is odd, so we work on |x| and restore the sign last.
  auto const ax{abs(x)};
  auto const reduced{ax > Real{1} ? Real{1} / ax : ax};
  auto const r2{reduced * reduced};
  // Horner from the highest coefficient down; bit-identical to the nested form,
  // within the column limit.
  auto q{Real{-0.01172120}};
  q = Real{0.05265332} + r2 * q;
  q = Real{-0.11643287} + r2 * q;
  q = Real{0.19354346} + r2 * q;
  q = Real{-0.33262347} + r2 * q;
  q = Real{0.99997726} + r2 * q;
  auto const p{reduced * q};
  auto const folded{ax > Real{1} ? half_pi_v<Real> - p : p};
  return radians<Real>{x < Real{0} ? -folded : folded};
}

/**
 * @brief Approximate \c atan2(y, x), in radians.
 *
 * Standard quadrant reduction over \c fast_atan. Returns 0 when both inputs are
 * zero.
 *
 * @tparam Real Floating-point type.
 * @param y Numerator (vertical).
 * @param x Denominator (horizontal).
 *
 * @return Approximate \c atan2(y, x) as a radians strong type.
 *
 * @pre At least one of \p x or \p y is non-zero (otherwise returns 0).
 * @post Result lies in (-pi, pi].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto fast_atan2(Real const y, Real const x) noexcept -> radians<Real> {
  // atan(y/x) only knows the line's slope, not which of the two opposite
  // directions along it the point lies, so it always lands in (-pi/2, pi/2)
  // (the right half-plane). atan2 recovers the full (-pi, pi] angle from the
  // signs of x and y:
  if (x > Real{0}) {
    // Right half-plane: atan(y/x) is already correct.
    return fast_atan(y / x);
  }
  if (x < Real{0}) {
    // Left half-plane: atan(y/x) is off by a half-turn; add pi when above the
    // x-axis, subtract pi when below, to swing into the correct quadrant. Use the
    // sign bit, not y >= 0, so a negative zero (atan2(-0, x<0)) returns -pi like
    // IEEE std::atan2 rather than +pi.
    auto const inner{fast_atan(y / x).value()};
    return radians<Real>{std::signbit(y) ? inner - pi_v<Real> : inner + pi_v<Real>};
  }
  // x == 0: straight up or down (avoids the y/x division by zero).
  if (y > Real{0}) {
    return radians<Real>{half_pi_v<Real>};
  }
  if (y < Real{0}) {
    return radians<Real>{-half_pi_v<Real>};
  }
  return radians<Real>{Real{0}};
}

/// @brief Default LUT size: 1024 entries (about 4 KB per float table).
inline constexpr std::size_t default_trig_lut_size = 1024;

namespace detail {

/**
 * @brief Compile-time-generated sine table of \p lut_size entries spanning
 *        [0, 2*pi). Cosine is read off the same table at an offset.
 *
 * @tparam Real Floating-point type.
 * @tparam lut_size Number of table entries.
 */
template <std::floating_point Real, std::size_t lut_size>
class trig_lut_table {
public:
  using table_type = std::array<Real, lut_size>;  ///< The table storage type.

private:
  table_type m_sin_table{};

public:
  /**
   * @brief Fills the table with one sine period at compile time.
   *
   * @pre None.
   * @post Entry i holds the polynomial approximation of sin(2*pi*i/lut_size)
   *       (built from poly_sincos, since std::sin is not constexpr).
   */
  constexpr trig_lut_table() noexcept {
    for (std::size_t i{0}; i < lut_size; ++i) {
      auto const angle{tau_v<Real> * static_cast<Real>(i) / static_cast<Real>(lut_size)};
      m_sin_table[i] = poly_sincos(angle).sin();
    }
  }

  /**
   * @brief Accesses the sine table.
   *
   * @return Const reference to the table of \p lut_size sine samples.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto sin_table() const noexcept -> table_type const& {
    return m_sin_table;
  }
};

template <std::floating_point Real, std::size_t lut_size = default_trig_lut_size>
inline constexpr trig_lut_table<Real, lut_size> trig_lut{};

/**
 * @brief Linear-interpolated sine lookup for a phase already in [0, 1).
 *
 * @tparam Real Floating-point type.
 * @tparam lut_size Number of table entries.
 * @param phase Phase in [0, 1).
 *
 * @return Interpolated sine sample.
 *
 * @pre \p phase is in [0, 1).
 * @post Result lies in [-1, 1].
 */
template <std::floating_point Real, std::size_t lut_size>
[[nodiscard]] constexpr auto lut_lookup_sin(Real const phase) noexcept -> Real {
  // Map the unit phase onto the table, split into an integer index i and a
  // fraction f in [0, 1), then linearly interpolate between entry i and its
  // successor: lerp(tab[i], tab[i+1], f). The wrap on i+1 closes the table into
  // a circle (the entry after the last is the first). Linear interpolation is
  // what gets accuracy to ~4.7e-6 at 1024 entries; a bare nearest-entry lookup
  // would only be table-spacing accurate (~6e-3 here).
  auto const scaled{phase * static_cast<Real>(lut_size)};
  auto const i{static_cast<std::size_t>(scaled)};
  auto const f{scaled - static_cast<Real>(i)};
  auto const i0{i % lut_size};
  auto const i1{(i + 1) % lut_size};
  auto const& tab{trig_lut<Real, lut_size>.sin_table()};
  return tab[i0] * (Real{1} - f) + tab[i1] * f;
}

/**
 * @brief Wraps \p x to [0, 2*pi) and rescales to a unit phase in [0, 1).
 *
 * @tparam Real Floating-point type.
 * @param x Angle in radians.
 *
 * @return Unit phase in [0, 1).
 *
 * @pre \p x is finite.
 * @post Result lies in [0, 1).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto to_unit_phase(Real const x) noexcept -> Real {
  // Floor-modulo into [0, 2pi) via scalar::mod, which reduces with floor and so
  // stays bounded for any finite magnitude (a truncating long long cast would be
  // undefined for inputs above LLONG_MAX, e.g. lut_sin(1e30)). mod's result is
  // strictly below 2pi, so phase < 1; but rounding can leave it a hair below 0,
  // so the (phase < 0) branch wraps that residue up to just under 1. The
  // (phase >= 1) branch never fires (mod's upper end is already strict).
  auto phase{mod(x, tau_v<Real>) / tau_v<Real>};
  if (phase >= Real{1}) {
    phase -= Real{1};
  } else if (phase < Real{0}) {
    phase += Real{1};
  }
  return phase;
}

}  // namespace detail

/**
 * @brief Lookup-table sine of \p r with linear interpolation.
 *
 * Uses a compile-time-generated table of \c default_trig_lut_size entries
 * spanning [0, 2*pi). Accuracy around 4.7e-6 at the default size. Cost is one mod,
 * one floor, one multiply, one add, and two table reads. Slower than libm on
 * modern x86 in most benchmarks; included for embedded targets and callers who
 * have measured a win.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Approximate \c sin(r).
 *
 * @pre \c r.value() is finite.
 * @post Result lies in [-1, 1].
 */
template <std::floating_point Real>
[[nodiscard]] auto lut_sin(radians<Real> const r) noexcept -> Real {
  auto const phase{detail::to_unit_phase(r.value())};
  return detail::lut_lookup_sin<Real, default_trig_lut_size>(phase);
}

/**
 * @brief Lookup-table cosine of \p r with linear interpolation.
 *
 * Reuses the sine table via the identity \c cos(x)=sin(x+pi/2). Same accuracy and
 * cost as \c lut_sin.
 *
 * @tparam Real Floating-point type.
 * @param r Angle in radians.
 *
 * @return Approximate \c cos(r).
 *
 * @pre \c r.value() is finite.
 * @post Result lies in [-1, 1].
 */
template <std::floating_point Real>
[[nodiscard]] auto lut_cos(radians<Real> const r) noexcept -> Real {
  auto const phase{detail::to_unit_phase(r.value() + half_pi_v<Real>)};
  return detail::lut_lookup_sin<Real, default_trig_lut_size>(phase);
}

/**
 * @brief Shortest signed difference \c a - b, wrapped to [-pi, pi).
 *
 * Useful when accumulated rotation has drifted by full turns and a sane
 * "current minus target" reading is needed. The range follows \c wrap_signed,
 * which is half-open at +pi: a difference of exactly pi maps to -pi.
 *
 * @tparam Real Floating-point type.
 * @param a Minuend angle.
 * @param b Subtrahend angle.
 *
 * @return Shortest signed difference, in radians.
 *
 * @pre Both inputs are finite.
 * @post Result lies in [-pi, pi).
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
angle_diff(radians<Real> const a, radians<Real> const b) noexcept -> radians<Real> {
  auto const diff{a.value() - b.value()};
  return wrap_signed(radians<Real>{diff});
}

/**
 * @brief Angle-aware lerp that always takes the short way around.
 *
 * Given two angles, interpolates along the shorter arc. Equivalent to
 * \c a + t * angle_diff(b, a). For \c t in [0, 1] the result moves smoothly from
 * \p a at \c t=0 to \p b at \c t=1.
 *
 * @tparam Real Floating-point type.
 * @param a Start angle.
 * @param b End angle.
 * @param t Interpolation parameter, typically in [0, 1].
 *
 * @return Interpolated angle, wrapped to [-pi, pi).
 *
 * @pre All inputs are finite.
 * @post Result lies in [-pi, pi) (it is passed through \c wrap_signed). At
 *       \c t=0 returns the wrap of \p a, at \c t=1 the wrap of \p b.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto
lerp_angle(radians<Real> const a, radians<Real> const b, Real const t) noexcept -> radians<Real> {
  auto const diff{angle_diff(b, a).value()};
  return wrap_signed(radians<Real>{a.value() + t * diff});
}

}  // namespace nexenne::math
