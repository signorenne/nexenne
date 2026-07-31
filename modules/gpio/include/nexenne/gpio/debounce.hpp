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
 */

#include <chrono>
#include <optional>

#include <nexenne/filter/timed_debounce.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief Forwards only the edge events that stay settled for a period.
 *
 * Feed raw physical events in timestamp order; the debouncer returns the
 * event again, with the settled level and the matching edge, once the line
 * has held a new level for at least the period, and \c std::nullopt while
 * it is still bouncing or unchanged. A zero period passes everything
 * through.
 */
class event_debounce {
public:
  using value_type = bool;

private:
  // The engine settles in the timestamp's native nanoseconds, so the
  // comparison never truncates whatever unit the caller thinks in.
  filter::timed_debounce<std::chrono::nanoseconds> m_filter{};

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
   * @brief Replaces the settling period.
   *
   * @param period New settling period; a negative value is clamped to zero.
   *
   * @pre None.
   * @post \c period() is the clamped \p period; the settled level and any
   *       in-progress candidate are unchanged.
   */
  constexpr auto period(std::chrono::nanoseconds const period) noexcept -> void {
    m_filter.period(period);
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
   *       transition).
   */
  constexpr auto reset() noexcept -> void {
    m_filter.reset();
  }

  /**
   * @brief Feeds one raw physical event through the settle test.
   *
   * On a settled change the returned event is a copy of \p event whose
   * \c physical holds the settled level and whose \c edge is REPLACED with
   * the direction of the settled transition; the first acceptance after
   * construction or \c reset carries \c edge_kind::none because there was no
   * prior settled level to transition from. Sequence and timestamp carry
   * through unchanged.
   *
   * @param event Raw physical event from a backend or transport drain.
   *
   * @return The settled event, or \c std::nullopt while the line is still
   *         bouncing or unchanged.
   *
   * @pre Event timestamps are non-decreasing across calls.
   * @post On a value result the settled level is the new stable value.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] constexpr auto feed(line_event const& event) noexcept
    -> std::optional<line_event> {
    bool const had_stable{m_filter.has_stable()};
    auto const settled{m_filter.update(event.timestamp.time_since_epoch(), event.physical)};
    if (!settled.has_value()) {
      return std::nullopt;
    }
    auto out{event};
    out.physical = *settled;
    out.edge = had_stable ? (*settled ? edge_kind::rising : edge_kind::falling) : edge_kind::none;
    return out;
  }
};

}  // namespace nexenne::gpio
