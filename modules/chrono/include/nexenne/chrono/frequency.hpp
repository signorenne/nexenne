#pragma once

/**
 * @file
 * @brief Compile-time / runtime helpers for Hz/period conversions.
 *
 * The chrono module already gives you \c std::chrono::duration. What's
 * missing is a clean way to say "1 kHz" and get the period
 * \c std::chrono::microseconds{1000}, or vice versa. This header adds
 * those at \c constexpr time so you can drop them straight into NTTPs.
 *
 * Use cases:
 *   - Configure a scan rate: \c period_from<hertz<1000>>() == 1 ms
 *   - Compute a timer reload value at compile time
 *   - Derive a sample rate from a known period
 */

#include <chrono>
#include <concepts>
#include <cstdint>
#include <limits>
#include <ratio>

namespace nexenne::chrono {

/**
 * @brief A frequency in hertz expressed as an NTTP-friendly type.
 *
 * Wraps a compile-time hertz value in a type usable as a non-type template
 * parameter. Frequencies are inherently non-negative, so the value is a
 * \c std::uint64_t; the \c (Hz > 0) constraint is enforced only on the
 * operations that would be ill-defined for a zero frequency.
 *
 * @tparam Hz The frequency in hertz.
 *
 * @pre None.
 * @post None.
 */
template <std::uint64_t Hz>
struct hertz {
  static constexpr std::uint64_t value = Hz;  ///< The wrapped frequency in hertz.
};

/**
 * @brief Period as a \c std::chrono::duration for the frequency type \p F.
 *
 * The result type is \c duration<int64_t, ratio<1, F::value>>, which is exact
 * with no floating-point round-off.
 *
 * @tparam F A frequency type exposing a positive \c value, such as \c hertz.
 *
 * @return The one-tick period corresponding to \p F.
 *
 * @pre \p F::value is greater than zero.
 * @post The result is a strictly positive duration.
 */
template <typename F>
  requires(
    F::value > 0
    && F::value <= static_cast<std::uint64_t>(std::numeric_limits<std::intmax_t>::max())
  )
[[nodiscard]] constexpr auto period_from() noexcept {
  return std::chrono::duration<std::int64_t, std::ratio<1, static_cast<std::intmax_t>(F::value)>>{1
  };
}

/**
 * @brief Frequency in hertz, truncated to integer, for a given period.
 *
 * @tparam Rep Representation type of \p period.
 * @tparam Period Tick-period ratio of \p period.
 * @param period The period to invert.
 *
 * @return The frequency in hertz, or zero when \p period is not positive.
 *
 * @pre None.
 * @post None.
 */
template <typename Rep, typename Period>
[[nodiscard]] constexpr auto hertz_from(std::chrono::duration<Rep, Period> const period
) noexcept -> std::uint64_t {
  if (period.count() <= 0) {
    return 0;
  }
  // freq = 1 / (count * seconds-per-tick) = Period::den / (Period::num * count),
  // evaluated as a single division so the intermediate is not truncated twice.
  auto const num{static_cast<std::uint64_t>(Period::num)};
  auto const den{static_cast<std::uint64_t>(Period::den)};
  auto const count{static_cast<std::uint64_t>(period.count())};
  return den / (num * count);
}

/**
 * @brief Period in nanoseconds for a runtime hertz value.
 *
 * @param hz The frequency in hertz.
 *
 * @return The period in nanoseconds, or \c nanoseconds::max() when \p hz is
 *         zero.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto period_ns_from(std::uint64_t const hz
) noexcept -> std::chrono::nanoseconds {
  if (hz == 0) {
    return std::chrono::nanoseconds::max();
  }
  return std::chrono::nanoseconds{static_cast<std::int64_t>(1'000'000'000ULL / hz)};
}

/**
 * @brief Period in microseconds for a runtime hertz value.
 *
 * @param hz The frequency in hertz.
 *
 * @return The period in microseconds, or \c microseconds::max() when \p hz is
 *         zero.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto period_us_from(std::uint64_t const hz
) noexcept -> std::chrono::microseconds {
  if (hz == 0) {
    return std::chrono::microseconds::max();
  }
  return std::chrono::microseconds{static_cast<std::int64_t>(1'000'000ULL / hz)};
}

/**
 * @brief Frequency in hertz, rounded down to integer, for a runtime period.
 *
 * @param period The period in nanoseconds.
 *
 * @return The frequency in hertz, or zero when \p period is not positive.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto hz_from_ns(std::chrono::nanoseconds const period
) noexcept -> std::uint64_t {
  if (period.count() <= 0) {
    return 0;
  }
  return 1'000'000'000ULL / static_cast<std::uint64_t>(period.count());
}

}  // namespace nexenne::chrono
