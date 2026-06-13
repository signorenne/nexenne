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
#include <type_traits>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

namespace detail {

// Saturate a long double value into Int: NaN to 0, out-of-range to the nearest
// bound, otherwise truncate. Used for every floating-point intermediate so a
// NaN or infinity never reaches an integer cast (which would be undefined).
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
  using lim = std::numeric_limits<Int>;

  // A floating-point source goes through a long double duration so a NaN or
  // infinity is saturated (NaN to 0) rather than cast to an integer ToDur,
  // which would be undefined behaviour and silently lose the NaN.
  if constexpr (std::is_floating_point_v<typename FromDur::rep>) {
    using fdur = std::chrono::duration<long double, typename ToDur::period>;
    return detail::saturate_from_ld<Int>(std::chrono::duration_cast<fdur>(d).count());
  } else {
    auto const c{std::chrono::duration_cast<ToDur>(d).count()};
    using C = decltype(c);

    if constexpr (std::is_floating_point_v<C>) {
      return detail::saturate_from_ld<Int>(static_cast<long double>(c));
    } else if constexpr (std::is_unsigned_v<Int>) {
      if constexpr (std::is_signed_v<C>) {
        if (c <= C{0}) {
          return Int{0};
        }
        using UC = std::make_unsigned_t<C>;
        using W = std::common_type_t<UC, std::uintmax_t>;
        auto const wc{static_cast<W>(static_cast<UC>(c))};
        auto const wmax{static_cast<W>(lim::max())};
        return wc > wmax ? lim::max() : static_cast<Int>(wc);
      } else {
        using W = std::common_type_t<C, std::uintmax_t>;
        auto const wc{static_cast<W>(c)};
        auto const wmax{static_cast<W>(lim::max())};
        return wc > wmax ? lim::max() : static_cast<Int>(wc);
      }
    } else {
      if constexpr (std::is_unsigned_v<C>) {
        using UW = std::common_type_t<C, std::make_unsigned_t<Int>, std::uintmax_t>;
        auto const wc{static_cast<UW>(c)};
        auto const wmax{static_cast<UW>(lim::max())};
        return wc > wmax ? lim::max() : static_cast<Int>(wc);
      } else {
        using W = std::common_type_t<C, Int, std::intmax_t>;
        auto const wc{static_cast<W>(c)};
        auto const wmin{static_cast<W>(lim::min())};
        auto const wmax{static_cast<W>(lim::max())};
        if (wc < wmin) {
          return lim::min();
        }
        if (wc > wmax) {
          return lim::max();
        }
        return static_cast<Int>(wc);
      }
    }
  }
}

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
