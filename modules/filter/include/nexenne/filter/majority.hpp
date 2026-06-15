#pragma once

/**
 * @file
 * @brief N-sample majority-vote (mode) filter for masking transient errors.
 */

#include <array>
#include <concepts>
#include <cstddef>

namespace nexenne::filter {

/**
 * @brief N-sample majority-vote filter.
 *
 * Collects \p N consecutive samples, then outputs the value that
 * appears most often (the mode). If no clear majority exists, the
 * last value in the batch wins (tie-break by recency).
 *
 * This is the software equivalent of Triple Modular Redundancy
 * (TMR), the standard technique in safety-critical systems for
 * masking single-event upsets (SEUs) and transient bus errors.
 * With \c N = 3, one corrupted read out of three is silently
 * corrected.
 *
 * Usage pattern for I2C / SPI:
 *
 * \code
 * auto vote{nexenne::filter::majority<std::uint16_t, 3>{}};
 *
 * // Read the register three times and vote:
 * vote.push(read_reg(0x42));
 * vote.push(read_reg(0x42));
 * auto const clean{vote.push(read_reg(0x42))};
 * // clean is the majority of the three readings
 * \endcode
 *
 * @tparam T Value type. Any \c std::equality_comparable type.
 * @tparam N Batch size (number of samples to vote over).
 * Odd values (3, 5, 7) guarantee a single-value majority.
 *
 * @note Reach for this when you can read a noisy or corruptible
 * source several times and want N-modular-redundancy voting; use an
 * odd window to avoid ties.
 */
template <std::equality_comparable T = bool, std::size_t N = 3>
  requires(N > 0)
class majority {
public:
  using value_type = T;
  using buffer_type = std::array<value_type, N>;
  static constexpr std::size_t batch_size{N};

private:
  buffer_type m_buf{};
  std::size_t m_idx{0};
  std::size_t m_count{0};
  value_type m_value{};

  /**
   * @brief Maps a chronological position to its ring-buffer index.
   *
   * Position 0 is the oldest buffered sample and position \c n-1 the
   * newest, where \c n is the number of valid samples.
   *
   * @param pos Chronological position, less than \p n.
   * @param n Number of valid samples currently buffered.
   *
   * @return The ring-buffer index holding that chronological sample.
   *
   * @pre \p pos is less than \p n.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto
  chrono_index(std::size_t const pos, std::size_t const n) const noexcept -> std::size_t {
    return (m_idx + N - n + pos) % N;
  }

  /**
   * @brief Finds the mode of the buffered samples, newest wins ties.
   *
   * Scans candidates in chronological order so that when several values
   * share the highest count the most recently stored one is chosen.
   *
   * @return The most frequent buffered value, breaking ties by recency.
   *
   * @pre At least one sample has been buffered.
   * @post None.
   *
   * @complexity \c O(N^2) from the pairwise scan.
   */
  [[nodiscard]] constexpr auto compute_mode() const noexcept -> value_type {
    auto const n{m_count < N ? m_count : N};
    value_type best{m_buf[chrono_index(0, n)]};
    auto best_cnt{std::size_t{0}};

    for (std::size_t i{0}; i < n; ++i) {
      auto const candidate{m_buf[chrono_index(i, n)]};
      auto cnt{std::size_t{0}};
      for (std::size_t j{0}; j < n; ++j) {
        if (m_buf[chrono_index(j, n)] == candidate) {
          ++cnt;
        }
      }
      // Greater-or-equal so ties resolve toward the more recent candidate.
      if (cnt >= best_cnt) {
        best = candidate;
        best_cnt = cnt;
      }
    }
    return best;
  }

public:
  /**
   * @brief Constructs an empty majority filter.
   *
   * @pre None.
   * @post \c filled() is \c false and \c value() returns a
   * value-initialised \c T.
   */
  constexpr majority() noexcept = default;

  /**
   * @brief Feeds one sample and returns the current batch majority.
   *
   * Stores \p sample in the ring buffer and recomputes the mode of the
   * samples seen so far. Ties are broken toward the most recently
   * stored value.
   *
   * @param sample New input sample.
   *
   * @return The most frequent value among the buffered samples.
   *
   * @pre None.
   * @post \c value() returns the value returned here and the buffer
   * holds at most \c N samples.
   *
   * @complexity \c O(N^2) from the pairwise mode scan.
   */
  [[nodiscard]] constexpr auto push(value_type const sample) noexcept -> value_type {
    m_buf[m_idx] = sample;
    m_idx = (m_idx + 1) % N;
    if (m_count < N) {
      ++m_count;
    }
    m_value = compute_mode();
    return m_value;
  }

  /**
   * @brief Returns the most recent majority without advancing.
   *
   * @return The last value produced by \c push, or a value-initialised
   * \c T before the first \c push.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto value() const noexcept -> value_type {
    return m_value;
  }

  /**
   * @brief Clears the batch buffer back to empty.
   *
   * @pre None.
   * @post \c filled() is \c false, the buffer is empty, and
   * \c value() returns a value-initialised \c T.
   */
  constexpr auto reset() noexcept -> void {
    m_buf = buffer_type{};
    m_idx = 0;
    m_count = 0;
    m_value = value_type{};
  }

  /**
   * @brief Reports whether the batch buffer has filled at least once.
   *
   * @return \c true once at least \c N samples have been pushed since
   * the last \c reset(), \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto filled() const noexcept -> bool {
    return m_count >= N;
  }
};

}  // namespace nexenne::filter
