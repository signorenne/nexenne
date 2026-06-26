#pragma once

/**
 * @file
 * @brief Rate-of-change guard that rejects samples jumping too far from the previous reading.
 */

#include <cmath>
#include <concepts>
#include <cstddef>

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
 * A genuine step larger than \c max_delta (a setpoint change, a
 * sensor rescale, or a corrupted first sample that primed the guard
 * with garbage) would otherwise be rejected forever, locking the
 * output. The optional escape hatch breaks that deadlock: after
 * \c escape_after() consecutive rejections the next sample is accepted
 * and the guard re-centres on it. An \c escape_after of zero disables
 * the escape, so \c reset() is then the only recovery.
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
  std::size_t m_escape_after{0};
  std::size_t m_reject_streak{0};
  bool m_primed{false};
  bool m_accepted{false};

public:
  /**
   * @brief Constructs a rate guard with a maximum per-sample jump.
   *
   * A negative limit is clamped to zero, which rejects every change
   * after the first sample.
   *
   * @param max_delta Maximum allowed absolute change between
   * consecutive samples.
   * @param escape_after Number of consecutive rejections after which
   * the next sample is accepted and the guard re-centres on it.
   * Zero (the default) disables the escape.
   *
   * @pre None. Negative \p max_delta inputs are clamped to zero.
   * @post \c max_delta() returns \c max(max_delta, 0),
   * \c escape_after() returns \p escape_after, and the guard is
   * unprimed.
   */
  constexpr explicit rate_guard(
    value_type const max_delta, std::size_t const escape_after = 0
  ) noexcept
      : m_max_delta{max_delta < value_type{0} ? value_type{0} : max_delta}
      , m_escape_after{escape_after} {}

  /**
   * @brief Feeds one sample, rejecting it if it jumps too far.
   *
   * The first sample after construction or \c reset() is accepted
   * directly. Afterward a sample is accepted only when it differs from
   * the current output by at most \c max_delta(); otherwise the output
   * is held and the rejected-run counter grows. When the escape hatch
   * is enabled and that run reaches \c escape_after(), the sample is
   * accepted anyway and the guard re-centres on it.
   *
   * @param sample New input sample.
   *
   * @return The current accepted value.
   *
   * @pre None.
   * @post \c value() returns the value returned here, \c accepted()
   * reports whether \p sample was accepted, and \c rejected_streak()
   * reflects the current run of consecutive rejections.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    if (!m_primed) {
      m_value = sample;
      m_primed = true;
      m_reject_streak = 0;
      m_accepted = true;
    } else if (std::abs(sample - m_value) <= m_max_delta) {
      m_value = sample;
      m_reject_streak = 0;
      m_accepted = true;
    } else if (m_escape_after != 0 && m_reject_streak + 1 >= m_escape_after) {
      // Escape hatch: a genuine step (or a garbage prime) would otherwise
      // reject every later reading forever. After enough consecutive
      // rejections, accept the sample and re-centre so real dynamics are
      // not lost.
      m_value = sample;
      m_reject_streak = 0;
      m_accepted = true;
    } else {
      ++m_reject_streak;
      m_accepted = false;
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
   * directly; the rate limit and escape setting are preserved.
   */
  constexpr auto reset() noexcept -> void {
    m_value = value_type{0};
    m_reject_streak = 0;
    m_primed = false;
    m_accepted = false;
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
    m_reject_streak = 0;
    m_primed = true;
    m_accepted = false;
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

  /**
   * @brief Reports whether the most recent \c push was accepted.
   *
   * @return \c true when the last sample was accepted (either within
   * the rate limit, the first sample, or forced through by the escape
   * hatch), \c false when it was rejected. \c false before any \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto accepted() const noexcept -> bool {
    return m_accepted;
  }

  /**
   * @brief Returns the current run of consecutive rejections.
   *
   * @return The number of samples rejected in a row since the last
   * accepted sample, reset to zero on every acceptance.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rejected_streak() const noexcept -> std::size_t {
    return m_reject_streak;
  }

  /**
   * @brief Returns the consecutive-rejection escape threshold.
   *
   * @return The number of consecutive rejections after which the next
   * sample is force-accepted, or zero when the escape is disabled.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto escape_after() const noexcept -> std::size_t {
    return m_escape_after;
  }

  /**
   * @brief Replaces the consecutive-rejection escape threshold.
   *
   * @param n New escape threshold; zero disables the escape hatch.
   *
   * @pre None.
   * @post \c escape_after() returns \p n; the held value and current
   * rejected run are unchanged.
   */
  constexpr auto escape_after(std::size_t const n) noexcept -> void {
    m_escape_after = n;
  }
};

}  // namespace nexenne::filter
