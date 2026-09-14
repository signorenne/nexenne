#pragma once

/**
 * @file
 * @brief Saturating duration -> integer conversion.
 *
 * Some embedded APIs accept a count in a specific integer width
 * (e.g. \c vTaskDelay takes \c uint32_t ticks, \c esp_timer_create
 * takes \c int64_t microseconds). Naively casting a wider duration
 * count can wrap. \c to_count_sat does the cast through a wide
 * intermediate and clamps to \c Int's representable range instead
 * of wrapping.
 *
 * \code
 * auto us{nexenne::chrono::to_count_sat<std::uint32_t,
 *                                       std::chrono::microseconds>(
 *     std::chrono::seconds{5000})};
 * // Saturates to UINT32_MAX rather than wrapping.
 * \endcode
 *
 * Floating-point reps are handled too: NaN converts to 0; values
 * outside the integer range clamp to the nearest representable.
 *
 * @tparam Int Target integer type.
 * @tparam ToDur Duration units to first \c duration_cast into.
 */

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ratio>
#include <type_traits>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

namespace detail {

/// @cond INTERNAL

/**
 * @brief Saturate a \c long \c double value into the integer type \p Int.
 *
 * Maps NaN to zero and an out-of-range magnitude to the nearest representable
 * bound, otherwise truncates toward zero. Used for every floating-point
 * intermediate so a NaN or infinity never reaches an integer cast, which would
 * be undefined behaviour.
 *
 * @tparam Int Target integer type.
 * @param v Value to saturate.
 *
 * @return \p v clamped into the representable range of \p Int.
 *
 * @pre None.
 * @post The result lies within the closed range of \p Int.
 */
template <std::integral Int>
[[nodiscard]] constexpr auto saturate_from_ld(long double const v) noexcept -> Int {
  using lim = std::numeric_limits<Int>;
  if (!(v == v)) {
    return Int{0};  // NaN
  }
  if constexpr (std::is_unsigned_v<Int>) {
    if (v <= 0.0L) {
      return Int{0};
    }
    if (v >= static_cast<long double>(lim::max())) {
      return lim::max();
    }
    return static_cast<Int>(v);
  } else {
    if (v <= static_cast<long double>(lim::min())) {
      return lim::min();
    }
    if (v >= static_cast<long double>(lim::max())) {
      return lim::max();
    }
    return static_cast<Int>(v);
  }
}

/**
 * @brief \c c times \c N/D, truncated toward zero, saturated into \p Int.
 *
 * The exact result \c duration_cast would compute for an integral count, but
 * without its intermediate \c c*N, which overflows (undefined behaviour) for a
 * large count whenever \c N is above one, whether the target unit is finer or
 * a non-integral fraction of the source. The magnitude is split as
 * \c q*D + r, so \c q*N is checked against the bound before it is formed and
 * \c r*N stays below \c N*D. The sign is handled separately, so an unsigned
 * source never meets a negative bound.
 *
 * @tparam Int Target integer type.
 * @tparam N Numerator of the source-to-target period ratio, positive.
 * @tparam D Denominator of the source-to-target period ratio, positive.
 * @tparam Rep Integral source count type.
 * @param c Source count.
 *
 * @return \c trunc(c * N / D), clamped to the range of \p Int.
 *
 * @pre None.
 * @post None.
 */
template <std::integral Int, std::intmax_t N, std::intmax_t D, std::integral Rep>
[[nodiscard]] constexpr auto scale_saturate(Rep const c) noexcept -> Int {
  static_assert(N > 0 && D > 0, "a period ratio is positive");
  using U = std::uintmax_t;
  constexpr auto un{static_cast<U>(N)};
  constexpr auto ud{static_cast<U>(D)};
  static_assert(
    un <= std::numeric_limits<U>::max() / ud, "period ratio too extreme to scale exactly"
  );
  using lim = std::numeric_limits<Int>;

  bool negative{false};
  U magnitude{0};
  if constexpr (std::is_signed_v<Rep>) {
    negative = c < 0;
    // -(c + 1) + 1 avoids negating the minimum.
    magnitude = negative ? static_cast<U>(-(c + 1)) + 1U : static_cast<U>(c);
  } else {
    magnitude = static_cast<U>(c);
  }
  if (negative && std::is_unsigned_v<Int>) {
    return Int{0};
  }
  auto bound{static_cast<U>(lim::max())};
  if constexpr (std::is_signed_v<Int>) {
    if (negative) {
      bound += 1U;  // |min| is one more than max
    }
  }
  auto const q{magnitude / ud};
  auto const r{magnitude % ud};
  auto const saturated{[&]() noexcept -> Int { return negative ? lim::min() : lim::max(); }};
  if (q > bound / un) {
    return saturated();
  }
  auto const high{q * un};
  auto const low{(r * un) / ud};
  if (low > bound - high) {
    return saturated();
  }
  auto const total{high + low};
  if (!negative) {
    return static_cast<Int>(total);
  }
  if (total == 0U) {
    return Int{0};
  }
  // total <= |min|: step back from -(total - 1) so -|min| never overflows.
  return static_cast<Int>(-static_cast<Int>(total - 1U) - 1);
}

/// @endcond

}  // namespace detail

/**
 * @brief Saturating conversion of a duration to a target integer count.
 *
 * Casts \p d to the units \p ToDur, then clamps the resulting count to the
 * representable range of \p Int instead of wrapping. The intermediate goes
 * through a wide type so a narrowing cast cannot overflow silently. A
 * floating-point source rep with a NaN value converts to zero; finite values
 * outside the integer range clamp to the nearest representable bound.
 *
 * @tparam Int Target integer type.
 * @tparam ToDur Duration units to first \c duration_cast into.
 * @tparam FromDur Source duration type, deduced from \p d.
 * @param d Duration to convert.
 *
 * @return The count of \p d in units \p ToDur, clamped to \p Int's range.
 *
 * @pre None.
 * @post The result lies within the closed range of \p Int; it never wraps.
 *
 * @par Example
 * \code
 *   auto const us{nexenne::chrono::to_count_sat<std::uint32_t,
 *                                                std::chrono::microseconds>(
 *       std::chrono::seconds{5000})};
 *   // Saturates to UINT32_MAX rather than wrapping.
 * \endcode
 */
template <std::integral Int, chrono_duration ToDur, chrono_duration FromDur>
[[nodiscard]] constexpr auto to_count_sat(FromDur const d) noexcept -> Int {
  // Through long double: casting a NaN or infinity to an integral ToDur is UB.
  if constexpr (std::is_floating_point_v<typename FromDur::rep>) {
    using fdur = std::chrono::duration<long double, typename ToDur::period>;
    return detail::saturate_from_ld<Int>(std::chrono::duration_cast<fdur>(d).count());
  } else {
    if constexpr (std::is_floating_point_v<typename ToDur::rep>) {
      return detail::saturate_from_ld<Int>(
        static_cast<long double>(std::chrono::duration_cast<ToDur>(d).count())
      );
    } else {
      // Not duration_cast: its count * num intermediate overflows (UB) for a large count.
      using ratio = std::ratio_divide<typename FromDur::period, typename ToDur::period>;
      return detail::scale_saturate<Int, ratio::num, ratio::den>(d.count());
    }
  }
}

namespace detail {

/// @cond INTERNAL

/**
 * @brief \c duration_cast that saturates instead of overflowing.
 *
 * For an integral target the count comes from \c to_count_sat, so a value
 * beyond \p ToDur's range clamps to its minimum or maximum; a floating target
 * cannot overflow and is cast directly.
 *
 * @tparam ToDur Target duration type.
 * @tparam FromDur Source duration type, deduced from \p d.
 * @param d Duration to convert.
 *
 * @return \p d in \p ToDur, clamped to its range.
 *
 * @pre None.
 * @post None.
 */
template <chrono_duration ToDur, chrono_duration FromDur>
[[nodiscard]] constexpr auto saturating_cast(FromDur const d) noexcept -> ToDur {
  if constexpr (std::is_floating_point_v<typename ToDur::rep>) {
    return std::chrono::duration_cast<ToDur>(d);
  } else {
    return ToDur{to_count_sat<typename ToDur::rep, ToDur>(d)};
  }
}

/**
 * @brief \p a plus \p b, clamped to the range of \p D.
 *
 * @tparam D Duration type of both operands.
 * @param a First operand.
 * @param b Second operand.
 *
 * @return The sum, or \c D::max() / \c D::min() where it would overflow.
 *
 * @pre None.
 * @post None.
 */
template <chrono_duration D>
[[nodiscard]] constexpr auto saturating_add(D const a, D const b) noexcept -> D {
  if constexpr (std::is_integral_v<typename D::rep>) {
    if (b > D::zero() && a > D::max() - b) {
      return D::max();
    }
    if (b < D::zero() && a < D::min() - b) {
      return D::min();
    }
  }
  return a + b;
}

/**
 * @brief \p d negated, with the minimum mapping to the maximum.
 *
 * @tparam D Duration type.
 * @param d Duration to negate.
 *
 * @return \c -d, or \c D::max() for \c D::min().
 *
 * @pre None.
 * @post None.
 */
template <chrono_duration D>
[[nodiscard]] constexpr auto saturating_negate(D const d) noexcept -> D {
  if constexpr (std::is_integral_v<typename D::rep>) {
    if (d == D::min()) {
      return D::max();
    }
  }
  return -d;
}

/// @endcond

}  // namespace detail

/**
 * @brief Saturating conversion of \p d to microseconds in a 32-bit unsigned.
 *
 * Convenience wrapper over \c to_count_sat for embedded APIs that take a
 * microsecond count in a \c std::uint32_t field, such as FreeRTOS or ESP-IDF.
 *
 * @tparam FromDur Source duration type, deduced from \p d.
 * @param d Duration to convert.
 *
 * @return Microsecond count of \p d, clamped to the \c std::uint32_t range.
 *
 * @pre None.
 * @post The result lies within the \c std::uint32_t range; it never wraps.
 */
template <chrono_duration FromDur>
[[nodiscard]] constexpr auto to_us_u32(FromDur const d) noexcept -> std::uint32_t {
  return to_count_sat<std::uint32_t, std::chrono::microseconds>(d);
}

/**
 * @brief Saturating conversion of \p d to milliseconds in a 32-bit unsigned.
 *
 * Convenience wrapper over \c to_count_sat for APIs that take a millisecond
 * count in a \c std::uint32_t field, such as Win32, Arduino, and many RTOS.
 *
 * @tparam FromDur Source duration type, deduced from \p d.
 * @param d Duration to convert.
 *
 * @return Millisecond count of \p d, clamped to the \c std::uint32_t range.
 *
 * @pre None.
 * @post The result lies within the \c std::uint32_t range; it never wraps.
 */
template <chrono_duration FromDur>
[[nodiscard]] constexpr auto to_ms_u32(FromDur const d) noexcept -> std::uint32_t {
  return to_count_sat<std::uint32_t, std::chrono::milliseconds>(d);
}

}  // namespace nexenne::chrono
