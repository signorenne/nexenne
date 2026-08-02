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
 * point on the event clock (when an edge fired), while waits and periods are
 * plain \c std::chrono durations. Keeping the point and the span apart means
 * a timestamp can never be passed where a timeout is expected.
 */

#include <chrono>
#include <cstdint>

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
[[nodiscard]] constexpr auto apply_polarity(edge_kind const edge, line_polarity const polarity)
  noexcept -> edge_kind {
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
 * @brief The clock edge-event timestamps are expressed on.
 *
 * A nanosecond clock with an unspecified epoch: on Linux the kernel stamps
 * events with \c CLOCK_MONOTONIC by default (an embedded backend uses its
 * system tick), and a line requested with \c line_clock::realtime carries
 * the wall clock's epoch instead. The clock exists only to give timestamps
 * a distinct \c std::chrono::time_point type; it has no \c now(), because
 * timestamps come from the event source, never from the consumer.
 */
struct event_clock {
  using rep = std::int64_t;                                ///< Tick representation.
  using period = std::nano;                                ///< Tick period.
  using duration = std::chrono::nanoseconds;               ///< Duration type.
  using time_point = std::chrono::time_point<event_clock>; ///< Timestamp type.

  /// @brief The clock never goes backwards.
  static constexpr bool is_steady{true};
};

/**
 * @brief A point in time on \c event_clock: when an edge event fired.
 *
 * Subtracting two timestamps yields a \c std::chrono::nanoseconds span, and a
 * timestamp cannot be passed where a duration (a timeout or a debounce
 * period) is expected.
 */
using event_time = event_clock::time_point;

}  // namespace nexenne::gpio
