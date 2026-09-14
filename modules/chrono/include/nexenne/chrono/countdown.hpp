#pragma once

/**
 * @file
 * @brief Polling-driven countdown timer.
 *
 * Set a target duration via the constructor or \c set_target,
 * then \c start(). \c tick() is the polling hook: call it in your
 * main loop and it returns \c true exactly once, on the transition
 * from running to expired. After expiry elapsed time keeps accruing,
 * so \c overrun() continues to grow with real time.
 *
 * \c remaining() / \c overrun() / \c progress() let you visualise
 * the countdown in real time. Like \c stopwatch, you can \c pause()
 * and \c resume() without losing the elapsed time.
 *
 * Why not run a thread / callback? Because for most embedded and
 * game-loop contexts you already have a polling loop, and a
 * polling timer is one synchronisation primitive simpler than a
 * threaded one. (For a thread-driven variant, build it on top of
 * this type plus a \c jthread of your choice; not in scope here.)
 *
 * @tparam Clock Steady clock to measure against.
 */

#include <chrono>
#include <compare>
#include <optional>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/conversion.hpp>
#include <nexenne/chrono/timer_state.hpp>

namespace nexenne::chrono {

/**
 * @brief Polling-driven countdown timer over a steady clock.
 *
 * Holds a target duration and a state machine driven by \c start, \c pause,
 * \c resume, \c reset, and \c tick. \c tick() returns \c true exactly once on
 * the transition from running to expired; elapsed time keeps accruing
 * afterward so \c overrun() continues to grow. It keeps only a start point and
 * an accumulated duration, so it never allocates.
 *
 * @tparam Clock Steady clock to measure against.
 *
 * @pre None.
 * @post A default-constructed countdown is idle with a zero target.
 */
template <steady_clock_like Clock = std::chrono::steady_clock>
class countdown {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

  /**
   * @brief Lifecycle state of a \c countdown; see \c countdown_state.
   */
  using state = countdown_state;

private:
  time_point m_start{};
  duration m_accumulated{duration::zero()};
  duration m_target{duration::zero()};
  state m_state{state::idle};
  // Whether time is accruing. Not derivable from m_state: expired keeps the
  // clock running after a tick() expiry but stopped after a zero-target start,
  // and set_target can change the target afterwards.
  bool m_ticking{false};

  /**
   * @brief Elapsed time against a caller-supplied \c now() snapshot.
   *
   * @param now The time snapshot to measure against.
   *
   * @return The accumulated time, plus the running segment while ticking.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto elapsed_at(time_point const now) const noexcept -> duration {
    return m_ticking ? m_accumulated + (now - m_start) : m_accumulated;
  }

  /**
   * @brief Elapsed time as of \c Clock::now().
   *
   * @return The elapsed time in the clock's own unit.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto elapsed_now() const noexcept -> duration {
    return elapsed_at(Clock::now());
  }

  /**
   * @brief Remaining time against a caller-supplied \c now() snapshot.
   *
   * Clamps an already-expired countdown to zero so a comparison against a
   * single shared snapshot never yields a negative remaining time.
   *
   * @param now The time snapshot to measure against.
   *
   * @return The non-negative time left as of \p now.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto remaining_at(time_point const now) const noexcept -> duration {
    auto const e{elapsed_at(now)};
    return e >= m_target ? duration::zero() : (m_target - e);
  }

public:
  /**
   * @brief Construct an idle countdown with a zero target.
   *
   * @pre None.
   * @post \c is_idle() is \c true and \c target() is \c duration::zero().
   */
  constexpr countdown() noexcept = default;

  /**
   * @brief Construct an idle countdown with the target \p target.
   *
   * A negative target is clamped to zero.
   *
   * @tparam D Source duration type, deduced from \p target.
   * @param target Target duration to count down from.
   *
   * @pre None.
   * @post \c is_idle() is \c true and \c target() is the non-negative cast
   *       of \p target.
   */
  template <chrono_duration D>
  constexpr explicit countdown(D const target) noexcept
      : m_target{detail::saturating_cast<duration>(target)} {
    if (m_target < duration::zero()) {
      m_target = duration::zero();
    }
  }

  /**
   * @brief Configured target duration.
   *
   * @return The target the countdown runs against.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto target() const noexcept -> duration {
    return m_target;
  }

  /**
   * @brief Replace the target duration.
   *
   * A negative target is clamped to zero. Does not change the current state
   * or elapsed time.
   *
   * @tparam D Source duration type, deduced from \p target.
   * @param target New target duration.
   *
   * @pre None.
   * @post \c target() is the non-negative cast of \p target.
   */
  template <chrono_duration D>
  auto set_target(D const target) noexcept -> void {
    m_target = detail::saturating_cast<duration>(target);
    if (m_target < duration::zero()) {
      m_target = duration::zero();
    }
  }

  /**
   * @brief Lengthen the target by \p delta.
   *
   * The result is clamped to zero, so a sufficiently negative \p delta
   * yields a zero target.
   *
   * @tparam D Source duration type, deduced from \p delta.
   * @param delta Amount to add to the target.
   *
   * @pre None.
   * @post \c target() is the prior target plus \p delta, clamped at zero.
   */
  template <chrono_duration D>
  auto extend(D const delta) noexcept -> void {
    m_target = detail::saturating_add(m_target, detail::saturating_cast<duration>(delta));
    if (m_target < duration::zero()) {
      m_target = duration::zero();
    }
  }

  /**
   * @brief Shorten the target by \p delta.
   *
   * Equivalent to \c extend(-delta); the result is clamped at zero.
   *
   * @tparam D Source duration type, deduced from \p delta.
   * @param delta Amount to subtract from the target.
   *
   * @pre None.
   * @post \c target() is the prior target minus \p delta, clamped at zero.
   */
  template <chrono_duration D>
  auto shrink(D const delta) noexcept -> void {
    extend(detail::saturating_negate(detail::saturating_cast<duration>(delta)));
  }

  /**
   * @brief Current lifecycle state.
   *
   * @return The stored \c state.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto current_state() const noexcept -> state {
    return m_state;
  }

  /**
   * @brief Whether the countdown is idle.
   *
   * @return \c true if the state is \c state::idle.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_idle() const noexcept -> bool {
    return m_state == state::idle;
  }

  /**
   * @brief Whether the countdown is running.
   *
   * @return \c true if the state is \c state::running.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_running() const noexcept -> bool {
    return m_state == state::running;
  }

  /**
   * @brief Whether the countdown is paused.
   *
   * @return \c true if the state is \c state::paused.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_paused() const noexcept -> bool {
    return m_state == state::paused;
  }

  /**
   * @brief Whether the countdown has expired.
   *
   * Returns \c true once the recorded state is expired, or while running if
   * the elapsed time has reached the target. Always \c false while idle.
   *
   * @return \c true if the countdown has reached or passed its target.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_expired() const noexcept -> bool {
    if (m_state == state::expired) {
      return true;
    }
    if (m_state == state::idle) {
      return false;
    }
    return elapsed_now() >= m_target;
  }

  /**
   * @brief Start the countdown.
   *
   * Re-startable from \c idle or \c expired, clearing any prior elapsed
   * time. No-op from \c running or \c paused. A zero target transitions
   * straight to \c expired.
   *
   * @pre None.
   * @post If the prior state was \c idle or \c expired, the countdown is
   *       \c running, or \c expired when the target is zero; otherwise the
   *       state is unchanged.
   */
  auto start() noexcept -> void {
    if (m_state != state::idle && m_state != state::expired) {
      return;
    }
    m_accumulated = duration::zero();
    m_ticking = false;
    if (m_target == duration::zero()) {
      m_state = state::expired;
      return;
    }
    m_start = Clock::now();
    m_ticking = true;
    m_state = state::running;
  }

  /**
   * @brief Pause a running countdown, freezing elapsed time.
   *
   * No-op unless the countdown is running.
   *
   * @pre None.
   * @post If the prior state was \c running, the countdown is \c paused;
   *       otherwise the state is unchanged.
   */
  auto pause() noexcept -> void {
    if (m_state != state::running) {
      return;
    }
    m_accumulated += Clock::now() - m_start;
    m_ticking = false;
    m_state = state::paused;
  }

  /**
   * @brief Resume a paused countdown.
   *
   * No-op unless the countdown is paused.
   *
   * @pre None.
   * @post If the prior state was \c paused, the countdown is \c running;
   *       otherwise the state is unchanged.
   */
  auto resume() noexcept -> void {
    if (m_state != state::paused) {
      return;
    }
    m_start = Clock::now();
    m_ticking = true;
    m_state = state::running;
  }

  /**
   * @brief Clear elapsed time and return to idle.
   *
   * Keeps the configured target.
   *
   * @pre None.
   * @post \c is_idle() is \c true and elapsed time is zero.
   */
  auto reset() noexcept -> void {
    m_start = time_point{};
    m_accumulated = duration::zero();
    m_ticking = false;
    m_state = state::idle;
  }

  /**
   * @brief Reset, then start.
   *
   * @pre None.
   * @post The countdown is \c running, or \c expired when the target is
   *       zero, with elapsed time measured from this call.
   */
  auto restart() noexcept -> void {
    reset();
    start();
  }

  /**
   * @brief Polling tick that detects the expiry transition.
   *
   * Returns \c true exactly once, on the transition from running to
   * expired, and \c false on every other call. Elapsed time keeps accruing
   * after expiry so \c overrun() continues to grow.
   *
   * @return \c true on the running-to-expired transition, else \c false.
   *
   * @pre None.
   * @post If \c true was returned, the state is now \c state::expired.
   */
  auto tick() noexcept -> bool {
    if (m_state != state::running) {
      return false;
    }
    if (elapsed_now() >= m_target) {
      m_state = state::expired;
      return true;
    }
    return false;
  }

  /**
   * @brief Time elapsed since the countdown started.
   *
   * Zero while idle; frozen while paused.
   *
   * @tparam D Duration type the result is expressed in.
   *
   * @return The elapsed time, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D = std::chrono::milliseconds>
  [[nodiscard]] auto elapsed() const noexcept -> D {
    return std::chrono::duration_cast<D>(elapsed_now());
  }

  /**
   * @brief Time left before the target is reached, clamped at zero.
   *
   * @tparam D Duration type the result is expressed in.
   *
   * @return The non-negative remaining time, in units \p D, rounded up for an
   *         integral \p D so it reads zero only once the target is reached.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D = std::chrono::milliseconds>
  [[nodiscard]] auto remaining() const noexcept -> D {
    auto const e{elapsed_now()};
    auto const r{e >= m_target ? duration::zero() : (m_target - e)};
    if constexpr (std::chrono::treat_as_floating_point_v<typename D::rep>) {
      return std::chrono::duration_cast<D>(r);
    } else {
      return std::chrono::ceil<D>(r);
    }
  }

  /**
   * @brief Time elapsed beyond the target, clamped at zero.
   *
   * Grows with real time after a \c tick() expiry because elapsed time keeps
   * accruing.
   *
   * @tparam D Duration type the result is expressed in.
   *
   * @return The non-negative overrun past the target, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D = std::chrono::milliseconds>
  [[nodiscard]] auto overrun() const noexcept -> D {
    auto const e{elapsed_now()};
    auto const o{e > m_target ? (e - m_target) : duration::zero()};
    return std::chrono::duration_cast<D>(o);
  }

  /**
   * @brief Fraction of the target that has elapsed, in \c [0, 1].
   *
   * Returns \c 0 while idle and \c 1 once expired. A zero target reports
   * \c 1 when expired and \c 0 otherwise.
   *
   * @return The clamped progress fraction in the closed range \c [0, 1].
   *
   * @pre None.
   * @post The result lies in the closed range \c [0, 1].
   */
  [[nodiscard]] auto progress() const noexcept -> double {
    // Clock's own unit: a nanosecond cast overflows a saturated target on a coarse clock.
    auto const target_count{m_target.count()};
    if (target_count <= 0) {
      return m_state == state::expired ? 1.0 : 0.0;
    }
    auto const p{static_cast<double>(elapsed_now().count()) / static_cast<double>(target_count)};
    if (p <= 0.0) {
      return 0.0;
    }
    if (p >= 1.0) {
      return 1.0;
    }
    return p;
  }

  /**
   * @brief Absolute time the countdown will expire, while running.
   *
   * Computed from the current \c Clock::now() plus the remaining time.
   *
   * @return The projected expiry \c time_point while running, or
   *         \c std::nullopt in any other state.
   *
   * @pre None.
   * @post The result is engaged if and only if \c is_running().
   */
  [[nodiscard]] auto deadline() const noexcept -> std::optional<time_point> {
    if (m_state != state::running) {
      return std::nullopt;
    }
    auto const e{elapsed_now()};
    auto const r{e >= m_target ? duration::zero() : (m_target - e)};
    return Clock::now() + r;
  }

  /**
   * @brief Three-way comparison by remaining time.
   *
   * Reads \c Clock::now() once and compares both sides against that single
   * snapshot, so scheduling jitter between two \c now() reads cannot flip
   * the result. The countdown that fires soonest sorts first.
   *
   * @return The ordering of the two remaining times.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator<=>(countdown const& a, countdown const& b) noexcept {
    auto const now{Clock::now()};
    return a.remaining_at(now) <=> b.remaining_at(now);
  }

  /**
   * @brief Equality by remaining time against a single \c now() snapshot.
   *
   * @return \c true if both countdowns have equal remaining time.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend auto operator==(countdown const& a, countdown const& b) noexcept -> bool {
    auto const now{Clock::now()};
    return a.remaining_at(now) == b.remaining_at(now);
  }
};

}  // namespace nexenne::chrono
