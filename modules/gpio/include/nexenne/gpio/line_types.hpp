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
 * A monotonic clock with nanosecond resolution and an unspecified epoch: on
 * Linux the kernel stamps events with \c CLOCK_MONOTONIC, and an embedded
 * backend uses its system tick converted to nanoseconds. The clock exists
 * only to give timestamps a distinct \c std::chrono::time_point type; it has
 * no \c now(), because timestamps come from the event source, never from the
 * consumer.
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
