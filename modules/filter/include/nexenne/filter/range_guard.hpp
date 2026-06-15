#pragma once

/**
 * @file
 * @brief Range-guard filter that rejects values outside a valid physical range.
 */

#include <algorithm>
#include <concepts>

namespace nexenne::filter {

/**
 * @brief Range-guard filter that rejects values outside a valid
 * physical range.
 *
 * When a bus read returns a value outside \c [lo, hi], the output
 * holds the last valid reading instead of forwarding garbage to
 * the application. This catches the most common I2C/SPI failure
 * mode: a corrupted transfer produces a nonsense register value
 * (0xFF, 0x00, or a random bit pattern) that falls outside the
 * sensor's physical output range.
 *
 * Behaviour:
 * - Sample inside \c [lo, hi] -> accepted, output updates.
 * - Sample outside range -> rejected, output holds previous.
 * - First sample, if out of range, is clamped to the nearest
 * bound so the filter starts in a defined state.
 *
 * @tparam T Ordered arithmetic type. Default \c double.
 *
 * @note Reach for this when a sensor has a known physical range (an
 * ADC channel, a temperature probe) and readings outside it are
 * impossible and should be rejected or clamped.
 */
template <std::totally_ordered T = double>
class range_guard {
public:
  using value_type = T;

private:
  value_type m_lo{};
  value_type m_hi{};
  value_type m_value{};
  bool m_primed{false};

public:
  /**
   * @brief Constructs a range guard from its inclusive bounds.
   *
   * @param lo Lower bound (inclusive).
   * @param hi Upper bound (inclusive).
   *
   * @pre \p lo is less than or equal to \p hi.
   * @post \c lo() returns \p lo, \c hi() returns \p hi, and the guard
   * is unprimed.
   */
  constexpr range_guard(value_type const lo, value_type const hi) noexcept : m_lo{lo}, m_hi{hi} {}

  /**
   * @brief Feeds one sample, rejecting values outside the range.
   *
   * A sample inside \c [lo, hi] is accepted and becomes the output.
   * An out-of-range sample is rejected and the previous valid output
   * is held, except that an out-of-range first sample is clamped to
   * the nearest bound so the guard starts in a defined state.
   *
   * @param sample New input sample.
   *
   * @return The current accepted (in-range) value.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    if (sample >= m_lo && sample <= m_hi) {
      m_value = sample;
      m_primed = true;
    } else if (!m_primed) {
      m_value = std::clamp(sample, m_lo, m_hi);
      m_primed = true;
    }
    return m_value;
  }

  /**
   * @brief Returns the current accepted value without advancing.
   *
   * @return The last in-range (or initially clamped) value, or a
   * value-initialised \c T before any \c push.
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
   * @post \c value() returns a value-initialised \c T,
   * \c last_accepted() returns \c false, and the next
   * out-of-range \c push is clamped rather than rejected.
   */
  constexpr auto reset() noexcept -> void {
    m_value = T{};
    m_primed = false;
  }

  /**
   * @brief Reports whether the guard has ever accepted a value.
   *
   * @return \c true once at least one sample has been accepted or the
   * first sample has been clamped, \c false while still
   * unprimed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto last_accepted() const noexcept -> bool {
    return m_primed;
  }

  /**
   * @brief Replaces the accepted range.
   *
   * @param lo New lower bound (inclusive).
   * @param hi New upper bound (inclusive).
   *
   * @pre \p lo is less than or equal to \p hi.
   * @post \c lo() returns \p lo and \c hi() returns \p hi; the held
   * value is unchanged even if it now falls outside the range.
   */
  constexpr auto range(value_type const lo, value_type const hi) noexcept -> void {
    m_lo = lo;
    m_hi = hi;
  }

  /**
   * @brief Returns the lower bound of the accepted range.
   *
   * @return The inclusive lower bound.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto lo() const noexcept -> value_type {
    return m_lo;
  }

  /**
   * @brief Returns the upper bound of the accepted range.
   *
   * @return The inclusive upper bound.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto hi() const noexcept -> value_type {
    return m_hi;
  }
};

}  // namespace nexenne::filter
