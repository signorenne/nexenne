#pragma once

/**
 * @file
 * @brief Checked narrowing cast that asserts the value is preserved in debug.
 */

#include <cassert>
#include <limits>
#include <type_traits>

namespace nexenne::utility {

namespace detail {

/// @cond INTERNAL

/**
 * @brief Two raised to \p exponent, computed exactly in \p Float.
 *
 * Used to build the open range bound of an integral type in the floating-point
 * domain: a power of two is exactly representable for every exponent the bound
 * needs, unlike the integral maximum itself (for example \c 2^63-1 rounds up to
 * \c 2^63 as a \c float, which would let an out-of-range value pass a naive
 * comparison against the cast maximum).
 *
 * @tparam Float Floating-point result type.
 * @param exponent Non-negative power to raise two to.
 *
 * @return \c 2^exponent as \p Float.
 *
 * @pre \p exponent is non-negative and within the exponent range of \p Float.
 * @post None.
 */
template <typename Float>
[[nodiscard]] constexpr auto pow2(int const exponent) noexcept -> Float {
  auto result{Float{1}};
  for (auto i{0}; i < exponent; ++i) {
    result *= Float{2};
  }
  return result;
}

/**
 * @brief Whether the floating-point \p from truncates to a value of \p To.
 *
 * The comparison happens entirely in \p From, against the half-open range
 * \c (To_min-1, To_max+1) whose endpoints are exact powers of two, so no
 * float-to-integer conversion (the operation whose validity is being decided)
 * is performed. A NaN compares false against both bounds and is reported as
 * out of range.
 *
 * @tparam To Integral target type.
 * @tparam From Floating-point source type.
 * @param from Value to classify.
 *
 * @return \c true when truncating \p from is representable in \p To.
 *
 * @pre None.
 * @post None.
 */
template <typename To, typename From>
[[nodiscard]] constexpr auto float_in_integral_range(From const from) noexcept -> bool {
  // digits of To counts its value bits, so 2^digits is To_max+1 exactly.
  constexpr auto bound{pow2<From>(std::numeric_limits<To>::digits)};
  if constexpr (std::is_signed_v<To>) {
    return from >= -bound && from < bound;
  } else {
    // Anything in (-1, 0) truncates to zero, which is representable; the
    // round-trip assert still rejects it afterwards for changing the value.
    return from > From{-1} && from < bound;
  }
}

/// @endcond

}  // namespace detail

/**
 * @brief Narrowing cast that, in debug builds, asserts the value is preserved.
 *
 * Performs \c static_cast<To>(from), then in debug builds asserts that casting
 * the result back to \p From reproduces \p from and that the sign did not
 * flip. The sign check matters because a round-trip alone accepts
 * \c narrow_cast<unsigned>(-1), which maps to the maximum value and back yet
 * is almost always a bug. For a floating-point source and an integral target
 * the value is range-checked in the floating-point domain before the cast,
 * because a \c static_cast of an out-of-range (or NaN) value is undefined
 * behaviour and would fire before any round-trip check could run; the check
 * compares against exact power-of-two bounds so the boundaries classify
 * correctly. With \c NDEBUG defined the asserts vanish and the call compiles
 * to exactly the \c static_cast.
 *
 * @tparam To Target arithmetic type.
 * @tparam From Source arithmetic type, deduced.
 * @param from Value to narrow.
 *
 * @return \p from converted to \p To.
 *
 * @pre \p from is representable in \p To: casting the result back to \p From
 *      reproduces \p from and leaves the sign unchanged. Violations assert in
 *      debug and are silent under \c NDEBUG.
 * @post Casting the result back to \p From equals \p from.
 *
 * @note A NaN source is never representable: the range check (integral target)
 *       or the round-trip check (floating-point target, since NaN != NaN)
 *       fails, so it asserts in debug and is a caller error under \c NDEBUG.
 *
 * @par Example
 * \code
 * auto const wide{std::int32_t{300}};
 * auto const narrow{nexenne::utility::narrow_cast<std::int16_t>(wide)};
 * \endcode
 */
template <typename To, typename From>
  requires std::is_arithmetic_v<To> && std::is_arithmetic_v<From>
[[nodiscard]] constexpr auto narrow_cast(From const from) noexcept -> To {
  if constexpr (std::is_floating_point_v<From> && !std::is_floating_point_v<To>) {
    // The cast below is undefined for an out-of-range or NaN source, so the
    // range must be validated first; in a constant evaluation a violation is a
    // compile error rather than a runtime abort.
    assert(
      detail::float_in_integral_range<To>(from)
      && "narrow_cast: floating-point value out of range of the target type"
    );
  }
  auto const to{static_cast<To>(from)};
  assert(static_cast<From>(to) == from && "narrow_cast: value changed during narrowing conversion");
  if constexpr (!std::is_floating_point_v<To> && !std::is_floating_point_v<From>
                && std::is_signed_v<To> != std::is_signed_v<From>) {
    assert(
      (to < To{}) == (from < From{}) && "narrow_cast: sign changed during narrowing conversion"
    );
  }
  return to;
}

}  // namespace nexenne::utility
