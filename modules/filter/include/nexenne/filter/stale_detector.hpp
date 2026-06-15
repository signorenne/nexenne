#pragma once

/**
 * @file
 * @brief Stale-data detector that flags when a sensor reads stuck.
 */

#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief Stale-data detector that flags when a sensor reads stuck.
 *
 * Passes every sample through unchanged but tracks whether the
 * same value has been received \p N times in a row. If so,
 * \c is_stale() returns \c true: the sensor (or the bus) is
 * likely frozen, because the IC is not responding, the pull-up is
 * asserting a fixed level, or the register is stuck.
 *
 * This is a diagnostic filter, not a corrective one: it
 * doesn't modify the output. Chain it before a corrective
 * filter and check \c is_stale() to decide whether to ignore
 * the reading, raise an alarm, or power-cycle the sensor.
 *
 * Satisfies \c filter_like (push/value/reset) so it chains
 * naturally with other filters.
 *
 * @tparam T Value type. Any \c std::equality_comparable type.
 * @tparam N Number of consecutive identical samples before
 * flagging stale.
 *
 * @note Reach for this as a liveness watchdog on a sensor that should
 * keep changing, to flag a frozen or disconnected source.
 */
template <std::equality_comparable T = double, std::size_t N = 10>
  requires(N > 0)
class stale_detector {
public:
  using value_type = T;
  static constexpr std::size_t threshold{N};

private:
  value_type m_last{};
  value_type m_value{};
  std::size_t m_streak{0};
  bool m_stale{false};
  bool m_primed{false};

public:
  /**
   * @brief Constructs an unprimed stale detector.
   *
   * @pre None.
   * @post \c is_stale() returns \c false, \c streak() returns zero,
   * and \c value() returns a value-initialised \c T.
   */
  constexpr stale_detector() noexcept = default;

  /**
   * @brief Feeds one sample, passing it through unchanged.
   *
   * Tracks the run length of identical consecutive samples. The output
   * is always the input; inspect \c is_stale() afterward to learn
   * whether the data looks frozen.
   *
   * @param sample New input sample.
   *
   * @return \p sample, returned verbatim.
   *
   * @pre None.
   * @post \c value() returns \p sample and \c is_stale() reflects
   * whether the last \c N samples were identical.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    m_value = sample;
    if (!m_primed) {
      m_last = sample;
      m_streak = 1;
      m_primed = true;
    } else if (sample == m_last) {
      if (m_streak < N) {
        ++m_streak;
      }
    } else {
      m_last = sample;
      m_streak = 1;
    }
    m_stale = (m_streak >= N);
    return sample;
  }

  /**
   * @brief Returns the most recent sample without advancing.
   *
   * @return The last value passed to \c push, or a value-initialised
   * \c T before the first \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Reports whether the data appears frozen.
   *
   * @return \c true when the last \c N samples were all identical,
   * \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_stale() const noexcept -> bool {
    return m_stale;
  }

  /**
   * @brief Returns the current run of identical samples.
   *
   * @return The number of consecutive identical samples, capped at
   * \c N.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto streak() const noexcept -> std::size_t {
    return m_streak;
  }

  /**
   * @brief Clears the detector back to the unprimed condition.
   *
   * @pre None.
   * @post \c is_stale() returns \c false, \c streak() returns zero,
   * and \c value() returns a value-initialised \c T.
   */
  constexpr auto reset() noexcept -> void {
    m_last = value_type{};
    m_value = value_type{};
    m_streak = 0;
    m_stale = false;
    m_primed = false;
  }
};

}  // namespace nexenne::filter
