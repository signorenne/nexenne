#pragma once

/**
 * @file
 * @brief Exponential Moving Average (EMA).
 */

#include <concepts>

namespace nexenne::filter {

/**
 * @brief Exponential Moving Average (EMA).
 *
 * Single-pole IIR smoother. Each output is a weighted blend of
 * the new sample and the previous output:
 *
 * \c y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *
 * \c alpha (the smoothing factor, 0 < alpha <= 1) controls how quickly
 * old data is forgotten. Higher alpha tracks faster; lower alpha
 * smooths more aggressively. Equivalent to a first-order
 * low-pass filter with time constant tau = (1 - alpha) / alpha samples.
 *
 * Zero allocation, zero heap, one multiply + one add per sample.
 *
 * @tparam T Arithmetic sample type. Default \c double.
 *
 * @note Reach for this as the default lightweight smoother when you want
 * "less jitter, a little lag" and do not need an exact cutoff. An
 * \c alpha of about \c 2 / (window + 1) mimics an N-sample average.
 */
template <std::floating_point T = double>
class ema {
public:
  using value_type = T;

private:
  value_type m_alpha{};
  value_type m_value{};
  bool m_primed{false};

public:
  /**
   * @brief Constructs an EMA with a fixed smoothing factor \p alpha.
   *
   * The filter starts unprimed: the first \c push seeds the running
   * value directly so the output has no startup lag.
   *
   * @param alpha Smoothing factor in \c (0, 1]. A value of \c 1
   * disables smoothing (output equals input); smaller
   * values smooth more aggressively.
   *
   * @pre \p alpha lies in \c (0, 1]. Values outside this range are
   * stored verbatim and yield an unstable or non-smoothing
   * filter.
   * @post \c alpha() returns \p alpha and \c value() returns zero
   * until the first \c push.
   */
  constexpr explicit ema(value_type const alpha) noexcept : m_alpha{alpha} {}

  /**
   * @brief Feeds one sample and returns the updated filtered output.
   *
   * The first sample after construction or \c reset() seeds the
   * running value directly. Every later sample is blended with the
   * previous output using the smoothing factor.
   *
   * @param sample New input sample.
   *
   * @return The filtered output after incorporating \p sample.
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
    } else {
      m_value = m_alpha * sample + (value_type{1} - m_alpha) * m_value;
    }
    return m_value;
  }

  /**
   * @brief Returns the most recent filtered output without advancing.
   *
   * @return The last value produced by \c push, or zero if no sample
   * has been pushed since construction or \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Clears the filter state back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns zero and the next \c push reseeds the
   * filter directly.
   */
  constexpr auto reset() noexcept -> void {
    m_value = value_type{};
    m_primed = false;
  }

  /**
   * @brief Resets the filter to a known primed value.
   *
   * @param initial Value the filter holds immediately after the
   * reset; the next \c push blends against it.
   *
   * @pre None.
   * @post \c value() returns \p initial and the filter is primed.
   */
  constexpr auto reset(value_type const initial) noexcept -> void {
    m_value = initial;
    m_primed = true;
  }

  /**
   * @brief Returns the current smoothing factor.
   *
   * @return The smoothing factor in use.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto alpha() const noexcept -> value_type {
    return m_alpha;
  }

  /**
   * @brief Replaces the smoothing factor for subsequent samples.
   *
   * @param a New smoothing factor in \c (0, 1].
   *
   * @pre \p a lies in \c (0, 1].
   * @post \c alpha() returns \p a; the stored output is unchanged.
   */
  constexpr auto alpha(value_type const a) noexcept -> void {
    m_alpha = a;
  }
};

}  // namespace nexenne::filter
