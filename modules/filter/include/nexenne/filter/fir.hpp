#pragma once

/**
 * @file
 * @brief FIR (Finite Impulse Response) convolution filter.
 */

#include <array>
#include <cstddef>
#include <span>

namespace nexenne::filter {

/**
 * @brief FIR (Finite Impulse Response) convolution filter.
 *
 * Convolves the input with a fixed coefficient vector, the cleanest
 * possible linear filter, no feedback, always stable, linear phase if
 * coefficients are symmetric. Use when you need:
 *
 * - Exact specified frequency response (designed via Parks-McClellan
 * or windowed sinc in offline tooling)
 * - Guaranteed stability (no IIR poles to worry about)
 * - Linear phase (group delay = (N-1)/2 samples, exact)
 *
 * Drawback: O(N) per sample. For a 64-tap FIR at 48 kHz that's
 * 3 MFLOPs, fine on any modern MCU. For tighter budgets, biquad
 * IIRs do less work for a similar response (at the cost of nonlinear
 * phase).
 *
 * @tparam T Sample type (e.g. \c float, \c double, fixed-point).
 * @tparam N Tap count (filter length).
 *
 * @note Reach for this when waveform shape must be preserved (linear phase)
 * or you need an exactly specified magnitude response designed offline.
 */
template <typename T, std::size_t N>
  requires(N > 0)
class fir {
public:
  using value_type = T;
  using coefficient_array = std::array<value_type, N>;
  using coefficient_span = std::span<value_type const, N>;
  static constexpr std::size_t taps{N};

private:
  coefficient_array m_coeffs{};
  coefficient_array m_history{};
  std::size_t m_idx{0};
  value_type m_last{};

public:
  /**
   * @brief Constructs a FIR filter with all-zero coefficients.
   *
   * Until coefficients are supplied the filter outputs zero for every
   * input.
   *
   * @pre None.
   * @post All taps and the history buffer are zero; \c value()
   * returns a value-initialised \c T.
   */
  constexpr fir() noexcept = default;

  /**
   * @brief Constructs a FIR filter from a coefficient vector.
   *
   * @param coeffs The \c N filter taps, index \c 0 weighting the
   * newest sample.
   *
   * @pre None.
   * @post \c coefficients() returns a copy of \p coeffs and the
   * history buffer is zero.
   *
   * @complexity \c O(N).
   */
  constexpr explicit fir(coefficient_span coeffs) noexcept {
    for (std::size_t i{0}; i < N; ++i) {
      m_coeffs[i] = coeffs[i];
    }
  }

  /**
   * @brief Replaces the coefficient vector.
   *
   * @param coeffs The new \c N filter taps.
   *
   * @pre None.
   * @post \c coefficients() returns a copy of \p coeffs; the history
   * buffer is left unchanged.
   *
   * @complexity \c O(N).
   */
  constexpr auto coefficients(coefficient_span coeffs) noexcept -> void {
    for (std::size_t i{0}; i < N; ++i) {
      m_coeffs[i] = coeffs[i];
    }
  }

  /**
   * @brief Returns the current coefficient vector.
   *
   * @return A const reference to the \c N filter taps.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto coefficients() const noexcept -> coefficient_array const& {
    return m_coeffs;
  }

  /**
   * @brief Feeds one sample and returns the convolution output.
   *
   * Stores \p sample in the ring buffer and multiply-accumulates the
   * coefficients against the last \c N inputs, newest first.
   *
   * @param sample New input sample.
   *
   * @return The filtered output for \p sample.
   *
   * @pre None.
   * @post The history advances by one sample and \c value() returns
   * the value returned here.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    m_history[m_idx] = sample;
    m_idx = (m_idx + 1) % N;

    value_type acc{};
    std::size_t pos{m_idx};
    // Multiply-accumulate against coefficients, walking history
    // newest-to-oldest by stepping the ring index forward.
    for (std::size_t i{0}; i < N; ++i) {
      pos = (pos == 0) ? N - 1 : pos - 1;
      acc += m_coeffs[i] * m_history[pos];
    }
    m_last = acc;
    return acc;
  }

  /**
   * @brief Returns the most recent output without advancing.
   *
   * @return The last value produced by \c push, or a value-initialised
   * \c T before the first \c push or after \c reset().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_last;
  }

  /**
   * @brief Clears the history buffer back to silence.
   *
   * @pre None.
   * @post The history buffer is zero and \c value() returns a
   * value-initialised \c T; the coefficients are unchanged.
   */
  constexpr auto reset() noexcept -> void {
    m_history = {};
    m_idx = 0;
    m_last = T{};
  }
};

}  // namespace nexenne::filter
