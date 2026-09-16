#pragma once

/**
 * @file
 * @brief Fixed-window median filter.
 */

#include <algorithm>
#include <array>
#include <cmath>
#include <concepts>
#include <cstddef>
#include <type_traits>

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
 * Implementation: maintains a ring buffer of raw samples and selects the
 * median from a scratch copy with \c std::nth_element, so each sample costs
 * \c O(N) on average. For a floating-point \c T a NaN sample is ignored: the
 * median is taken over the samples that are numbers, so a NaN is removed like
 * any other spike instead of breaking the ordering the selection relies on.
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
  using value_type = T;                           ///< Filtered sample type.
  using buffer_type = std::array<value_type, N>;  ///< Ring of the last \c N samples.
  static constexpr std::size_t window_size{N};    ///< Number of samples the median is taken over.

private:
  /// @brief Whether copying a sample cannot throw; members that copy one are \c noexcept then.
  static constexpr bool nothrow_copy{
    std::is_nothrow_copy_constructible_v<T> && std::is_nothrow_copy_assignable_v<T>
  };

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
   * Stores \p sample in the ring buffer and selects the middle element of
   * a scratch copy of the valid portion. For an even number of samples
   * the lower of the two central values is returned. NaN samples are
   * left out; a window holding only NaN yields NaN.
   *
   * @param sample New input sample.
   *
   * @return The median of the samples currently in the window.
   *
   * @pre None.
   * @post \c value() returns the value returned here and the window
   * holds at most \c N samples.
   *
   * @complexity \c O(N) on average (\c std::nth_element).
   */
  [[nodiscard]] constexpr auto push(T const sample) noexcept(nothrow_copy) -> T {
    m_buf[m_idx] = sample;
    m_idx = (m_idx + 1) % N;
    if (m_count < N) {
      ++m_count;
    }

    auto work{buffer_type{}};
    for (std::size_t i{0}; i < m_count; ++i) {
      work[i] = m_buf[i];
    }
    auto const first{work.begin()};
    auto numbers_end{first + static_cast<std::ptrdiff_t>(m_count)};
    if constexpr (std::floating_point<T>) {
      // NaN breaks the strict weak order std::nth_element needs, so partition it out first.
      numbers_end = std::partition(first, numbers_end, [](T const v) { return !std::isnan(v); });
      if (numbers_end == first) {
        m_value = sample;
        return m_value;
      }
    }
    // (count - 1) / 2 is the middle index for an odd count and the lower middle for an even one.
    auto const middle{first + (numbers_end - first - 1) / 2};
    std::nth_element(first, middle, numbers_end);
    m_value = *middle;
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
  [[nodiscard]] constexpr auto value() const noexcept(nothrow_copy) -> T {
    return m_value;
  }

  /**
   * @brief Clears the window back to empty.
   *
   * @pre None.
   * @post \c filled() is \c false, the window is empty, and
   * \c value() returns a value-initialised \c T.
   */
  constexpr auto reset() noexcept(nothrow_copy) -> void {
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
