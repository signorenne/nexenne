#pragma once

/**
 * @file
 * @brief Backend-neutral vocabulary types for the nexenne::gpio module.
 *
 * The enums here describe line behaviour at the wrapper level, independent of
 * whether the underlying backend is the Linux character device, an MCU HAL
 * peripheral, or an in-memory test double; backends translate these neutral
 * values into their native equivalents. The strong identifiers (\c chip_id,
 * \c line_offset, \c event_sequence) all share the same shape, a small
 * unsigned integer, and silently passing a chip index where a line offset is
 * expected is a historically common GPIO bug; a distinct type per meaning
 * turns that mistake into a compile error.
 *
 * Time is split into two deliberately distinct types: \c event_time is a
 * tagged point on the clock that stamped an edge, while waits and periods are
 * plain \c std::chrono durations. Keeping the point and the span apart means
 * a timestamp can never be passed where a timeout is expected.
 */

#include <cassert>
#include <chrono>
#include <compare>
#include <cstdint>
#include <optional>

#include <nexenne/utility/strong_typedef.hpp>

namespace nexenne::gpio {

/**
 * @brief Whether a line is read from or written to.
 */
enum class line_direction : std::uint8_t {
  input,   ///< Sampled by the controller; reads reflect the external level.
  output,  ///< Driven by the controller; writes propagate to the hardware.
};

/**
 * @brief Mapping between the physical level on the wire and the logical level.
 *
 * Applying polarity at the wrapper layer keeps application code in the logical
 * domain: a pressed button is \c true regardless of whether the schematic uses
 * an active-low pull-up or an active-high push-button.
 */
enum class line_polarity : std::uint8_t {
  active_high,  ///< Logical \c true is the high physical level.
  active_low,   ///< Logical \c true is the low physical level.
};

/**
 * @brief On-chip bias network requested for an input line.
 */
enum class line_bias : std::uint8_t {
  as_is,      ///< Leave the hardware bias untouched.
  disabled,   ///< Explicitly disable any internal bias.
  pull_up,    ///< Enable the internal pull-up.
  pull_down,  ///< Enable the internal pull-down.
};

/**
 * @brief Output driver topology of an output line.
 */
enum class line_drive : std::uint8_t {
  push_pull,    ///< Both transistors driven; the default for a digital output.
  open_drain,   ///< Only the low side drives; an external pull-up is required.
  open_source,  ///< Only the high side drives; an external pull-down is required.
};

/**
 * @brief The edge transition observed in a single sample or event.
 *
 * A steady-state observation (a synchronous read, or the first settled sample
 * after a debounce reset) carries \c none; an event produced by edge detection
 * carries the transition that fired.
 */
enum class edge_kind : std::uint8_t {
  none,     ///< Steady-state sample; no transition.
  rising,   ///< A low to high transition.
  falling,  ///< A high to low transition.
};

/**
 * @brief The clock a backend is asked to stamp a line's edge events with.
 *
 * The monotonic default is right for measuring time between edges: it never
 * jumps. The realtime selection stamps events with the wall clock instead,
 * which is what a system needs when edge timestamps must line up with logs
 * or with events recorded on other hosts; it inherits the wall clock's
 * ability to step under NTP. The hte selection asks the hardware timestamp
 * engine to stamp events in hardware, at the pin, removing interrupt and
 * scheduling latency from the timestamp; only platforms with such an engine
 * (NVIDIA Tegra is the known one) accept it. A backend without the
 * requested clock rejects the open as unsupported rather than silently
 * stamping from another clock.
 */
enum class line_clock : std::uint8_t {
  monotonic,  ///< A clock that never goes backwards; the default.
  realtime,   ///< The wall clock; timestamps line up with log time.
  hte,        ///< The hardware timestamp engine; stamped at the pin.
};

/**
 * @brief The edge events a backend is asked to deliver for a line.
 *
 * This is the subscription side of \c edge_kind: a request can ask for both
 * directions, but a single delivered event always names exactly one.
 */
enum class edge_detection : std::uint8_t {
  none,     ///< Polling only; no event stream is produced for the line.
  rising,   ///< Deliver low to high transitions.
  falling,  ///< Deliver high to low transitions.
  both,     ///< Deliver transitions in both directions.
};

/**
 * @brief Whether a delivered edge falls under a subscription.
 *
 * The router's predicate: a consumer subscribed to one direction filters a
 * shared event stream with this instead of hand-written enum comparisons.
 * A steady-state observation (\c edge_kind::none) matches no subscription,
 * and \c edge_detection::none matches nothing at all.
 *
 * @param subscription Edge events a consumer asked for.
 * @param edge Edge direction of the delivered event.
 *
 * @return \c true when \p edge is one of the directions \p subscription
 *         asks for.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto
matches(edge_detection const subscription, edge_kind const edge) noexcept -> bool {
  switch (subscription) {
    case edge_detection::none:
      return false;
    case edge_detection::rising:
      return edge == edge_kind::rising;
    case edge_detection::falling:
      return edge == edge_kind::falling;
    case edge_detection::both:
      return edge != edge_kind::none;
  }
  return false;
}

/**
 * @brief Maps a physical level to its logical level under a polarity.
 *
 * For \c line_polarity::active_low the level is inverted; for
 * \c line_polarity::active_high it passes through unchanged. The mapping is
 * its own inverse, so the same function converts logical back to physical.
 *
 * @param level Level to convert.
 * @param polarity Polarity of the line.
 *
 * @return The converted level.
 *
 * @pre None.
 * @post The result equals \p level exactly when \p polarity is
 *       \c line_polarity::active_high.
 */
[[nodiscard]] constexpr auto apply_polarity(bool const level, line_polarity const polarity) noexcept
  -> bool {
  return polarity == line_polarity::active_low ? !level : level;
}

/**
 * @brief Maps a physical edge direction to its logical direction under a polarity.
 *
 * For \c line_polarity::active_low a physical rising edge is the logical
 * falling edge and vice versa; \c edge_kind::none passes through. Applying
 * polarity to the level and the edge together is what keeps an active-low
 * line from ever surfacing an inconsistent pair (a logical \c true paired
 * with a falling edge that never happened).
 *
 * @param edge Edge direction to convert.
 * @param polarity Polarity of the line.
 *
 * @return The converted edge direction.
 *
 * @pre None.
 * @post The result equals \p edge exactly when \p polarity is
 *       \c line_polarity::active_high or \p edge is \c edge_kind::none.
 */
[[nodiscard]] constexpr auto
apply_polarity(edge_kind const edge, line_polarity const polarity) noexcept -> edge_kind {
  if (polarity == line_polarity::active_high || edge == edge_kind::none) {
    return edge;
  }
  return edge == edge_kind::rising ? edge_kind::falling : edge_kind::rising;
}

/**
 * @brief Identifier of a GPIO chip (a bank of lines) within a system.
 *
 * On Linux this is the N in \c /dev/gpiochipN; on an embedded target the
 * backend defines the mapping (often a port letter mapped to a small integer).
 */
using chip_id = utility::identifier<struct chip_id_tag, std::uint16_t>;

/**
 * @brief Zero-based offset of a line within its chip.
 */
using line_offset = utility::identifier<struct line_offset_tag, std::uint32_t>;

/**
 * @brief Monotonic sequence number of an edge event.
 *
 * Consumers compare consecutive sequence numbers to detect events dropped by
 * an overflowed kernel or transport buffer; \c sequence_tracker.hpp automates
 * the comparison. Zero means "unset".
 */
using event_sequence = utility::identifier<struct event_sequence_tag, std::uint64_t>;

/**
 * @brief The steady clock a line stamps events on by default.
 *
 * A nanosecond clock with an unspecified epoch that never steps: on Linux the
 * kernel's \c CLOCK_MONOTONIC, on an embedded backend its system tick. It has
 * no \c now(), because timestamps come from the event source, never from the
 * consumer.
 */
struct monotonic_event_clock {
  using rep = std::int64_t;                                           ///< Tick representation.
  using period = std::nano;                                           ///< Tick period.
  using duration = std::chrono::nanoseconds;                          ///< Duration type.
  using time_point = std::chrono::time_point<monotonic_event_clock>;  ///< Timestamp type.
  static constexpr bool is_steady{true};  ///< Steady: a monotonic stamp never goes back.
};

/**
 * @brief The wall clock a line requested with \c line_clock::realtime stamps on.
 *
 * Nanoseconds since the Unix epoch, so stamps line up with log time and with
 * other hosts; not steady, since the wall clock steps under NTP. It has no
 * \c now() for the same reason as \c monotonic_event_clock.
 */
struct realtime_event_clock {
  using rep = std::int64_t;                                          ///< Tick representation.
  using period = std::nano;                                          ///< Tick period.
  using duration = std::chrono::nanoseconds;                         ///< Duration type.
  using time_point = std::chrono::time_point<realtime_event_clock>;  ///< Timestamp type.
  static constexpr bool is_steady{false};  ///< Not steady: the wall clock can step back.
};

/**
 * @brief When an edge event fired, tagged with the clock that stamped it.
 *
 * Each line picks its clock at run time through \c line_clock, and one
 * request can mix clocks, so a timestamp carries its clock rather than being
 * a single \c time_point type. \c monotonic() and \c realtime() hand it out
 * as the matching typed \c time_point, or \c std::nullopt for another clock.
 * Arithmetic and ordering read the raw nanoseconds and assume both operands
 * share a clock. A timestamp is not a duration, so it cannot be passed where a
 * timeout or a debounce period is expected.
 */
class event_time {
public:
  using value_type = std::chrono::nanoseconds;  ///< Representation of the offset.

private:
  line_clock m_clock{line_clock::monotonic};
  std::chrono::nanoseconds m_since_epoch{};

public:
  /**
   * @brief Constructs the monotonic epoch.
   *
   * @pre None.
   * @post \c clock() is \c line_clock::monotonic and \c time_since_epoch() is zero.
   */
  constexpr event_time() noexcept = default;

  /**
   * @brief Constructs a timestamp from its offset and its clock.
   *
   * @param since_epoch Offset from the clock's epoch.
   * @param clock Clock that stamped the event; monotonic by default.
   *
   * @pre None.
   * @post \c time_since_epoch() equals \p since_epoch and \c clock() equals \p clock.
   */
  constexpr explicit event_time(
    std::chrono::nanoseconds const since_epoch, line_clock const clock = line_clock::monotonic
  ) noexcept
      : m_clock{clock}, m_since_epoch{since_epoch} {}

  /**
   * @brief Constructs a monotonic timestamp from its typed time point.
   *
   * @param t Point on the monotonic clock.
   *
   * @pre None.
   * @post \c monotonic() equals \p t.
   */
  constexpr explicit event_time(monotonic_event_clock::time_point const t) noexcept
      : m_clock{line_clock::monotonic}, m_since_epoch{t.time_since_epoch()} {}

  /**
   * @brief Constructs a realtime timestamp from its typed time point.
   *
   * @param t Point on the wall clock.
   *
   * @pre None.
   * @post \c realtime() equals \p t.
   */
  constexpr explicit event_time(realtime_event_clock::time_point const t) noexcept
      : m_clock{line_clock::realtime}, m_since_epoch{t.time_since_epoch()} {}

  /**
   * @brief The clock that stamped the event.
   *
   * @return The clock.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto clock() const noexcept -> line_clock {
    return m_clock;
  }

  /**
   * @brief The clock that stamped the event, for modification.
   *
   * @return Mutable reference to the clock.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto clock() noexcept -> line_clock& {
    return m_clock;
  }

  /**
   * @brief The offset from the clock's epoch.
   *
   * @return The offset in nanoseconds.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto time_since_epoch() const noexcept -> std::chrono::nanoseconds {
    return m_since_epoch;
  }

  /**
   * @brief The offset from the clock's epoch, for modification.
   *
   * @return Mutable reference to the offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto time_since_epoch() noexcept -> std::chrono::nanoseconds& {
    return m_since_epoch;
  }

  /**
   * @brief The timestamp as a steady time point, when the monotonic clock stamped it.
   *
   * @return The monotonic time point, or \c std::nullopt for another clock.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto monotonic() const noexcept
    -> std::optional<monotonic_event_clock::time_point> {
    if (m_clock != line_clock::monotonic) {
      return std::nullopt;
    }
    return monotonic_event_clock::time_point{m_since_epoch};
  }

  /**
   * @brief The timestamp as a wall-clock time point, when the realtime clock stamped it.
   *
   * @return The realtime time point, or \c std::nullopt for another clock.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto realtime() const noexcept
    -> std::optional<realtime_event_clock::time_point> {
    if (m_clock != line_clock::realtime) {
      return std::nullopt;
    }
    return realtime_event_clock::time_point{m_since_epoch};
  }

  /**
   * @brief The timestamp \p d later, on the same clock.
   *
   * @param t Timestamp.
   * @param d Offset to add.
   *
   * @return \p t moved by \p d.
   *
   * @pre None.
   * @post The result has the clock of \p t.
   */
  [[nodiscard]] friend constexpr auto
  operator+(event_time const t, std::chrono::nanoseconds const d) noexcept -> event_time {
    return event_time{t.m_since_epoch + d, t.m_clock};
  }

  /**
   * @brief The span between two timestamps.
   *
   * @param a Later timestamp.
   * @param b Earlier timestamp.
   *
   * @return \c a - \c b in nanoseconds.
   *
   * @pre \p a and \p b share a clock.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator-(event_time const a, event_time const b) noexcept
    -> std::chrono::nanoseconds {
    assert(a.m_clock == b.m_clock && "event_time difference needs one clock");
    return a.m_since_epoch - b.m_since_epoch;
  }

  /**
   * @brief Equality of clock and offset.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both the clock and the offset match.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(event_time const& lhs, event_time const& rhs) noexcept -> bool = default;

  /**
   * @brief Orders by clock, then by offset, so timestamps on one clock sort by time.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return The three-way ordering.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator<=>(event_time const& lhs, event_time const& rhs) noexcept = default;
};

}  // namespace nexenne::gpio
