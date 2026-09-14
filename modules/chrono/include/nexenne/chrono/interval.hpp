#pragma once

/**
 * @file
 * @brief Polling-driven periodic timer.
 *
 * Where \c countdown fires once, \c interval fires repeatedly -
 * one tick every \c period units. Call \c tick() in your loop;
 * each call returns \c true exactly once per period boundary
 * crossed and advances the internal anchor by one period.
 *
 * \code
 * auto i{interval<>{std::chrono::milliseconds{100}}};
 * i.start();
 * while (running) {
 *     while (i.tick()) {   // drain all missed ticks in one go
 *         every_100ms();
 *     }
 *     do_other_work();
 * }
 * \endcode
 *
 * Catch-up semantics: each \c tick() call advances by exactly one period. If
 * you're far behind, drain with a \c while loop to process every missed period
 * now; calling once per loop iteration spreads the backlog over later
 * iterations instead. Missed periods are never skipped on their own: call
 * \c start() again to re-anchor at the current time when they should be
 * dropped.
 *
 * @tparam Clock Steady clock to measure against.
 */

#include <chrono>
#include <compare>
#include <cstdint>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/conversion.hpp>

namespace nexenne::chrono {

/**
 * @brief Polling-driven periodic timer over a steady clock.
 *
 * Fires once per \c period boundary. Each \c tick() returns \c true at most
 * once per crossed boundary and advances the internal anchor by exactly one
 * period, so a caller that is far behind can drain missed ticks with a loop
 * or work through them one per iteration; none is skipped.
 *
 * @tparam Clock Steady clock to measure against.
 *
 * @pre None.
 * @post A default-constructed interval is stopped with a zero period.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class interval {
public:
  using clock_type = Clock;                       ///< Steady clock the interval reads.
  using duration = typename Clock::duration;      ///< Period and remaining span on \p Clock.
  using time_point = typename Clock::time_point;  ///< Tick boundary instant on \p Clock.

private:
  duration m_period{duration::zero()};  ///< Spacing between ticks, never negative.
  time_point m_anchor{};                ///< Last consumed boundary, or the start time.
  std::uint64_t m_count{0};             ///< Boundaries consumed since \c start().
  bool m_running{false};                ///< Whether the interval is armed.

public:
  /**
   * @brief Construct a stopped interval with a zero period.
   *
   * @pre None.
   * @post \c is_running() is \c false and \c period() is \c duration::zero().
   */
  constexpr interval() noexcept = default;

  /**
   * @brief Construct a stopped interval with the period \p period.
   *
   * A negative period is clamped to zero.
   *
   * @tparam D Source duration type, deduced from \p period.
   * @param period Spacing between ticks.
   *
   * @pre None.
   * @post \c is_running() is \c false and \c period() is the non-negative
   *       cast of \p period.
   */
  template <chrono_duration D>
  constexpr explicit interval(D const period) noexcept
      : m_period{detail::saturating_cast<duration>(period)} {
    if (m_period < duration::zero()) {
      m_period = duration::zero();
    }
  }

  /**
   * @brief Configured tick period.
   *
   * @return The spacing between ticks.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto period() const noexcept -> duration {
    return m_period;
  }

  /**
   * @brief Configured tick period, for modification.
   *
   * Changing it does not re-anchor a running interval.
   *
   * @return Mutable reference to the period.
   *
   * @pre A value written through the reference is non-negative.
   * @post None.
   */
  [[nodiscard]] constexpr auto period() noexcept -> duration& {
    return m_period;
  }

  /**
   * @brief Arm the timer, anchoring at the current time.
   *
   * The next \c tick() returns \c true once \c period has elapsed. Resets
   * the reported tick count.
   *
   * @pre None.
   * @post \c is_running() is \c true and \c tick_count() is zero.
   */
  auto start() noexcept -> void {
    m_anchor = Clock::now();
    m_count = 0;
    m_running = true;
  }

  /**
   * @brief Stop the timer, leaving the anchor and tick count intact.
   *
   * @pre None.
   * @post \c is_running() is \c false.
   */
  auto stop() noexcept -> void {
    m_running = false;
  }

  /**
   * @brief Stop the timer and clear the anchor and tick count.
   *
   * @pre None.
   * @post \c is_running() is \c false and \c tick_count() is zero.
   */
  auto reset() noexcept -> void {
    m_running = false;
    m_count = 0;
    m_anchor = time_point{};
  }

  /**
   * @brief Whether the interval is armed.
   *
   * @return \c true if the timer is running.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_running() const noexcept -> bool {
    return m_running;
  }

  /**
   * @brief Total period boundaries reported so far.
   *
   * @return The number of times \c tick() has returned \c true since the
   *         last \c start().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto tick_count() const noexcept -> std::uint64_t {
    return m_count;
  }

  /**
   * @brief Poll for a crossed period boundary.
   *
   * When running with a positive period and at least one period has elapsed
   * since the anchor, advances the anchor by one period and counts the tick.
   *
   * @return \c true if a boundary was consumed, else \c false.
   *
   * @pre None.
   * @post On \c true, the anchor has advanced by one period and
   *       \c tick_count() has grown by one.
   */
  auto tick() noexcept -> bool {
    if (!m_running || m_period <= duration::zero()) {
      return false;
    }
    // Elapsed form: anchor + period would overflow for a period near duration::max().
    if (Clock::now() - m_anchor < m_period) {
      return false;
    }
    m_anchor += m_period;
    ++m_count;
    return true;
  }

  /**
   * @brief Time until the next tick boundary.
   *
   * @return The remaining time, or zero when stopped, when the period is
   *         not positive, or when a boundary has already been crossed but
   *         not yet consumed via \c tick().
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto remaining() const noexcept -> duration {
    if (!m_running || m_period <= duration::zero()) {
      return duration::zero();
    }
    auto const since{Clock::now() - m_anchor};
    if (since >= m_period) {
      return duration::zero();
    }
    return m_period - since;
  }

  /**
   * @brief Time until the next tick boundary in the units \p D.
   *
   * @tparam D Duration type the result is cast to.
   *
   * @return The remaining time, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D>
  [[nodiscard]] auto remaining() const noexcept -> D {
    return std::chrono::duration_cast<D>(remaining());
  }

  /**
   * @brief Absolute time of the next tick boundary.
   *
   * @return The next boundary time, or \c time_point::max() when stopped (so
   *         that stopped intervals sort to the end of a priority queue) or when
   *         the boundary lies beyond the clock's range.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto next_tick_at() const noexcept -> time_point {
    if (!m_running) {
      return time_point::max();
    }
    if (m_anchor.time_since_epoch() > duration::max() - m_period) {
      return time_point::max();
    }
    return m_anchor + m_period;
  }

  /**
   * @brief Three-way comparison by next-tick time.
   *
   * Running intervals sort by when they will next fire, earliest first;
   * stopped intervals collect at the end.
   *
   * @return The ordering of the two next-tick times.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator<=>(interval const& a, interval const& b) noexcept {
    return a.next_tick_at() <=> b.next_tick_at();
  }

  /**
   * @brief Equality by next-tick time.
   *
   * @return \c true if both intervals share the same next-tick time.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator==(interval const& a, interval const& b) noexcept -> bool {
    return a.next_tick_at() == b.next_tick_at();
  }
};

}  // namespace nexenne::chrono
