#pragma once

/**
 * @file
 * @brief Per-frame delta and FPS tracker for game / simulation loops.
 *
 * Call \c tick() once per frame; it returns the time elapsed since
 * the previous call (or zero on the first call). Internally tracks
 * a moving window of recent frame durations so \c fps() reflects
 * the recent average rather than a single jittery sample.
 *
 * Zero allocation: the moving window is a fixed-size array. The
 * sum is maintained incrementally so \c fps() is O(1).
 *
 * \code
 * auto ft{frame_timer<>{}};
 * while (running) {
 *     auto const dt{ft.tick()};
 *     update(dt);
 *     render();
 *     if (ft.frame_count() % 60 == 0) {
 *         log("fps = {}", ft.fps());
 *     }
 * }
 * \endcode
 *
 * @tparam Clock Steady clock to time with.
 * @tparam WindowSize Number of recent frames to average over.
 */

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief Per-frame delta and moving-average FPS tracker.
 *
 * Call \c tick() once per frame to get the delta since the previous call. A
 * fixed-size ring buffer of recent frame durations backs \c fps(), so the
 * reported rate reflects a recent average rather than one jittery sample. The
 * running sum is maintained incrementally, keeping \c fps() constant time, and
 * the type never allocates.
 *
 * @tparam Clock Steady clock to time with.
 * @tparam WindowSize Number of recent frames to average over; must be positive.
 *
 * @pre \p WindowSize is greater than zero.
 * @post A default-constructed frame_timer has not yet started.
 */
template <steady_clock_like Clock = std::chrono::steady_clock, std::size_t WindowSize = 60>
  requires(WindowSize > 0)
class frame_timer {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

private:
  std::array<duration, WindowSize> m_window{};
  duration m_sum{duration::zero()};
  std::size_t m_idx{0};
  std::size_t m_filled{0};
  std::uint64_t m_frames{0};
  time_point m_last{};
  bool m_started{false};

public:
  /**
   * @brief Construct a frame timer that has not yet started.
   *
   * @pre None.
   * @post \c started() is \c false, \c frame_count() is zero, and \c fps()
   *       is zero.
   */
  constexpr frame_timer() noexcept = default;

  /**
   * @brief Record a frame boundary and return the frame delta.
   *
   * Reads \c Clock::now(), folds the delta into the moving window, and
   * advances the frame counter.
   *
   * @return The duration since the previous \c tick(), or zero on the first
   *         call.
   *
   * @pre None.
   * @post \c frame_count() has grown by one and \c started() is \c true.
   */
  auto tick() noexcept -> duration {
    auto const now{Clock::now()};
    auto dt{duration::zero()};
    if (m_started) {
      dt = now - m_last;
      // Keep a running sum so \c fps() is O(1). When the
      // window is full, subtract the slot we're about to
      // overwrite; otherwise just add the new value.
      if (m_filled == WindowSize) {
        m_sum -= m_window[m_idx];
      } else {
        ++m_filled;
      }
      m_window[m_idx] = dt;
      m_sum += dt;
      m_idx = (m_idx + 1) % WindowSize;
    }
    m_last = now;
    m_started = true;
    ++m_frames;
    return dt;
  }

  /**
   * @brief Frames per second averaged over the recent window.
   *
   * Derived from the mean frame duration across the filled window.
   *
   * @return The recent average FPS, or zero before the second \c tick()
   *         when the window is still empty.
   *
   * @pre None.
   * @post The result is greater than or equal to zero.
   */
  [[nodiscard]] auto fps() const noexcept -> double {
    if (m_filled == 0) {
      return 0.0;
    }
    auto const sum_ns{std::chrono::duration_cast<std::chrono::nanoseconds>(m_sum).count()};
    if (sum_ns <= 0) {
      return 0.0;
    }
    auto const avg_sec{static_cast<double>(sum_ns) / static_cast<double>(m_filled) * 1e-9};
    return 1.0 / avg_sec;
  }

  /**
   * @brief Cumulative number of frames since construction or reset.
   *
   * @return The total \c tick() count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto frame_count() const noexcept -> std::uint64_t {
    return m_frames;
  }

  /**
   * @brief Time of the most recent \c tick().
   *
   * @return The last recorded frame time; meaningful only once
   *         \c started() is \c true.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto last_tick() const noexcept -> time_point {
    return m_last;
  }

  /**
   * @brief Whether at least one \c tick() has been recorded.
   *
   * @return \c true once \c tick() has been called.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto started() const noexcept -> bool {
    return m_started;
  }

  /**
   * @brief Clear all counters and the moving window.
   *
   * @pre None.
   * @post \c started() is \c false, \c frame_count() is zero, and \c fps()
   *       is zero.
   */
  auto reset() noexcept -> void {
    m_window = {};
    m_sum = duration::zero();
    m_idx = 0;
    m_filled = 0;
    m_frames = 0;
    m_started = false;
  }
};

}  // namespace nexenne::chrono
