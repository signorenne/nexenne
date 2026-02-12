#pragma once

/**
 * @file
 * @brief Slew-rate limiter.
 */

#include <algorithm>
#include <cmath>
#include <concepts>

namespace nexenne::filter {

/**
 * @brief Slew-rate limiter.
 *
 * Limits how fast the output can change from one sample to the
 * next. If the target value jumps by more than \c max_rate, the
 * output moves toward it by exactly \c max_rate per step:
 *
 * y[n] = clamp(x[n], y[n-1] - rate, y[n-1] + rate)
 *
 * Common uses:
 * - Smoothing motor / actuator commands to avoid mechanical
 * shock.
 * - Ramping LED brightness to avoid flicker.
 * - Limiting joystick / control-surface response.
 *
 * @tparam T Arithmetic sample type. Default \c double.
 *
 * @note Reach for this to ramp a setpoint or to protect an actuator
 * or motor from a sudden jump by capping the change per sample.
 */
template <std::floating_point T = double>
class slew {
public:
  using value_type = T;

private:
  value_type m_max_rate{value_type{0}};
  value_type m_value{value_type{0}};
  bool m_primed{false};

public:
  /**
   * @brief Constructs a slew limiter with a maximum per-sample step.
   *
   * A negative rate is clamped to zero, which freezes the output at
   * its primed value.
   *
   * @param max_rate_per_sample Maximum absolute change permitted
   * between consecutive \c push calls.
   *
   * @pre None. Negative inputs are clamped to zero.
   * @post \c max_rate() returns \c max(max_rate_per_sample, 0) and the
   * filter is unprimed.
   */
  constexpr explicit slew(T const max_rate_per_sample) noexcept
      : m_max_rate{max_rate_per_sample < T{0} ? T{0} : max_rate_per_sample} {}

  /**
   * @brief Moves the output toward \p target by at most the max rate.
   *
   * The first call after construction or \c reset() jumps directly to
   * \p target. Later calls clamp the change to the configured maximum
   * rate per sample.
   *
   * @param target Desired output value.
   *
   * @return The rate-limited output.
   *
   * @pre None.
   * @post \c value() returns the value returned here, which differs
   * from the previous output by at most \c max_rate().
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const target) noexcept -> T {
    if (!m_primed) {
      m_value = target;
      m_primed = true;
    } else {
      m_value = std::clamp(target, m_value - m_max_rate, m_value + m_max_rate);
    }
    return m_value;
  }

  /**
   * @brief Returns the most recent output without advancing.
   *
   * @return The last value produced by \c push, or zero before the
   * first \c push or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_value;
  }

  /**
   * @brief Clears the filter back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns zero and the next \c push jumps directly
   * to its target.
   */
  constexpr auto reset() noexcept -> void {
    m_value = T{0};
    m_primed = false;
  }

  /**
   * @brief Resets the filter to a known primed value.
   *
   * @param initial Value the output holds after the reset.
   *
   * @pre None.
   * @post \c value() returns \p initial and the filter is primed, so
   * the next \c push is rate-limited.
   */
  constexpr auto reset(T const initial) noexcept -> void {
    m_value = initial;
    m_primed = true;
  }

  /**
   * @brief Returns the current maximum per-sample rate.
   *
   * @return The configured rate limit, always non-negative.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_rate() const noexcept -> T {
    return m_max_rate;
  }

  /**
   * @brief Replaces the maximum per-sample rate.
   *
   * A negative argument is clamped to zero.
   *
   * @param r New rate limit.
   *
   * @pre None. Negative inputs are clamped to zero.
   * @post \c max_rate() returns \c max(r, 0); the stored output is
   * unchanged.
   */
  constexpr auto max_rate(value_type const r) noexcept -> void {
    m_max_rate = r < value_type{0} ? value_type{0} : r;
  }
};

}  // namespace nexenne::filter
