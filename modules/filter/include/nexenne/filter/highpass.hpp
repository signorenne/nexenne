#pragma once

/**
 * @file
 * @brief First-order IIR high-pass filter.
 */

#include <cmath>
#include <concepts>
#include <numbers>

namespace nexenne::filter {

/**
 * @brief First-order IIR high-pass filter.
 *
 * Passes rapid changes (high frequencies) and attenuates slow
 * drift (low frequencies). The complement of \c lowpass:
 *
 * alpha = RC / (RC + dt)
 * y[n] = alpha * (y[n-1] + x[n] - x[n-1])
 *
 * Common uses: removing DC offset from a sensor signal,
 * extracting the "AC component" of a measurement, edge
 * detection in sampled data.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Reach for this to remove a DC offset or slow drift and keep the
 * fast changes.
 */
template <std::floating_point T = double>
class highpass {
public:
  using value_type = T;

private:
  value_type m_alpha{};
  value_type m_prev_input{};
  value_type m_value{};
  bool m_primed{false};

  [[nodiscard]] static constexpr auto compute_alpha(
    value_type const cutoff_hz, value_type const sample_rate_hz
  ) noexcept -> value_type {
    auto const dt{value_type{1} / sample_rate_hz};
    auto const rc{value_type{1} / (value_type{2} * std::numbers::pi_v<value_type> * cutoff_hz)};
    return rc / (rc + dt);
  }

public:
  /**
   * @brief Constructs a high-pass filter from a cutoff and sample rate.
   *
   * Precomputes the smoothing coefficient from the cutoff and sample
   * rate so the per-sample cost is one multiply and a few adds.
   *
   * @param cutoff_hz Cutoff frequency in Hz.
   * @param sample_rate_hz Rate at which \c push is called, in Hz.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @post \c alpha() returns the derived coefficient and the filter is
   * unprimed.
   */
  constexpr highpass(value_type const cutoff_hz, value_type const sample_rate_hz) noexcept
      : m_alpha{compute_alpha(cutoff_hz, sample_rate_hz)} {}

  /**
   * @brief Feeds one sample and returns the high-pass output.
   *
   * The first sample after construction or \c reset() seeds the input
   * memory and emits zero; later samples apply the difference equation.
   *
   * @param sample New input sample.
   *
   * @return The filtered output for \p sample.
   *
   * @pre None.
   * @post \c value() returns the value returned here.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    if (!m_primed) {
      m_prev_input = sample;
      m_value = value_type{};
      m_primed = true;
    } else {
      m_value = m_alpha * (m_value + sample - m_prev_input);
      m_prev_input = sample;
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
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Clears the filter back to the unprimed condition.
   *
   * @pre None.
   * @post \c value() returns zero and the next \c push reseeds the
   * input memory; the cutoff coefficient is preserved.
   */
  constexpr auto reset() noexcept -> void {
    m_prev_input = value_type{};
    m_value = value_type{};
    m_primed = false;
  }

  /**
   * @brief Recomputes the coefficient for a new cutoff and sample rate.
   *
   * @param cutoff_hz New cutoff frequency in Hz.
   * @param sample_rate_hz New sample rate in Hz.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @post \c alpha() reflects the new cutoff; the filter state is
   * unchanged.
   */
  constexpr auto
  cutoff(value_type const cutoff_hz, value_type const sample_rate_hz) noexcept -> void {
    m_alpha = compute_alpha(cutoff_hz, sample_rate_hz);
  }

  /**
   * @brief Returns the current smoothing coefficient.
   *
   * @return The internal coefficient derived from the cutoff and
   * sample rate.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto alpha() const noexcept -> value_type {
    return m_alpha;
  }
};

}  // namespace nexenne::filter
