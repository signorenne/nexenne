#pragma once

/**
 * @file
 * @brief Powers, roots, and transcendentals.
 *
 * Sections: roots (sqrt, inv_sqrt); fast approximations (fast_inv_sqrt,
 * fast_exp, fast_log); and integer-exponent powers (pow_int).
 *
 * \c sqrt is \c constexpr at compile time (Newton iteration) and dispatches to
 * \c std::sqrt at runtime. \c fast_inv_sqrt is the Q_rsqrt-style bit-trick with
 * two Newton refinements, \c constexpr via \c std::bit_cast (C++23).
 * \c fast_exp and \c fast_log are Schraudolph-style IEEE-754 bit manipulations
 * with polynomial correction, accuracy roughly 1e-3 to 1e-7. Use the libm
 * \c std::exp / \c std::log when accuracy matters. The three bit-trick functions
 * are restricted to \c float and \c double, the only IEEE-754 layouts they
 * decode.
 */

#include <bit>
#include <cmath>
#include <concepts>
#include <cstdint>

#include <nexenne/math/constants.hpp>

namespace nexenne::math {

namespace detail {

/**
 * @brief Either of the two IEEE-754 binary layouts the bit-tricks decode.
 *
 * \c fast_inv_sqrt, \c fast_exp, and \c fast_log reinterpret the bits of a
 * \c float (binary32) or a \c double (binary64). Constraining them on this
 * concept gives a clear error for \c long \c double instead of a \c bit_cast
 * size-mismatch deep inside the body.
 *
 * @tparam Real Type to test.
 */
template <typename Real>
concept ieee_float = std::same_as<Real, float> || std::same_as<Real, double>;

/**
 * @brief Newton iteration for \c sqrt, used during constant evaluation.
 *
 * @tparam Real Floating-point type.
 * @param value Non-negative input.
 *
 * @return Square root of \p value, or 0 for non-positive input.
 *
 * @pre None.
 * @post Result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto sqrt_newton(Real const value) noexcept -> Real {
  if (value <= Real{0}) {
    return Real{0};
  }
  auto y{value};
  for (auto i{0}; i < 30; ++i) {
    y = Real{0.5} * (y + value / y);
  }
  return y;
}

}  // namespace detail

/**
 * @brief Square root of \p value.
 *
 * \c constexpr at compile time via Newton iteration; dispatches to \c std::sqrt
 * at runtime for full IEEE-754 precision and hardware acceleration.
 *
 * @tparam Real Floating-point type.
 * @param value Non-negative input.
 *
 * @return Square root of \p value.
 *
 * @pre \p value is non-negative and finite.
 * @post Result is non-negative.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto sqrt(Real const value) noexcept -> Real {
  if consteval {
    return detail::sqrt_newton(value);
  } else {
    return std::sqrt(value);
  }
}

/**
 * @brief Reciprocal square root of \p value: \c 1/sqrt(value).
 *
 * @tparam Real Floating-point type.
 * @param value Strictly positive input.
 *
 * @return \c 1/sqrt(value).
 *
 * @pre \p value is strictly positive and finite.
 * @post Result is strictly positive.
 *
 * @warning Division by zero when \p value is zero.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto inv_sqrt(Real const value) noexcept -> Real {
  return Real{1} / sqrt(value);
}

/**
 * @brief Approximate reciprocal square root, about 5e-6 relative accuracy.
 *
 * Quake III "Q_rsqrt" bit manipulation with two Newton refinements. The first
 * Newton step takes accuracy from about 1e-3 to about 5e-6 (roughly doubling the
 * correct digits each iteration). \c constexpr via \c std::bit_cast. Magic
 * constants from Lomont's 2003 analysis for \c float and the corresponding
 * \c double form.
 *
 * @tparam Real Floating-point type (\c float or \c double).
 * @param value Strictly positive input.
 *
 * @return Approximation of \c 1/sqrt(value).
 *
 * @pre \p value is strictly positive and finite.
 * @post Relative error is approximately 5e-6.
 *
 * @warning Undefined for non-positive inputs.
 */
template <detail::ieee_float Real>
[[nodiscard]] constexpr auto fast_inv_sqrt(Real const value) noexcept -> Real {
  if constexpr (std::same_as<Real, float>) {
    auto const half{Real{0.5} * value};
    auto bits{std::bit_cast<std::int32_t>(value)};
    bits = std::int32_t{0x5F37'5A86} - (bits >> 1);
    auto y{std::bit_cast<float>(bits)};
    y = y * (Real{1.5} - half * y * y);  // first Newton step
    y = y * (Real{1.5} - half * y * y);  // second Newton step
    return y;
  } else {
    auto const half{Real{0.5} * value};
    auto bits{std::bit_cast<std::int64_t>(value)};
    bits = std::int64_t{0x5FE6'EB50'C7B5'37A9} - (bits >> 1);
    auto y{std::bit_cast<double>(bits)};
    y = y * (Real{1.5} - half * y * y);  // first Newton step
    y = y * (Real{1.5} - half * y * y);  // second Newton step
    return y;
  }
}

/**
 * @brief Raises \p base to an integer power by exponentiation by squaring.
 *
 * Constant-time in the number of bits of \p exponent (logarithmic in its
 * magnitude). Handles negative exponents via reciprocal.
 *
 * @tparam Real Floating-point type.
 * @param base Base value.
 * @param exponent Integer exponent. May be negative.
 *
 * @return \p base raised to \p exponent.
 *
 * @pre When \p exponent is negative, \p base is non-zero. \p exponent is not
 *      \c INT_MIN (its negation would overflow).
 * @post Result is finite when the precondition holds.
 *
 * @par Example
 * \code
 *   constexpr auto v{nexenne::math::pow_int(2.0, 10)};
 *   static_assert(v == 1024.0);
 * \endcode
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto pow_int(Real base, int exponent) noexcept -> Real {
  if (exponent < 0) {
    return Real{1} / pow_int(base, -exponent);
  }
  auto result{Real{1}};
  while (exponent > 0) {
    if ((exponent & 1) != 0) {
      result *= base;
    }
    base *= base;
    exponent >>= 1;
  }
  return result;
}

/**
 * @brief Approximate \c exp of \p x, about 3e-6 relative accuracy.
 *
 * Splits \c e^x as \c 2^(x*log2(e)) and approximates \c 2^y via IEEE-754
 * exponent injection (Schraudolph 1999) plus a 7-term Taylor expansion of
 * \c 2^yf on the fractional part \c yf in [0, 1).
 *
 * @tparam Real Floating-point type (\c float or \c double).
 * @param x Exponent.
 *
 * @return Approximation of \c e^x.
 *
 * @pre \p x is finite and within the representable exponent range
 *      (about [-87, 88] for \c float).
 * @post Result is non-negative and finite within the supported range.
 *
 * @warning Accuracy is intentionally lower than libm. For full precision use
 *          \c std::exp.
 */
template <detail::ieee_float Real>
[[nodiscard]] constexpr auto fast_exp(Real const x) noexcept -> Real {
  auto const y{x * log2_e_v<Real>};
  auto const yi{static_cast<Real>(static_cast<long long>(y >= Real{0} ? y : y - Real{1}))};
  auto const yf{y - yi};
  // 7-term Taylor for 2^yf around yf=0, derived from exp(yf*ln(2)).
  // Coefficients are (ln(2))^k / k! for k = 0..7.
  auto const two_yf{
    Real{1}
    + yf
        * (Real{0.69314718055994531} + yf * (Real{0.24022650695910071} + yf * (Real{0.05550410866482158} + yf * (Real{0.00961812910762848} + yf * (Real{0.00133335581464284} + yf * (Real{0.00015403530393381} + yf * Real{0.00001525273380405}))))))
  };
  if constexpr (std::same_as<Real, float>) {
    auto bits{std::bit_cast<std::int32_t>(two_yf)};
    bits += static_cast<std::int32_t>(yi) << 23;
    return std::bit_cast<float>(bits);
  } else {
    auto bits{std::bit_cast<std::int64_t>(two_yf)};
    bits += static_cast<std::int64_t>(yi) << 52;
    return std::bit_cast<double>(bits);
  }
}

/**
 * @brief Approximate natural log of \p x, about 1e-7 accuracy.
 *
 * Extracts the IEEE-754 binary exponent and mantissa, then computes \c ln(m) for
 * the mantissa \c m in [1, 2) using the identity \c ln(m)=2*atanh((m-1)/(m+1))
 * and a 6-term odd-power series. This works on a much tighter range than a direct
 * polynomial fit to \c log2(m), so a small number of terms suffices. Final
 * result: \c exp_part*ln(2)+ln(m).
 *
 * @tparam Real Floating-point type (\c float or \c double).
 * @param x Strictly positive input.
 *
 * @return Approximation of \c ln(x).
 *
 * @pre \p x is strictly positive and finite.
 * @post Result is finite.
 *
 * @warning Accuracy is intentionally lower than libm. For full precision use
 *          \c std::log.
 */
template <detail::ieee_float Real>
[[nodiscard]] constexpr auto fast_log(Real const x) noexcept -> Real {
  Real exp_part{};
  Real m{};
  if constexpr (std::same_as<Real, float>) {
    auto bits{std::bit_cast<std::int32_t>(x)};
    exp_part = static_cast<Real>(((bits >> 23) & 0xFF) - 127);
    bits = (bits & 0x007F'FFFF) | 0x3F80'0000;
    m = std::bit_cast<float>(bits);
  } else {
    auto bits{std::bit_cast<std::int64_t>(x)};
    exp_part = static_cast<Real>(((bits >> 52) & 0x7FF) - 1023);
    bits = (bits & 0x000F'FFFF'FFFF'FFFF) | 0x3FF0'0000'0000'0000;
    m = std::bit_cast<double>(bits);
  }
  // u = (m - 1) / (m + 1) lies in [0, 1/3] for m in [1, 2).
  // ln(m) = 2 * (u + u^3/3 + u^5/5 + u^7/7 + u^9/9 + u^11/11 + ...).
  // 6 terms keeps truncation error below about 1e-7 at the worst case u = 1/3.
  auto const u{(m - Real{1}) / (m + Real{1})};
  auto const u2{u * u};
  auto const ln_m{
    Real{2} * u
    * (Real{1} + u2 * (Real{1} / Real{3} + u2 * (Real{1} / Real{5} + u2 * (Real{1} / Real{7} + u2 * (Real{1} / Real{9} + u2 * Real{1} / Real{11})))))
  };
  return exp_part * ln_two_v<Real> + ln_m;
}

}  // namespace nexenne::math
