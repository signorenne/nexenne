#pragma once

/**
 * @file
 * @brief LMS (Least Mean Squares) adaptive FIR filter.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <span>

namespace nexenne::filter {

/**
 * @brief LMS (Least Mean Squares) adaptive FIR filter.
 *
 * Adjusts its own coefficients on every sample to minimise the
 * squared error between a desired signal and the filter output.
 * The canonical adaptive-filter algorithm, used for echo cancellation,
 * adaptive noise cancellation, system identification, channel
 * equalisation. O(N) per sample for both filtering AND coefficient
 * update.
 *
 * Update rule:
 * y[n] = sum(w[i] * x[n - i])
 * e[n] = d[n] - y[n]
 * w[i] += mu * e[n] * x[n - i]
 *
 * Step size \c mu controls the convergence/stability tradeoff:
 * - Too small: slow adaptation, never tracks fast changes
 * - Too large: unstable, coefficients explode
 * - Typical: \c 0.001 .. \c 0.1 (normalise by input power for safety)
 *
 * @tparam T Floating-point sample type
 * @tparam N Filter order (number of taps)
 *
 * @note Identifying or undoing an unknown system online (echo cancellation,
 * adaptive noise cancellation, channel equalisation, plant identification) when
 * you have a reference input and a desired target.
 */
template <std::floating_point T, std::size_t N>
  requires(N > 0)
class lms {
public:
  using value_type = T;
  static constexpr std::size_t taps{N};

private:
  std::array<value_type, N> m_coeffs{};
  std::array<value_type, N> m_history{};
  std::size_t m_idx{0};
  value_type m_step_size{value_type{0.01}};
  value_type m_last_output{};
  value_type m_last_error{};

public:
  /**
   * @brief Constructs an LMS filter with zero taps and the default
   * step size.
   *
   * @pre None.
   * @post All taps and history are zero and \c step_size() returns the
   * default \c 0.01.
   */
  constexpr lms() noexcept = default;

  /**
   * @brief Constructs an LMS filter with a chosen step size.
   *
   * @param step_size Adaptation step size mu trading convergence
   * speed against stability.
   *
   * @pre \p step_size is positive and small enough for the input
   * power to keep the update stable (commonly \c 0.001 to
   * \c 0.1).
   * @post \c step_size() returns \p step_size and all taps are zero.
   */
  constexpr explicit lms(T const step_size) noexcept : m_step_size{step_size} {}

  /**
   * @brief Replaces the adaptation step size.
   *
   * @param mu New step size.
   *
   * @pre \p mu is positive and within the stable range for the input
   * power.
   * @post \c step_size() returns \p mu; taps and history are unchanged.
   */
  constexpr auto step_size(value_type const mu) noexcept -> void {
    m_step_size = mu;
  }

  /**
   * @brief Returns the current adaptation step size.
   *
   * @return The step size mu in use.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto step_size() const noexcept -> T {
    return m_step_size;
  }

  /**
   * @brief Returns a view of the current filter coefficients.
   *
   * @return A span over the \c N adapted taps.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto coefficients() const noexcept -> std::span<T const, N> {
    return m_coeffs;
  }

  /**
   * @brief Runs one filter-then-adapt step.
   *
   * Pushes \p input into the history, computes the output from the
   * current taps, then updates each tap by the LMS rule using the
   * error between \p desired and the output.
   *
   * @param input Reference or observed input sample.
   * @param desired Desired output (training signal) to track.
   *
   * @return The filter output computed before the coefficient update.
   *
   * @pre \c step_size() is within the stable range for the current
   * input power.
   * @post The taps move one LMS step toward minimising the squared
   * error; \c value() and \c error() reflect this step.
   *
   * @complexity \c O(N).
   */
  [[nodiscard]] constexpr auto push(T const input, T const desired) noexcept -> T {
    m_history[m_idx] = input;
    m_idx = (m_idx + 1) % N;

    // Compute output y[n] = sum(w[i] * x[n - i])
    value_type output{};
    std::size_t pos{m_idx};
    for (std::size_t i{0}; i < N; ++i) {
      pos = (pos == 0) ? N - 1 : pos - 1;
      output += m_coeffs[i] * m_history[pos];
    }

    // Error and coefficient update
    value_type const error{desired - output};
    pos = m_idx;
    for (std::size_t i{0}; i < N; ++i) {
      pos = (pos == 0) ? N - 1 : pos - 1;
      m_coeffs[i] += m_step_size * error * m_history[pos];
    }

    m_last_output = output;
    m_last_error = error;
    return output;
  }

  /**
   * @brief Returns the most recent filter output without advancing.
   *
   * @return The last output produced by \c push, or a
   * value-initialised \c T before the first \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_last_output;
  }

  /**
   * @brief Returns the most recent adaptation error.
   *
   * @return The last \c desired minus output error, or a
   * value-initialised \c T before the first \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto error() const noexcept -> T {
    return m_last_error;
  }

  /**
   * @brief Clears the taps, history, and cached outputs.
   *
   * @pre None.
   * @post All taps and history are zero and \c value() and \c error()
   * return value-initialised values; \c step_size() is
   * preserved.
   */
  constexpr auto reset() noexcept -> void {
    m_coeffs = {};
    m_history = {};
    m_idx = 0;
    m_last_output = T{};
    m_last_error = T{};
  }
};

}  // namespace nexenne::filter
