#pragma once

/**
 * @file
 * @brief First-order IIR low-pass filter.
 */

#include <cmath>
#include <concepts>
#include <numbers>

namespace nexenne::filter {

/**
 * @brief First-order IIR low-pass filter.
 *
 * The digital equivalent of an analogue RC low-pass circuit.
 * Parameterised by cutoff frequency and sample rate so the user
 * thinks in Hz rather than raw coefficients:
 *
 * alpha = dt / (RC + dt)
 * y[n] = alpha * x[n] + (1 - alpha) * y[n-1]
 *
 * where \c dt = 1 / sample_rate and \c RC = 1 / (2pi * cutoff).
 *
 * For embedded use where \c \<cmath\> is available, the constructor
 * computes alpha once and the per-sample cost is one multiply + one
 * add. If you already know alpha, use \c ema directly.
 *
 * @tparam T Floating-point sample type. Default \c double.
 *
 * @note Reach for this when you think in a cutoff frequency in Hz and a
 * sample rate, to reject noise above the cutoff.
 */
template <std::floating_point T = double>
class lowpass {
public:
  using value_type = T;

private:
  value_type m_alpha{};
  value_type m_value{};
  bool m_primed{false};

  [[nodiscard]] static constexpr auto compute_alpha(
    value_type const cutoff_hz, value_type const sample_rate_hz
  ) noexcept -> value_type {
    auto const dt{value_type{1} / sample_rate_hz};
    auto const rc{value_type{1} / (value_type{2} * std::numbers::pi_v<value_type> * cutoff_hz)};
    return dt / (rc + dt);
  }

public:
  /**
   * @brief Constructs a low-pass filter from a cutoff and sample rate.
   *
   * Precomputes the smoothing coefficient from the cutoff and sample
   * rate so the per-sample cost is one multiply and one add.
   *
   * @param cutoff_hz Cutoff frequency in Hz.
   * @param sample_rate_hz Rate at which \c push is called, in Hz.
   *
   * @pre \p sample_rate_hz is positive and \p cutoff_hz lies in
   * \c (0, sample_rate_hz / 2) (below Nyquist).
   * @post \c alpha() returns the derived coefficient and the filter is
   * unprimed.
   */
  constexpr lowpass(value_type const cutoff_hz, value_type const sample_rate_hz) noexcept
      : m_alpha{compute_alpha(cutoff_hz, sample_rate_hz)} {}

  /**
   * @brief Feeds one sample and returns the low-pass output.
   *
   * The first sample after construction or \c reset() seeds the
   * running value directly; later samples are blended with the
   * previous output.
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
      m_value = sample;
      m_primed = true;
    } else {
      m_value = m_alpha * sample + (value_type{1} - m_alpha) * m_value;
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
   * running value; the cutoff coefficient is preserved.
   */
  constexpr auto reset() noexcept -> void {
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
