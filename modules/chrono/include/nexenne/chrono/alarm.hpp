#pragma once

/**
 * @file
 * @brief Polling-driven, callback-on-expiry timer.
 *
 * Companion to \c countdown and \c deadline: an alarm holds a fire
 * time and a callback. Each call to \c poll(now) checks whether the
 * fire time has been reached and, if so, invokes the callback exactly
 * once (or repeatedly, depending on the configured mode).
 *
 * Modes:
 *   - \c one_shot   : fire once at the deadline, then stay disarmed
 *   - \c periodic   : fire at fixed-period intervals; auto-rearms
 *                     to the next boundary (catches up if poll() is
 *                     called late, like \c blinking_led).
 *
 * Callbacks are stored in \c in_place_function (no heap), with the
 * inline capacity configurable per instantiation (default 64 bytes).
 *
 * Use \c countdown / \c deadline if you want the polling caller to
 * decide what to do on expiry; use \c alarm when you want to bake the
 * "what to do" into the timer itself.
 *
 * @tparam Clock Any \c chrono::clock_like
 * @tparam CallbackBytes Inline storage for the callback (default 64)
 */

#include <utility>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/utility/in_place_function.hpp>

namespace nexenne::chrono {

/**
 * @brief Firing policy for an \c alarm.
 *
 * Selects whether the alarm fires a single time at its deadline and then
 * disarms, or rearms itself to the next period boundary after every fire.
 *
 * @pre None.
 * @post None.
 */
enum class alarm_mode {
  one_shot,
  periodic
};

/**
 * @brief Polling-driven timer that invokes a stored callback on expiry.
 *
 * Holds a fire time, a firing \c alarm_mode, and an inline callback. Each
 * \c poll(now) checks the fire time against \p now and invokes the callback
 * when it has been reached, once for a one-shot alarm and for every elapsed
 * cycle for a periodic alarm. The callback lives in an \c in_place_function
 * with \p CallbackBytes of inline storage, so the type never allocates.
 *
 * @tparam Clock Any \c chrono::clock_like type.
 * @tparam CallbackBytes Inline storage in bytes for the callback.
 *
 * @pre None.
 * @post A default-constructed alarm is disarmed with an empty callback.
 */
template <clock_like Clock, std::size_t CallbackBytes = 64>
class alarm {
public:
  using clock_type = Clock;
  using time_point = typename Clock::time_point;
  using duration = typename Clock::duration;
  using callback_type = ::nexenne::utility::in_place_function<void(), CallbackBytes>;

private:
  callback_type m_cb{};
  time_point m_next{};
  duration m_period{duration::zero()};
  alarm_mode m_mode{alarm_mode::one_shot};
  bool m_armed{false};

public:
  /**
   * @brief Construct a disarmed alarm with no callback.
   *
   * @pre None.
   * @post \c is_armed() is \c false and the stored callback is empty.
   */
  constexpr alarm() noexcept = default;

  /**
   * @brief Set the callback fired on expiry.
   *
   * Replaces the stored callback. Replacing while armed is supported: the
   * new callback fires on the next boundary in place of the old one.
   *
   * @param cb Callback to store, taking no arguments and returning void.
   *
   * @pre None.
   * @post Subsequent fires invoke \p cb instead of any prior callback.
   */
  auto set_callback(callback_type cb) noexcept -> void {
    m_cb = std::move(cb);
  }

  /**
   * @brief Arm as a one-shot firing at the absolute time \p when.
   *
   * @param when Absolute fire time on \p Clock.
   *
   * @pre None.
   * @post \c is_armed() is \c true, \c mode() is \c alarm_mode::one_shot,
   *       and \c next_fire_time() equals \p when.
   */
  auto arm_at(time_point const when) noexcept -> void {
    m_next = when;
    m_period = duration::zero();
    m_mode = alarm_mode::one_shot;
    m_armed = true;
  }

  /**
   * @brief Arm as a one-shot firing \p delay after \p now.
   *
   * @param now Reference time the delay is measured from.
   * @param delay Time after \p now at which the alarm fires.
   *
   * @pre None.
   * @post \c is_armed() is \c true, \c mode() is \c alarm_mode::one_shot,
   *       and \c next_fire_time() equals \p now plus \p delay.
   */
  auto arm_after(time_point const now, duration const delay) noexcept -> void {
    arm_at(now + delay);
  }

  /**
   * @brief Arm as a periodic timer firing every \p period.
   *
   * The first fire is scheduled \p period after \p now; each subsequent
   * \c poll rearms to the next boundary.
   *
   * @param now Reference time the first period is measured from.
   * @param period Spacing between successive fires.
   *
   * @pre None.
   * @post \c is_armed() is \c true, \c mode() is \c alarm_mode::periodic,
   *       and \c next_fire_time() equals \p now plus \p period.
   */
  auto arm_periodic(time_point const now, duration const period) noexcept -> void {
    m_next = now + period;
    m_period = period;
    m_mode = alarm_mode::periodic;
    m_armed = true;
  }

  /**
   * @brief Disarm the alarm so further polling does nothing.
   *
   * @pre None.
   * @post \c is_armed() is \c false; the stored callback is retained.
   */
  auto disarm() noexcept -> void {
    m_armed = false;
    m_period = duration::zero();
  }

  /**
   * @brief Whether the alarm is currently armed.
   *
   * @return \c true if a fire is pending, \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_armed() const noexcept -> bool {
    return m_armed;
  }

  /**
   * @brief Absolute time of the next scheduled fire.
   *
   * @return The next fire time. Meaningful only while \c is_armed().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto next_fire_time() const noexcept -> time_point {
    return m_next;
  }

  /**
   * @brief Current firing mode.
   *
   * @return \c alarm_mode::one_shot or \c alarm_mode::periodic.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mode() const noexcept -> alarm_mode {
    return m_mode;
  }

  /**
   * @brief Drive the alarm against the current time \p now.
   *
   * Fires the callback when the scheduled time has been reached. A one-shot
   * alarm fires at most once and then disarms; a periodic alarm catches up
   * to \p now by firing every elapsed cycle and rearming to the next
   * boundary. A periodic alarm with a non-positive period disarms itself
   * rather than looping forever.
   *
   * @param now Current time on \p Clock.
   *
   * @pre None.
   * @post A one-shot alarm that fired is disarmed; a periodic alarm has
   *       \c next_fire_time() strictly after \p now unless it disarmed on a
   *       non-positive period.
   * @throws Whatever the stored callback throws; it is invoked unguarded.
   */
  auto poll(time_point const now) -> void {
    if (!m_armed) {
      return;
    }
    while (now >= m_next) {
      if (m_cb) {
        m_cb();
      }
      if (m_mode == alarm_mode::one_shot) {
        m_armed = false;
        return;
      }
      m_next += m_period;
      // A zero or negative period would never advance past now: disarm rather
      // than spin forever (and overflow m_next on a negative period).
      if (m_period <= duration::zero()) {
        m_armed = false;
        return;
      }
    }
  }
};

}  // namespace nexenne::chrono
