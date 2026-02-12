#pragma once

/**
 * @file
 * @brief Rate-of-change guard that rejects samples jumping too far from the previous reading.
 */

#include <cmath>
#include <concepts>

namespace nexenne::filter {

/**
 * @brief Rate-of-change guard that rejects samples jumping too
 * far from the previous reading.
 *
 * If \c |sample - previous| > max_delta, the sample is rejected
 * (output holds). This catches single-read bus corruption where
 * the returned value is syntactically valid (passes range check)
 * but physically impossible given the system's dynamics (for
 * example a temperature sensor jumping from 25 degC to 250 degC in
 * one sample period).
 *
 * Complementary to \c range_guard: range_guard catches values
 * outside the sensor's absolute limits; rate_guard catches values
 * that are within range but changed too fast to be real.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Reach for this when the measured quantity cannot physically
 * change faster than a known rate (an encoder, a slow thermal mass),
 * so a larger jump must be a fault.
 */
template <std::floating_point T = double>
class rate_guard {
public:
  using value_type = T;

private:
  value_type m_max_delta{value_type{0}};
  value_type m_value{value_type{0}};
  bool m_primed{false};

public:
  /**
   * @brief Constructs a rate guard with a maximum per-sample jump.
   *
   * A negative limit is clamped to zero, which rejects every change
   * after the first sample.
   *
   * @param max_delta Maximum allowed absolute change between
   * consecutive samples.
   *
   * @pre None. Negative inputs are clamped to zero.
   * @post \c max_delta() returns \c max(max_delta, 0) and the guard is
   * unprimed.
   */
  constexpr explicit rate_guard(value_type const max_delta) noexcept
      : m_max_delta{max_delta < value_type{0} ? value_type{0} : max_delta} {}

  /**
   * @brief Feeds one sample, rejecting it if it jumps too far.
   *
   * The first sample after construction or \c reset() is accepted
   * directly. Afterward a sample is accepted only when it differs from
   * the current output by at most \c max_delta(); otherwise the output
   * is held.
   *
   * @param sample New input sample.
   *
   * @return The current accepted value.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    if (!m_primed) {
      m_value = sample;
      m_primed = true;
    } else if (std::abs(sample - m_value) <= m_max_delta) {
      m_value = sample;
    }
    return m_value;
  }

  /**
   * @brief Returns the current accepted value without advancing.
   *
   * @return The last accepted value, or zero before the first \c push
   * or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Clears the guard back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns zero and the next \c push is accepted
   * directly; the rate limit is preserved.
   */
  constexpr auto reset() noexcept -> void {
    m_value = value_type{0};
    m_primed = false;
  }

  /**
   * @brief Resets the guard to a known primed value.
   *
   * @param initial Value the output holds after the reset.
   *
   * @pre None.
   * @post \c value() returns \p initial and the next \c push is
   * rate-limited against it.
   */
  constexpr auto reset(value_type const initial) noexcept -> void {
    m_value = initial;
    m_primed = true;
  }

  /**
   * @brief Returns the current maximum per-sample change.
   *
   * @return The rate limit, always non-negative.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto max_delta() const noexcept -> value_type {
    return m_max_delta;
  }

  /**
   * @brief Replaces the maximum per-sample change.
   *
   * A negative argument is clamped to zero.
   *
   * @param d New rate limit.
   *
   * @pre None. Negative inputs are clamped to zero.
   * @post \c max_delta() returns \c max(d, 0); the held value is
   * unchanged.
   */
  constexpr auto max_delta(value_type const d) noexcept -> void {
    m_max_delta = d < value_type{0} ? value_type{0} : d;
  }
};

}  // namespace nexenne::filter
