#pragma once

/**
 * @file
 * @brief Q-format fixed-point arithmetic.
 *
 * A signed integer with an implicit binary scale factor: a value \c v with
 * \c FractionBits = 8 represents the rational number \c v / 256. Use when your
 * target lacks an FPU, or every cycle counts, or you need bit-exact determinism
 * and can tolerate the bounded precision.
 *
 * What a given format can represent. A \c fixed<Storage, FractionBits> with a
 * \c B-bit storage type splits its bits into one sign bit, \c I = B - 1 -
 * FractionBits integer bits, and \c FractionBits fraction bits. From that:
 * - The step between adjacent representable values (the resolution) is exactly
 *   \c 1 / 2^FractionBits, constant across the whole range (unlike floating
 *   point, whose step grows with magnitude). Query it with \c resolution().
 * - The representable range is \c [-2^I, 2^I - resolution], i.e.
 *   \c [min(), max()]. Going outside it overflows the storage integer.
 * - Decimal precision is fixed at about \c FractionBits * 0.301 digits after the
 *   point (log10(2) per bit).
 *
 * The three provided aliases (B = total bits, I = integer bits):
 *
 *     alias    storage  frac  int   range                  resolution
 *     q8_8     int16     8     7    [-128, +127.996]        1/256    ~= 3.9e-3
 *     q16_16   int32    16    15    [-32768, +32767.99998]  1/65536  ~= 1.5e-5
 *     q32_32   int64    32    31    [-2.15e9, +2.15e9]      1/2^32   ~= 2.3e-10
 *
 * Arithmetic operators preserve the type and do not check for overflow: signed
 * overflow of the storage type is undefined behaviour, so pick a storage type
 * whose range covers your values. Multiplication and division promote to a
 * doubly-wide integer internally, so only the final (narrowed) result must fit,
 * not the intermediate.
 */

#include <compare>
#include <concepts>
#include <cstdint>
#include <limits>
#include <type_traits>

namespace nexenne::math {

/**
 * @brief Fixed-point number: a signed integer with an implicit binary scale.
 *
 * @tparam Storage Signed integer storage type (for example int16/32/64).
 * @tparam FractionBits Number of fractional bits, in 1..8*sizeof(Storage)-2.
 *         At least one integer bit is required: with zero integer bits the scale
 *         factor 2^FractionBits would not fit the signed storage (it would wrap
 *         negative), poisoning every conversion, so that degenerate (pure
 *         fractional) configuration is rejected at compile time.
 */
template <std::signed_integral Storage, std::size_t FractionBits>
  requires(FractionBits > 0 && FractionBits < sizeof(Storage) * 8 - 1)
class fixed {
public:
  using storage_type = Storage;  ///< The underlying signed integer storage type.
  static constexpr std::size_t fraction_bits = FractionBits;  ///< Number of fractional bits.
  /// Number of integer bits (excludes the sign bit and the fraction bits).
  static constexpr std::size_t integer_bits = sizeof(Storage) * 8 - 1 - FractionBits;
  static constexpr Storage scale = Storage{1} << FractionBits;  ///< Integer-to-fixed scale factor.

private:
  storage_type m_raw{0};

  struct raw_tag {};

  constexpr fixed(raw_tag, storage_type const raw) noexcept : m_raw{raw} {}

public:
  /**
   * @brief Constructs a zero value.
   *
   * @pre None.
   * @post The represented value is zero.
   */
  constexpr fixed() noexcept = default;

  /**
   * @brief Constructs from an integer (lossless, shifted up by \c FractionBits).
   *
   * @tparam I Integral source type.
   * @param v Integer value to represent.
   *
   * @pre \p v fits in the integer range of the storage type.
   * @post The represented value equals \p v.
   */
  template <std::integral I>
  constexpr explicit fixed(I const v) noexcept
      : m_raw{static_cast<storage_type>(static_cast<storage_type>(v) << FractionBits)} {}

  /**
   * @brief Constructs from a floating-point value (truncates toward zero).
   *
   * @tparam F Floating-point source type.
   * @param v Value to represent.
   *
   * @pre \p v is finite and within the representable range.
   * @post The represented value is \p v truncated to the fixed-point grid.
   */
  template <std::floating_point F>
  constexpr explicit fixed(F const v) noexcept
      : m_raw{static_cast<storage_type>(v * static_cast<F>(scale))} {}

  /**
   * @brief Constructs directly from the raw underlying integer (no shift).
   *
   * @param raw Raw underlying integer value.
   *
   * @return Fixed-point value whose raw representation is \p raw.
   *
   * @pre None.
   * @post \c raw() of the result equals \p raw.
   */
  [[nodiscard]] static constexpr auto from_raw(storage_type const raw) noexcept -> fixed {
    return fixed{raw_tag{}, raw};
  }

  /**
   * @brief The most negative representable value, \c -2^integer_bits.
   *
   * The raw representation is the storage type's minimum integer.
   *
   * @return The smallest value this format can hold.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto min() noexcept -> fixed {
    return from_raw(std::numeric_limits<storage_type>::min());
  }

  /**
   * @brief The largest representable value, \c 2^integer_bits - resolution.
   *
   * The raw representation is the storage type's maximum integer.
   *
   * @return The largest value this format can hold.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto max() noexcept -> fixed {
    return from_raw(std::numeric_limits<storage_type>::max());
  }

  /**
   * @brief The step between adjacent representable values, \c 1/2^FractionBits.
   *
   * Constant across the entire range, since fixed-point spacing does not grow
   * with magnitude. This is the smallest positive representable value (raw 1).
   *
   * @return The resolution of this format.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static constexpr auto resolution() noexcept -> fixed {
    return from_raw(storage_type{1});
  }

  /**
   * @brief Returns the raw underlying integer representation.
   *
   * @return The raw stored integer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto raw() const noexcept -> storage_type {
    return m_raw;
  }

  /**
   * @brief Returns the integer part by arithmetic right shift.
   *
   * Discards the fractional bits. Because the shift is arithmetic, negative
   * values round toward negative infinity (so a represented -0.5 yields -1), not
   * toward zero.
   *
   * @return The integer part of the represented value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto to_int() const noexcept -> storage_type {
    return m_raw >> FractionBits;
  }

  /**
   * @brief Converts to a floating-point value.
   *
   * @tparam F Floating-point result type. Defaults to \c double.
   *
   * @return The represented value as type \p F.
   *
   * @pre None.
   * @post None.
   */
  template <std::floating_point F = double>
  [[nodiscard]] constexpr auto to_float() const noexcept -> F {
    return static_cast<F>(m_raw) / static_cast<F>(scale);
  }

  /**
   * @brief Fixed-point addition.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return Sum of \p a and \p b.
   *
   * @pre The mathematical sum is representable in the storage type.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator+(fixed a, fixed b) noexcept -> fixed {
    return from_raw(static_cast<storage_type>(a.m_raw + b.m_raw));
  }

  /**
   * @brief Fixed-point subtraction.
   *
   * @param a Minuend.
   * @param b Subtrahend.
   *
   * @return Difference of \p a and \p b.
   *
   * @pre The mathematical difference is representable in the storage type.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator-(fixed a, fixed b) noexcept -> fixed {
    return from_raw(static_cast<storage_type>(a.m_raw - b.m_raw));
  }

  /**
   * @brief Fixed-point negation.
   *
   * @param a Value to negate.
   *
   * @return The negation of \p a.
   *
   * @pre None.
   * @post For \c min() the result is \c min() itself (its negation is not
   *       representable in two's complement), defined rather than undefined.
   */
  [[nodiscard]] friend constexpr auto operator-(fixed a) noexcept -> fixed {
    // Negate in the unsigned domain: negating the storage minimum directly is
    // signed-overflow UB (e.g. -INT32_MIN for q16_16, a value the type produces
    // via min()). The unsigned two's-complement negation is well defined and
    // narrows back to the same bit pattern.
    using unsigned_storage = std::make_unsigned_t<storage_type>;
    return from_raw(
      static_cast<storage_type>(unsigned_storage{0} - static_cast<unsigned_storage>(a.m_raw))
    );
  }

  /**
   * @brief Fixed-point multiplication.
   *
   * Promotes to a doubly-wide integer internally to avoid intermediate overflow,
   * then shifts back down by \c FractionBits.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return Product of \p a and \p b.
   *
   * @pre The mathematical product is representable in the storage type.
   * @post None.
   *
   * @note Rounds to nearest, ties away from zero, consistently with
   *       \c operator/. Rounding is symmetric, so \c (-a)*b equals \c -(a*b).
   */
  [[nodiscard]] friend constexpr auto operator*(fixed a, fixed b) noexcept -> fixed {
    // Each raw integer is (real value)*scale, so the raw product is
    // (a*b)*scale^2, one scale factor too many. Shifting right by FractionBits
    // (dividing by scale) brings it back to (a*b)*scale, the correct raw form.
    // The raw product can need up to 2*sizeof(Storage)*8 bits, so promote to a
    // doubly-wide integer first to avoid overflowing the intermediate.
    // __int128 is a GCC/Clang extension (C++ has no standard 128-bit integer
    // yet); __extension__ marks the use intentional so -Wpedantic stays quiet.
    __extension__ using wide = std::conditional_t<sizeof(Storage) <= 4, std::int64_t, __int128>;
    auto const product{static_cast<wide>(a.m_raw) * static_cast<wide>(b.m_raw)};
    // Round to nearest by adding half an output ulp before the back-shift, with
    // the sign of the product so ties go away from zero (a plain arithmetic shift
    // would floor toward negative infinity, disagreeing with operator/).
    constexpr wide half{wide{1} << (FractionBits - 1)};
    auto const rounded{
      product < 0 ? -(((-product) + half) >> FractionBits) : (product + half) >> FractionBits
    };
    return from_raw(static_cast<storage_type>(rounded));
  }

  /**
   * @brief Fixed-point division.
   *
   * Promotes to a doubly-wide integer internally to preserve precision.
   *
   * @param a Dividend.
   * @param b Divisor.
   *
   * @return Quotient of \p a and \p b.
   *
   * @pre \p b is non-zero and the quotient is representable in the storage type.
   * @post None.
   *
   * @note Rounds to nearest, ties away from zero, consistently with
   *       \c operator*. Rounding is symmetric, so \c (-a)/b equals \c -(a/b).
   */
  [[nodiscard]] friend constexpr auto operator/(fixed a, fixed b) noexcept -> fixed {
    // Dividing the raw integers cancels both scale factors, leaving a plain
    // ratio with no scale. Pre-shift the dividend left by FractionBits (multiply
    // by scale) so the quotient comes out in raw form again. The pre-shift can
    // overflow the storage type, so promote to a doubly-wide integer first.
    // __int128 is a GCC/Clang extension (C++ has no standard 128-bit integer
    // yet); __extension__ marks the use intentional so -Wpedantic stays quiet.
    __extension__ using wide = std::conditional_t<sizeof(Storage) <= 4, std::int64_t, __int128>;
    auto const numerator{static_cast<wide>(a.m_raw) << FractionBits};
    auto const denominator{static_cast<wide>(b.m_raw)};
    // Round to nearest, ties away from zero, to match operator* (plain integer
    // division truncates toward zero). Divide, then if twice the remainder's
    // magnitude reaches the divisor's, step the quotient one ulp away from zero
    // (in the direction of the quotient's sign).
    auto const quotient{numerator / denominator};
    auto const remainder{numerator % denominator};
    auto const remainder_magnitude{remainder < 0 ? -remainder : remainder};
    auto const denominator_magnitude{denominator < 0 ? -denominator : denominator};
    auto const step{(numerator < 0) == (denominator < 0) ? wide{1} : wide{-1}};
    auto const rounded{
      remainder_magnitude * 2 >= denominator_magnitude ? quotient + step : quotient
    };
    return from_raw(static_cast<storage_type>(rounded));
  }

  /**
   * @brief In-place fixed-point addition.
   *
   * @param o Value added to \c *this.
   *
   * @return Reference to \c *this after the addition.
   *
   * @pre The mathematical sum is representable in the storage type.
   * @post \c *this holds the sum of its prior value and \p o.
   */
  constexpr auto operator+=(fixed const o) noexcept -> fixed& {
    *this = *this + o;
    return *this;
  }

  /**
   * @brief In-place fixed-point subtraction.
   *
   * @param o Value subtracted from \c *this.
   *
   * @return Reference to \c *this after the subtraction.
   *
   * @pre The mathematical difference is representable in the storage type.
   * @post \c *this holds the difference of its prior value and \p o.
   */
  constexpr auto operator-=(fixed const o) noexcept -> fixed& {
    *this = *this - o;
    return *this;
  }

  /**
   * @brief In-place fixed-point multiplication.
   *
   * @param o Value multiplied into \c *this.
   *
   * @return Reference to \c *this after the multiplication.
   *
   * @pre The mathematical product is representable in the storage type.
   * @post \c *this holds the product of its prior value and \p o.
   */
  constexpr auto operator*=(fixed const o) noexcept -> fixed& {
    *this = *this * o;
    return *this;
  }

  /**
   * @brief In-place fixed-point division.
   *
   * @param o Divisor applied to \c *this.
   *
   * @return Reference to \c *this after the division.
   *
   * @pre \p o is non-zero and the quotient is representable in the storage type.
   * @post \c *this holds the quotient of its prior value and \p o.
   */
  constexpr auto operator/=(fixed const o) noexcept -> fixed& {
    *this = *this / o;
    return *this;
  }

  /**
   * @brief Three-way comparison of two fixed-point values.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return Three-way comparison result of the raw representations.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator<=>(fixed lhs, fixed rhs) noexcept = default;
  /**
   * @brief Equality comparison of two fixed-point values.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when the raw representations are equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator==(fixed lhs, fixed rhs) noexcept -> bool = default;
};

/// @brief Q8.8: 16-bit storage, 8 fraction bits. Range [-128, +127.996],
///        resolution 1/256 (~3.9e-3, ~2 decimal digits). Smallest footprint.
using q8_8 = fixed<std::int16_t, 8>;
/// @brief Q16.16: 32-bit storage, 16 fraction bits. Range [-32768, +32767.99998],
///        resolution 1/65536 (~1.5e-5, ~4 decimal digits). The sensible default.
using q16_16 = fixed<std::int32_t, 16>;
/// @brief Q32.32: 64-bit storage, 32 fraction bits. Range about [-2.15e9, +2.15e9],
///        resolution 1/2^32 (~2.3e-10, ~9 decimal digits). Uses the __int128
///        multiply path. Widest range and finest resolution.
using q32_32 = fixed<std::int64_t, 32>;

}  // namespace nexenne::math
