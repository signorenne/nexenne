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
 * The running sum is maintained incrementally with Neumaier
 * compensation, so a small sample added to a large sum is not lost
 * when the large sample later leaves the window, and it is re-derived
 * from the window once per wrap (every \c N pushes) to shed any
 * residual rounding. \c push is therefore amortised \c O(1).
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
  using value_type = T;                         ///< Sample type of the input and the mean.
  static constexpr std::size_t window_size{N};  ///< Number of samples averaged over.

private:
  using buffer_type = std::array<value_type, N>;

  buffer_type m_buf{};
  value_type m_sum{};
  value_type m_compensation{};  ///< Neumaier correction: low-order bits m_sum lost.
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
   * the cost is constant on most pushes. Each add and subtract is
   * compensated (Neumaier), so the low-order bits a large sum cannot
   * hold are kept aside and survive when the large sample leaves.
   * Every \c N pushes, when the write index wraps over a full window,
   * the sum is re-derived from the buffer to shed residual rounding.
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
      accumulate(-m_buf[m_idx]);
    } else {
      ++m_count;
    }
    m_buf[m_idx] = sample;
    accumulate(sample);
    m_idx = (m_idx + 1) % N;
    if (m_idx == 0 && m_count == N) {
      m_sum = value_type{};
      m_compensation = value_type{};
      for (auto const stored : m_buf) {
        accumulate(stored);
      }
    }
    return (m_sum + m_compensation) / static_cast<value_type>(m_count);
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
    return m_count == 0 ? value_type{}
                        : (m_sum + m_compensation) / static_cast<value_type>(m_count);
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
    m_compensation = value_type{};
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

private:
  /**
   * @brief Adds \p x to the running sum with Neumaier compensation.
   *
   * @param x Value to add (a negated sample to remove one).
   *
   * @pre None.
   * @post \c m_sum plus \c m_compensation holds the sum including \p x.
   */
  constexpr auto accumulate(value_type const x) noexcept -> void {
    auto const t{m_sum + x};
    if ((m_sum < value_type{0} ? -m_sum : m_sum) >= (x < value_type{0} ? -x : x)) {
      m_compensation += (m_sum - t) + x;
    } else {
      m_compensation += (x - t) + m_sum;
    }
    m_sum = t;
  }
};

}  // namespace nexenne::filter
