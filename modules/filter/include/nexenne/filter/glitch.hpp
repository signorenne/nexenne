#pragma once

/**
 * @file
 * @brief Digital glitch filter rejecting transient pulses shorter than a width.
 */

#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief Digital glitch filter that rejects transient pulses shorter
 * than \p N samples.
 *
 * Where \c debounce requires \p N consecutive /identical/ samples,
 * the glitch filter is stricter: a new value is only accepted if
 * it persists for at least \p N consecutive samples /and/ the
 * signal hasn't returned to the stable value in between. If it
 * does return, the pending candidate is cancelled (the transient
 * is treated as a glitch and suppressed).
 *
 * Think of it as a hardware glitch filter implemented in software:
 * many MCU GPIO peripherals have a configurable N-cycle glitch
 * rejection stage; this type provides the same semantics when
 * the hardware doesn't, or when filtering a value read over
 * a bus (I2C, SPI, UART) rather than sampled from a pin.
 *
 * Common uses:
 * - EMI/ESD-induced transients on digital sensor lines.
 * - Communication line noise (UART RX, I2C SDA ringing).
 * - Digital logic with ringing after edge transitions.
 * - GPIO lines without hardware glitch filters.
 *
 * @tparam T Value type. Any \c std::equality_comparable type
 * (typically \c bool for digital lines, or an integer
 * register value for bus reads).
 * @tparam N Minimum pulse width (in samples) to be accepted.
 * Transients shorter than this are suppressed.
 *
 * @note Reach for this to suppress short transient pulses such as an EMI
 * spike on a digital line, shorter than a minimum width, while still
 * passing a genuine sustained change.
 */
template <std::equality_comparable T = bool, std::size_t N = 3>
  requires(N > 0)
class glitch {
public:
  using value_type = T;
  static constexpr std::size_t hold_count{N};

private:
  value_type m_stable{};   ///< last accepted (output) value
  value_type m_pending{};  ///< candidate under observation
  std::size_t m_hold{0};   ///< consecutive samples matching pending
  bool m_primed{false};

public:
  /**
   * @brief Constructs an unprimed glitch filter.
   *
   * @pre None.
   * @post \c value() returns a value-initialised \c T and the first
   * \c push seeds the stable value.
   */
  constexpr glitch() noexcept = default;

  /**
   * @brief Constructs a glitch filter primed to a known value.
   *
   * @param initial Value treated as the current stable output.
   *
   * @pre None.
   * @post \c value() returns \p initial and \c pending() is \c false.
   */
  constexpr explicit glitch(T const initial) noexcept
      : m_stable{initial}, m_pending{initial}, m_primed{true} {}

  /**
   * @brief Feeds one sample and returns the glitch-filtered output.
   *
   * A new value propagates to the output only after \c N consecutive
   * samples confirm it. If the signal returns to the stable value
   * before the hold count is reached, the pending candidate is
   * cancelled and the transient is suppressed.
   *
   * @param sample New input sample.
   *
   * @return The current stable (glitch-filtered) value.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    if (!m_primed) {
      m_stable = sample;
      m_pending = sample;
      m_primed = true;
      return m_stable;
    }

    if (sample == m_stable) {
      // Signal returned to stable, so cancel any pending candidate.
      m_pending = m_stable;
      m_hold = 0;
    } else {
      if (sample == m_pending) {
        ++m_hold;  // candidate persists, so count up
      } else {
        m_pending = sample;  // new candidate, so restart the hold counter
        m_hold = 1;
      }
      // Accept on either path so that N == 1 promotes a single sample: a fresh
      // candidate already holds for one sample.
      if (m_hold >= N) {
        m_stable = m_pending;
        m_hold = 0;
      }
    }
    return m_stable;
  }

  /**
   * @brief Returns the current stable value without advancing.
   *
   * @return The last accepted (glitch-filtered) value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_stable;
  }

  /**
   * @brief Clears the filter back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns a value-initialised \c T, \c pending() is
   * \c false, and the next \c push seeds the stable value.
   */
  constexpr auto reset() noexcept -> void {
    m_stable = T{};
    m_pending = T{};
    m_hold = 0;
    m_primed = false;
  }

  /**
   * @brief Resets the filter to a known stable value.
   *
   * @param initial Value treated as the current stable output.
   *
   * @pre None.
   * @post \c value() returns \p initial and \c pending() is \c false.
   */
  constexpr auto reset(T const initial) noexcept -> void {
    m_stable = initial;
    m_pending = initial;
    m_hold = 0;
    m_primed = true;
  }

  /**
   * @brief Reports whether a candidate transition is being held.
   *
   * @return \c true while a differing value is counting toward the
   * hold threshold but has not yet been accepted, \c false
   * otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto pending() const noexcept -> bool {
    return m_hold > 0;
  }
};

}  // namespace nexenne::filter
