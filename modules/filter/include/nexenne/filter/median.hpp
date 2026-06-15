#pragma once

/**
 * @file
 * @brief Fixed-window median filter.
 */

#include <algorithm>
#include <array>
#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief Fixed-window median filter.
 *
 * A nonlinear filter that outputs the median of the last \c N
 * samples. Unlike linear filters (SMA, EMA, low-pass), the
 * median filter is immune to outlier spikes: a single extreme
 * value cannot affect the output if the window is large enough.
 *
 * Common uses: removing salt-and-pepper noise from images,
 * de-spiking sensor readings (e.g. ultrasonic distance sensors
 * that occasionally return nonsense), or any situation where
 * you want to reject isolated outliers rather than smooth them.
 *
 * Implementation: maintains a ring buffer of raw samples and
 * a separate sorted copy for the median query. Per-sample cost
 * is O(N log N) for the sort step; for typical window sizes
 * (3 to 11) this is a handful of comparisons.
 *
 * @tparam T Ordered sample type. Default \c double.
 * @tparam N Window size. Must be odd for a single-value median;
 * even windows return the lower of the two middle values.
 *
 * @note Reach for it for impulsive or outlier noise (salt-and-pepper, a
 * rangefinder returning occasional nonsense, a stray bit flip): it removes
 * spikes outright but does not smooth, so pair a light linear stage after it.
 * Prefer an odd window so the middle is unambiguous.
 */
template <std::totally_ordered T = double, std::size_t N = 3>
  requires(N > 0)
class median {
public:
  using value_type = T;
  using buffer_type = std::array<value_type, N>;
  static constexpr std::size_t window_size{N};

private:
  buffer_type m_buf{};
  std::size_t m_idx{0};
  std::size_t m_count{0};
  value_type m_value{};

public:
  /**
   * @brief Constructs an empty median filter.
   *
   * @pre None.
   * @post \c filled() is \c false and \c value() returns a
   * value-initialised \c T.
   */
  constexpr median() noexcept = default;

  /**
   * @brief Feeds one sample and returns the median of the window.
   *
   * Stores \p sample in the ring buffer, sorts a scratch copy of the
   * valid portion, and selects the middle element. For an even number
   * of valid samples the lower of the two central values is returned.
   *
   * @param sample New input sample.
   *
   * @return The median of the samples currently in the window.
   *
   * @pre None.
   * @post \c value() returns the value returned here and the window
   * holds at most \c N samples.
   *
   * @complexity \c O(N log N) from the sort step.
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept -> T {
    m_buf[m_idx] = sample;
    m_idx = (m_idx + 1) % N;
    if (m_count < N) {
      ++m_count;
    }

    // Copy the valid portion into local scratch and sort it.
    auto work{buffer_type{}};
    for (std::size_t i{0}; i < m_count; ++i) {
      work[i] = m_buf[i];
    }
    std::sort(work.begin(), work.begin() + static_cast<std::ptrdiff_t>(m_count));
    // Lower of the two central values for an even count; the middle
    // element for an odd count. (m_count - 1) / 2 yields the lower
    // middle index without favouring the upper element on even windows.
    m_value = work[(m_count - 1) / 2];
    return m_value;
  }

  /**
   * @brief Returns the most recent median without advancing.
   *
   * @return The last value produced by \c push, or a
   * value-initialised \c T before the first \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> T {
    return m_value;
  }

  /**
   * @brief Clears the window back to empty.
   *
   * @pre None.
   * @post \c filled() is \c false, the window is empty, and
   * \c value() returns a value-initialised \c T.
   */
  constexpr auto reset() noexcept -> void {
    m_buf = {};
    m_idx = 0;
    m_count = 0;
    m_value = value_type{};
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
};

}  // namespace nexenne::filter
