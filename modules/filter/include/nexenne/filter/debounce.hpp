#pragma once

/**
 * @file
 * @brief Digital debounce filter.
 */

#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief Digital debounce filter.
 *
 * Requires \p Threshold consecutive identical samples before
 * accepting a new value. Until the threshold is met, the output
 * stays at the previously accepted value. Designed for noisy
 * digital inputs:
 *
 * - Mechanical switches / buttons (contact bounce).
 * - GPIO lines with electrical noise.
 * - Boolean sensor signals that flicker near the transition
 * point.
 *
 * The default \c Threshold of 3 means three consecutive matching
 * samples are needed to confirm a transition; a single glitch is
 * ignored.
 *
 * @tparam T Sample type (usually \c bool). Any
 * \c std::equality_comparable type works.
 * @tparam Threshold Number of consecutive identical samples
 * required to accept a new value.
 *
 * @note Reach for this to reject contact bounce or chatter on a
 * discrete input such as a button or a limit switch by requiring a
 * number of consecutive agreeing samples; set the threshold from your
 * sample rate.
 */
template <std::equality_comparable T = bool, std::size_t Threshold = 3>
  requires(Threshold > 0)
class debounce {
public:
  using value_type = T;
  static constexpr std::size_t threshold{Threshold};

private:
  value_type m_stable{};     ///< last accepted value
  value_type m_candidate{};  ///< candidate being counted
  std::size_t m_streak{0};   ///< consecutive matching samples
  bool m_primed{false};

public:
  /**
   * @brief Constructs an unprimed debouncer.
   *
   * @pre None.
   * @post \c value() returns a value-initialised \c T and the first
   * \c push is accepted immediately.
   */
  constexpr debounce() noexcept = default;

  /**
   * @brief Constructs a debouncer primed to a known value.
   *
   * @param initial Value treated as the current stable output.
   *
   * @pre None.
   * @post \c value() returns \p initial and a new value still needs
   * \c Threshold consecutive samples to be accepted.
   */
  constexpr explicit debounce(T const initial) noexcept
      : m_stable{initial}, m_candidate{initial}, m_primed{true} {}

  /**
   * @brief Feeds one sample and returns the debounced output.
   *
   * The first sample after an unprimed construction or \c reset() is
   * accepted immediately. Afterward, a differing value must repeat for
   * \c Threshold consecutive samples before it replaces the stable
   * output; any interruption restarts the count.
   *
   * @param sample New input sample.
   *
   * @return The current stable (debounced) value.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    if (!m_primed) {
      m_stable = sample;
      m_candidate = sample;
      m_streak = 1;
      m_primed = true;
      return m_stable;
    }

    if (sample == m_candidate) {
      if (m_streak < Threshold) {
        ++m_streak;
      }
    } else {
      m_candidate = sample;
      m_streak = 1;
    }
    // Promote on either path so that Threshold == 1 accepts immediately: a
    // fresh candidate already has a streak of 1.
    if (m_streak >= Threshold) {
      m_stable = m_candidate;
    }
    return m_stable;
  }

  /**
   * @brief Returns the current stable value without advancing.
   *
   * @return The last accepted (debounced) value.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_stable;
  }

  /**
   * @brief Clears the debouncer back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns a value-initialised \c T and the next
   * \c push is accepted immediately.
   */
  constexpr auto reset() noexcept -> void {
    m_stable = T{};
    m_candidate = T{};
    m_streak = 0;
    m_primed = false;
  }

  /**
   * @brief Resets the debouncer to a known, fully confirmed value.
   *
   * @param initial Value treated as the current stable output.
   *
   * @pre None.
   * @post \c value() returns \p initial with the streak counter at
   * \c Threshold, so the value is already confirmed.
   */
  constexpr auto reset(T const initial) noexcept -> void {
    m_stable = initial;
    m_candidate = initial;
    m_streak = Threshold;
    m_primed = true;
  }
};

}  // namespace nexenne::filter
