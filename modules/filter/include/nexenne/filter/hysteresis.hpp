#pragma once

/**
 * @file
 * @brief Hysteresis (Schmitt trigger) filter.
 */

#include <concepts>

namespace nexenne::filter {

/**
 * @brief Hysteresis (Schmitt trigger) filter.
 *
 * Converts a noisy analogue signal into a clean boolean by
 * using two thresholds instead of one:
 *
 * - Output goes \c true when input rises above \c high.
 * - Output goes \c false when input falls below \c low.
 * - Between \c low and \c high, the output holds its previous
 * value (the "dead band").
 *
 * This eliminates the rapid toggling that a single-threshold
 * comparator produces when the signal hovers near the boundary.
 *
 * Common uses: thermostat control (on above 22 degC, off below
 * 20 degC), battery-level indicators, any binary decision on a
 * noisy measurement.
 *
 * Unlike same-type filters (\c ema, \c sma, etc.), hysteresis is
 * a /converter/: it takes an analogue \c T and produces a
 * \c bool. It therefore does NOT satisfy \c filter_like (which
 * requires input and output to share \c value_type). Use
 * \c input_type and \c output_type instead.
 *
 * @tparam T Ordered arithmetic type for the input signal.
 * Default \c double.
 *
 * @note Reach for this for a binary decision on a noisy analogue signal
 * such as a thermostat or a level or threshold detector, where a single
 * threshold would chatter; the deadband between the two thresholds gives
 * immunity.
 */
template <std::totally_ordered T = double>
class hysteresis {
public:
  using input_type = T;      ///< the analogue signal type fed to \c push()
  using output_type = bool;  ///< the binary output type returned by \c push()

  // \c hysteresis is intentionally NOT \c filter_like: its input
  // type (T, analogue) and output type (bool, digital) differ.
  // Same-type filters use \c value_type; converters like this one
  // use \c input_type / \c output_type instead.

private:
  input_type m_low{input_type{0}};
  input_type m_high{input_type{0}};
  bool m_state{false};

public:
  /**
   * @brief Constructs a Schmitt trigger from its two thresholds.
   *
   * @param low Threshold at or below which the output becomes
   * \c false.
   * @param high Threshold at or above which the output becomes
   * \c true.
   *
   * @pre \p low is less than or equal to \p high. When the two are
   * equal the dead band vanishes and the trigger behaves as a
   * plain comparator.
   * @post \c low_threshold() returns \p low, \c high_threshold()
   * returns \p high, and \c value() returns \c false.
   */
  constexpr hysteresis(input_type const low, input_type const high) noexcept
      : m_low{low}, m_high{high} {}

  /**
   * @brief Feeds one sample and returns the trigger output.
   *
   * The output latches \c true above \c high_threshold() and
   * \c false below \c low_threshold(); between the thresholds it
   * holds its previous state.
   *
   * @param sample New analogue input sample.
   *
   * @return The binary trigger state after processing \p sample.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(input_type const sample) noexcept -> output_type {
    if (sample >= m_high) {
      m_state = true;
    } else if (sample <= m_low) {
      m_state = false;
    }
    return m_state;
  }

  /**
   * @brief Returns the current trigger state without advancing.
   *
   * @return The latched binary output.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> output_type {
    return m_state;
  }

  /**
   * @brief Clears the trigger to the \c false state.
   *
   * @pre None.
   * @post \c value() returns \c false.
   */
  constexpr auto reset() noexcept -> void {
    m_state = false;
  }

  /**
   * @brief Resets the trigger to a known state.
   *
   * @param initial State the trigger holds after the reset.
   *
   * @pre None.
   * @post \c value() returns \p initial.
   */
  constexpr auto reset(output_type const initial) noexcept -> void {
    m_state = initial;
  }

  /**
   * @brief Returns the lower (falling) threshold.
   *
   * @return The threshold at or below which the output becomes
   * \c false.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto low_threshold() const noexcept -> input_type {
    return m_low;
  }

  /**
   * @brief Returns the upper (rising) threshold.
   *
   * @return The threshold at or above which the output becomes
   * \c true.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto high_threshold() const noexcept -> input_type {
    return m_high;
  }

  /**
   * @brief Replaces both thresholds.
   *
   * @param low New lower threshold.
   * @param high New upper threshold.
   *
   * @pre \p low is less than or equal to \p high.
   * @post \c low_threshold() returns \p low and \c high_threshold()
   * returns \p high; the latched state is unchanged.
   */
  constexpr auto thresholds(input_type const low, input_type const high) noexcept -> void {
    m_low = low;
    m_high = high;
  }
};

}  // namespace nexenne::filter
