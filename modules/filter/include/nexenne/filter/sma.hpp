#pragma once

/**
 * @file
 * @brief Simple Moving Average (SMA) with a fixed-size window.
 */

#include <array>
#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief Simple Moving Average (SMA) with a fixed-size window.
 *
 * Maintains a ring buffer of the last \p N samples and outputs
 * their arithmetic mean. Useful for smoothing noisy sensor data
 * with a guaranteed-bounded delay of \c N/2 samples.
 *
 * The running sum is maintained incrementally, and re-derived from
 * the window once per wrap (every \c N pushes) so that the
 * incremental add and subtract cannot let floating-point rounding
 * drift the sum permanently after a large-magnitude sample. \c push
 * is therefore amortised \c O(1).
 *
 * Zero heap: the window is a \c std::array. For a runtime-sized
 * window, use \c ema (which approximates SMA with exponential
 * decay and needs no buffer).
 *
 * @tparam T Floating-point sample type. Default \c double.
 * @tparam N Window size (number of samples to average over).
 *
 * @note Reach for this when every sample in a fixed window should count
 * equally and you want a predictable \c N / 2 sample delay.
 */
template <std::floating_point T = double, std::size_t N = 8>
  requires(N > 0)
class sma {
public:
  using value_type = T;
  static constexpr std::size_t window_size{N};

private:
  using buffer_type = std::array<value_type, N>;

  buffer_type m_buf{};
  value_type m_sum{};
  std::size_t m_idx{0};
  std::size_t m_count{0};

public:
  /**
   * @brief Constructs an empty filter with a zeroed window.
   *
   * @pre None.
   * @post \c count() is zero, \c filled() is \c false, and
   * \c value() returns zero.
   */
  constexpr sma() noexcept = default;

  /**
   * @brief Feeds one sample and returns the average of the window.
   *
   * Maintains the running sum incrementally: when the window is full
   * the oldest sample is subtracted before the newest is added, so
   * the cost is constant on most pushes. Every \c N pushes, when the
   * write index wraps over a full window, the sum is re-derived from
   * the buffer: an incremental add and subtract lets floating-point
   * rounding accumulate permanently once the sum is large relative to
   * a sample, so the periodic resum bounds that drift.
   *
   * @param sample New input sample.
   *
   * @return The arithmetic mean of the samples currently in the
   * window, including \p sample.
   *
   * @pre None.
   * @post \c count() is \c min(previous_count + 1, N) and \c value()
   * returns the value returned here.
   *
   * @complexity Amortised \c O(1); \c O(N) on the one push in every
   * \c N that resums the full window.
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    // Drop the oldest sample from the running sum before overwriting it.
    if (m_count == N) {
      m_sum -= m_buf[m_idx];
    } else {
      ++m_count;
    }
    m_buf[m_idx] = sample;
    m_sum += sample;
    m_idx = (m_idx + 1) % N;
    if (m_idx == 0 && m_count == N) {
      value_type sum{};
      for (auto const stored : m_buf) {
        sum += stored;
      }
      m_sum = sum;
    }
    return m_sum / static_cast<value_type>(m_count);
  }

  /**
   * @brief Returns the current window average without advancing.
   *
   * @return The mean of the samples currently in the window, or zero
   * when the window is empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_count == 0 ? value_type{} : m_sum / static_cast<value_type>(m_count);
  }

  /**
   * @brief Clears the window and running sum.
   *
   * @pre None.
   * @post \c count() is zero, \c filled() is \c false, and
   * \c value() returns zero.
   */
  constexpr auto reset() noexcept -> void {
    m_buf = buffer_type{};
    m_sum = value_type{};
    m_idx = 0;
    m_count = 0;
  }

  /**
   * @brief Reports whether the window holds a full set of \c N samples.
   *
   * @return \c true once at least \c N samples have been pushed since
   * the last \c reset(), \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto filled() const noexcept -> bool {
    return m_count == N;
  }

  /**
   * @brief Returns the number of samples currently in the window.
   *
   * @return Sample count, between \c 0 and \c N inclusive.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto count() const noexcept -> std::size_t {
    return m_count;
  }
};

}  // namespace nexenne::filter
