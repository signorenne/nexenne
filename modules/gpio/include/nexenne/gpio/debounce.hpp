#pragma once

/**
 * @file
 * @brief Userspace time-based debounce for one line's edge event stream.
 *
 * A bouncing contact produces a burst of raw edges; \c event_debounce sits
 * in the drain path and forwards only settled transitions, reusing the
 * tested, allocation-free \c nexenne::filter::timed_debounce engine rather
 * than re-implementing the settle logic. It operates in the PHYSICAL domain
 * (on \c line_event::physical) and leaves polarity to \c decode.hpp, so it
 * composes identically over every backend.
 *
 * This is the debounce that works everywhere. Its backend-side sibling is
 * \c line_config::debounce_period, which on Linux filters in the kernel
 * before events ever reach userspace: prefer the kernel knob when the
 * driver supports it (it costs no wakeups), and this type when it does not,
 * on other backends, or when the period must change at runtime without
 * reopening the request.
 *
 * An edge stream never repeats a level: once a contact holds, the next event
 * is the opposite edge, which may be seconds away. So the settle is driven by
 * time as well as by events: \c deadline says when the pending level will have
 * held for the period, and \c expire settles it once the caller's clock has
 * passed that point.
 */

#include <chrono>
#include <optional>

#include <nexenne/filter/timed_debounce.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/utility/ignore.hpp>

namespace nexenne::gpio {

/**
 * @brief Forwards only the edge events that stay settled for a period.
 *
 * Feed raw physical events in timestamp order. Once the line has held a new
 * level for at least the period, the debouncer returns the raw event that
 * began that level, with the matching edge; it returns \c std::nullopt while
 * the line is still bouncing or unchanged. The level settles on the first of
 * two triggers: \c expire called at or after \c deadline, or the next
 * \c feed of an event stamped at or after it. Drive \c expire from the wait
 * timeout, or a press is reported only when the release arrives. A zero
 * period passes everything through.
 *
 * @note Subscribe both edges: the settle test follows the level, and a
 *       rising-only or falling-only stream never changes it.
 */
class event_debounce {
public:
  using value_type = bool;

private:
  // The engine settles in the timestamp's native nanoseconds, so the
  // comparison never truncates whatever unit the caller thinks in.
  filter::timed_debounce<std::chrono::nanoseconds> m_filter{};
  // The raw event that began the level still waiting to settle, if any.
  std::optional<line_event> m_pending{};

  /**
   * @brief Runs one event through the engine, tracking the pending level.
   *
   * Keeps \c m_pending equal to the engine's candidate, which the engine
   * does not expose.
   *
   * @param event Raw physical event.
   *
   * @return The settled event as \c feed describes it, or \c std::nullopt.
   *
   * @pre Event timestamps are non-decreasing across calls.
   * @post \c m_pending holds the event that began the pending level, if any.
   */
  [[nodiscard]] constexpr auto feed_raw(line_event const& event) noexcept
    -> std::optional<line_event> {
    bool const had_stable{m_filter.has_stable()};
    auto const settled{m_filter.update(event.timestamp.time_since_epoch(), event.physical)};
    if (!settled.has_value()) {
      if (event.physical == m_filter.stable_value()) {
        m_pending.reset();
      } else if (!m_pending.has_value()) {
        m_pending = event;
      }
      return std::nullopt;
    }
    auto out{m_pending.value_or(event)};
    m_pending.reset();
    out.physical = *settled;
    out.edge = had_stable ? (*settled ? edge_kind::rising : edge_kind::falling) : edge_kind::none;
    return out;
  }

public:
  /**
   * @brief Constructs a pass-through debouncer with a zero period.
   *
   * @pre None.
   * @post \c period() is zero and the next \c feed forwards its event.
   */
  constexpr event_debounce() noexcept = default;

  /**
   * @brief Constructs a debouncer with a settling period.
   *
   * @param period Minimum time a changed level must persist before it is
   *               forwarded; a negative value is clamped to zero.
   *
   * @pre None.
   * @post \c period() is the clamped \p period.
   */
  explicit constexpr event_debounce(std::chrono::nanoseconds const period) noexcept
      : m_filter{period} {}

  /**
   * @brief The current settling period.
   *
   * @return The configured period; always non-negative.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto period() const noexcept -> std::chrono::nanoseconds {
    return m_filter.period();
  }

  /**
   * @brief The current settling period, for modification.
   *
   * @return Mutable reference to the stored value.
   *
   * @pre A value written through the reference is non-negative.
   * @post None.
   */
  [[nodiscard]] constexpr auto period() noexcept -> std::chrono::nanoseconds& {
    return m_filter.period();
  }

  /**
   * @brief The currently settled physical level, when one exists.
   *
   * Diagnostic view of the filter state: what the debouncer believes the
   * line is holding right now, independent of whether the last \c feed
   * forwarded anything. A supervisor logs this next to a raw read to spot
   * a line that bounces forever without settling.
   *
   * @return The settled level, or \c std::nullopt before the first
   *         acceptance and after \c reset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto stable() const noexcept -> std::optional<bool> {
    if (!m_filter.has_stable()) {
      return std::nullopt;
    }
    return m_filter.stable_value();
  }

  /**
   * @brief Drops all cached state, as after a reopen.
   *
   * @pre None.
   * @post The next \c feed accepts its event as the settled level and
   *       reports it with \c edge_kind::none (an acceptance, not a
   *       transition); \c deadline() is empty.
   */
  constexpr auto reset() noexcept -> void {
    m_filter.reset();
    m_pending.reset();
  }

  /**
   * @brief When the pending level will have held for the period.
   *
   * The time to wake up and call \c expire: pass the gap between this and the
   * current time as the backend's wait timeout, clamped at zero, so a level
   * that holds is reported without waiting for the next edge.
   *
   * @return The timestamp of the event that began the pending level plus the
   *         period, or \c std::nullopt when no level is pending.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto deadline() const noexcept -> std::optional<event_time> {
    if (!m_pending.has_value()) {
      return std::nullopt;
    }
    return m_pending->timestamp + m_filter.period();
  }

  /**
   * @brief Settles the pending level once \p now has reached its deadline.
   *
   * The returned event is a copy of the raw event that began the settled
   * level, with \c edge REPLACED by the direction of the settled transition;
   * its sequence and timestamp are that raw event's.
   *
   * @param now Current time on the clock the line stamps its events with (on
   *        Linux, \c CLOCK_MONOTONIC unless the line asked for another).
   *
   * @return The settled event, or \c std::nullopt when no level is pending or
   *         \p now is before \c deadline().
   *
   * @pre \p now is not earlier than the last fed event's timestamp.
   * @post On a value result the settled level is the new stable value and
   *       \c deadline() is empty.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto expire(event_time const now) noexcept -> std::optional<line_event> {
    auto const due{deadline()};
    if (!due.has_value() || now < *due) {
      return std::nullopt;
    }
    auto out{*m_pending};
    // A repeat of the candidate level after the period is what the engine
    // settles on; by now it has held that long.
    auto const settled{m_filter.update(now.time_since_epoch(), out.physical)};
    m_pending.reset();
    if (!settled.has_value()) {
      return std::nullopt;  // unreachable while m_pending mirrors the candidate
    }
    out.physical = *settled;
    out.edge = *settled ? edge_kind::rising : edge_kind::falling;
    return out;
  }

  /**
   * @brief Feeds one raw physical event through the settle test.
   *
   * A pending level whose deadline \p event has reached settles first, as
   * \c expire at the event's timestamp would, and is returned; \p event then
   * starts the next candidate. Otherwise a settled change returns a copy of
   * the raw event that began the settled level (\p event itself unless the
   * level repeated) with \c edge REPLACED by the direction of the settled
   * transition. The first acceptance after construction or \c reset returns
   * \p event with \c edge_kind::none, because there was no prior settled
   * level to transition from.
   *
   * @param event Raw physical event from a backend or transport drain.
   *
   * @return The settled event, or \c std::nullopt while the line is still
   *         bouncing or unchanged.
   *
   * @pre Event timestamps are non-decreasing across calls, including the
   *      times passed to \c expire.
   * @post On a value result the settled level is the new stable value.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto feed(line_event const& event) noexcept -> std::optional<line_event> {
    if (m_filter.period().count() > 0) {
      if (auto overdue{expire(event.timestamp)}) {
        // With a nonzero period an event never settles on its first sample, so
        // this one only repeats the settled level or starts the next candidate.
        utility::ignore(feed_raw(event));
        return overdue;
      }
    }
    return feed_raw(event);
  }
};

}  // namespace nexenne::gpio
