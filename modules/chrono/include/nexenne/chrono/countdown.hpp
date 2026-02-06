#pragma once

/**
 * @file
 * @brief Polling-driven countdown timer.
 *
 * Set a target duration via the constructor or \c set_target,
 * then \c start(). \c tick() is the polling hook: call it in your
 * main loop and it returns \c true exactly once, on the transition
 * from running to expired. After expiry the underlying stopwatch
 * keeps running so \c overrun() continues to grow with real time.
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

#include <algorithm>
#include <chrono>
#include <compare>
#include <cstdint>
#include <format>
#include <optional>
#include <ranges>
#include <string>

#include <nexenne/chrono/concepts.hpp>
#include <nexenne/chrono/duration_parts.hpp>
#include <nexenne/chrono/stopwatch.hpp>

namespace nexenne::chrono {

/**
 * @brief Polling-driven countdown timer over a steady clock.
 *
 * Holds a target duration and a state machine driven by \c start, \c pause,
 * \c resume, \c reset, and \c tick. \c tick() returns \c true exactly once on
 * the transition from running to expired; the underlying stopwatch keeps
 * running afterward so \c overrun() continues to grow.
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
   * @brief Lifecycle state of a \c countdown.
   *
   * @pre None.
   * @post None.
   */
  enum class state : std::uint8_t {
    idle,
    running,
    paused,
    expired,
  };

private:
  stopwatch<Clock> m_sw{};
  duration m_target{duration::zero()};
  state m_state{state::idle};

  [[nodiscard]] auto remaining_at(time_point const now) const noexcept -> duration {
    auto const e{m_sw.elapsed_at(now)};
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
      : m_target{std::chrono::duration_cast<duration>(target)} {
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
    m_target = std::chrono::duration_cast<duration>(target);
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
    m_target += std::chrono::duration_cast<duration>(delta);
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
    extend(-std::chrono::duration_cast<duration>(delta));
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
    return m_sw.elapsed() >= m_target;
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
    m_sw.reset();
    if (m_target == duration::zero()) {
      m_state = state::expired;
      return;
    }
    m_sw.start();
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
    m_sw.pause();
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
    m_sw.resume();
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
    m_sw.reset();
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
   * expired, and \c false on every other call. The underlying stopwatch
   * keeps running after expiry so \c overrun() continues to grow.
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
    if (m_sw.elapsed() >= m_target) {
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
    return m_sw.template elapsed<D>();
  }

  /**
   * @brief Time left before the target is reached, clamped at zero.
   *
   * @tparam D Duration type the result is expressed in.
   *
   * @return The non-negative remaining time, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D = std::chrono::milliseconds>
  [[nodiscard]] auto remaining() const noexcept -> D {
    auto const e{m_sw.elapsed()};
    auto const r{e >= m_target ? duration::zero() : (m_target - e)};
    return std::chrono::duration_cast<D>(r);
  }

  /**
   * @brief Time elapsed beyond the target, clamped at zero.
   *
   * Grows with real time after expiry because the underlying stopwatch
   * keeps running.
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
    auto const e{m_sw.elapsed()};
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
    auto const target_ns{std::chrono::duration_cast<std::chrono::nanoseconds>(m_target).count()};
    if (target_ns <= 0) {
      return m_state == state::expired ? 1.0 : 0.0;
    }
    auto const e_ns{std::chrono::duration_cast<std::chrono::nanoseconds>(m_sw.elapsed()).count()};
    auto const p{static_cast<double>(e_ns) / static_cast<double>(target_ns)};
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
    auto const e{m_sw.elapsed()};
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

/**
 * @brief \c std::format support for \c countdown.
 *
 * Formats the remaining time by default, or the elapsed time with the \c 'e'
 * flag. A leading \c '!' disables the suppress-zero behaviour, showing every
 * component including zeros.
 *
 * @tparam Clock Steady clock of the formatted countdown.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::chrono::steady_clock_like Clock>
struct std::formatter<nexenne::chrono::countdown<Clock>, char> {
private:
  bool suppress_zero{true};
  bool show_elapsed{false};

public:
  /**
   * @brief Parse the format spec flags.
   *
   * @param ctx The format parse context.
   *
   * @return Iterator past the consumed spec.
   *
   * @pre None.
   * @post The \c '!' and \c 'e' flags, if present, have been consumed.
   */
  constexpr auto parse(std::format_parse_context& ctx) {
    auto it{ctx.begin()};
    auto const end{ctx.end()};
    if (it != end && *it == '!') {
      suppress_zero = false;
      ++it;
    }
    if (it != end && *it == 'e') {
      show_elapsed = true;
      ++it;
    }
    return it;
  }

  /**
   * @brief Write the formatted countdown to the output.
   *
   * @tparam Out Output iterator type of the format context.
   * @param c The countdown to format.
   * @param ctx The format context to write into.
   *
   * @return Iterator past the written output.
   *
   * @pre None.
   * @post None.
   */
  template <class Out>
  auto format(nexenne::chrono::countdown<Clock> const& c, std::basic_format_context<Out, char>& ctx)
    const {
    auto const ms{
      show_elapsed ? c.template elapsed<std::chrono::milliseconds>()
                   : c.template remaining<std::chrono::milliseconds>()
    };
    auto const s{nexenne::chrono::format(ms, "{s-}{d}d:{h}h:{m}m:{s}s.{ms}", suppress_zero)};
    return std::ranges::copy(s, ctx.out()).out;
  }
};
