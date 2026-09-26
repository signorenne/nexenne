#pragma once

/**
 * @file
 * @brief Debug printing and formatting for nexenne::gpio types.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The
 * output is for diagnostics, not serialisation, and is not stable across
 * versions. This header is the single owner of the standard format header for
 * the module, so error.hpp and the value-type headers stay free of it. Each
 * \c std::formatter inherits \c std::formatter<std::string_view>, so a width
 * or alignment spec applies to the whole rendered string. The stateful types
 * (the handles, the backends, the sinks, the debouncer, and the sequence
 * tracker) print a one-line summary of what their public accessors expose.
 */

#include <cstddef>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/chip.hpp>
#include <nexenne/gpio/debounce.hpp>
#include <nexenne/gpio/drain.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/io/callback_sink.hpp>
#include <nexenne/gpio/io/chardev_chip.hpp>
#include <nexenne/gpio/io/chardev_info.hpp>
#include <nexenne/gpio/io/chardev_watch.hpp>
#include <nexenne/gpio/io/mock_chip.hpp>
#include <nexenne/gpio/io/queue_sink.hpp>
#include <nexenne/gpio/line.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/gpio/line_value.hpp>
#include <nexenne/gpio/sequence_tracker.hpp>

namespace nexenne::gpio {

/**
 * @brief Streams a \c gpio_error by its \c to_string name.
 *
 * @param os Output stream.
 * @param err Error to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The error name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, gpio_error const err) -> std::ostream& {
  return os << to_string(err);
}

/**
 * @brief Name of a \c line_direction.
 *
 * @param direction Direction to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_direction const direction) noexcept
  -> std::string_view {
  switch (direction) {
    case line_direction::input:
      return "input";
    case line_direction::output:
      return "output";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_direction by its \c to_string name.
 *
 * @param os Output stream.
 * @param direction Direction to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_direction const direction) -> std::ostream& {
  return os << to_string(direction);
}

/**
 * @brief Name of a \c line_polarity.
 *
 * @param polarity Polarity to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_polarity const polarity) noexcept -> std::string_view {
  switch (polarity) {
    case line_polarity::active_high:
      return "active_high";
    case line_polarity::active_low:
      return "active_low";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_polarity by its \c to_string name.
 *
 * @param os Output stream.
 * @param polarity Polarity to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_polarity const polarity) -> std::ostream& {
  return os << to_string(polarity);
}

/**
 * @brief Name of a \c line_bias.
 *
 * @param bias Bias to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_bias const bias) noexcept -> std::string_view {
  switch (bias) {
    case line_bias::as_is:
      return "as_is";
    case line_bias::disabled:
      return "disabled";
    case line_bias::pull_up:
      return "pull_up";
    case line_bias::pull_down:
      return "pull_down";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_bias by its \c to_string name.
 *
 * @param os Output stream.
 * @param bias Bias to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_bias const bias) -> std::ostream& {
  return os << to_string(bias);
}

/**
 * @brief Name of a \c line_drive.
 *
 * @param drive Drive topology to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_drive const drive) noexcept -> std::string_view {
  switch (drive) {
    case line_drive::push_pull:
      return "push_pull";
    case line_drive::open_drain:
      return "open_drain";
    case line_drive::open_source:
      return "open_source";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_drive by its \c to_string name.
 *
 * @param os Output stream.
 * @param drive Drive topology to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_drive const drive) -> std::ostream& {
  return os << to_string(drive);
}

/**
 * @brief Name of a \c line_clock selection.
 *
 * @param clock Clock selection to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_clock const clock) noexcept -> std::string_view {
  switch (clock) {
    case line_clock::monotonic:
      return "monotonic";
    case line_clock::realtime:
      return "realtime";
    case line_clock::hte:
      return "hte";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_clock by its \c to_string name.
 *
 * @param os Output stream.
 * @param clock Clock selection to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_clock const clock) -> std::ostream& {
  return os << to_string(clock);
}

/**
 * @brief Debug string for an \c event_time.
 *
 * Example: \c "1200ns" on the monotonic clock, \c "1200ns realtime" on any
 * other.
 *
 * @param t Timestamp to print.
 *
 * @return The offset in nanoseconds, followed by the clock when not monotonic.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(event_time const t) -> std::string {
  if (t.clock() == line_clock::monotonic) {
    return std::format("{}ns", t.time_since_epoch().count());
  }
  return std::format("{}ns {}", t.time_since_epoch().count(), to_string(t.clock()));
}

/**
 * @brief Streams an \c event_time via its \c to_string.
 *
 * @param os Output stream.
 * @param t Timestamp to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted timestamp has been written to \p os.
 */
inline auto operator<<(std::ostream& os, event_time const t) -> std::ostream& {
  return os << to_string(t);
}

/**
 * @brief Name of an \c edge_kind.
 *
 * @param edge Edge direction to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(edge_kind const edge) noexcept -> std::string_view {
  switch (edge) {
    case edge_kind::none:
      return "none";
    case edge_kind::rising:
      return "rising";
    case edge_kind::falling:
      return "falling";
  }
  return "unknown";
}

/**
 * @brief Streams an \c edge_kind by its \c to_string name.
 *
 * @param os Output stream.
 * @param edge Edge direction to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, edge_kind const edge) -> std::ostream& {
  return os << to_string(edge);
}

/**
 * @brief Name of an \c edge_detection subscription.
 *
 * @param edges Subscription to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(edge_detection const edges) noexcept -> std::string_view {
  switch (edges) {
    case edge_detection::none:
      return "none";
    case edge_detection::rising:
      return "rising";
    case edge_detection::falling:
      return "falling";
    case edge_detection::both:
      return "both";
  }
  return "unknown";
}

/**
 * @brief Streams an \c edge_detection by its \c to_string name.
 *
 * @param os Output stream.
 * @param edges Subscription to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, edge_detection const edges) -> std::ostream& {
  return os << to_string(edges);
}

/**
 * @brief Debug string for a \c line_spec.
 *
 * Example: \c "line_spec(button, chip 0, line 17, input, active_low,
 * pull_up, push_pull)".
 *
 * @param spec Spec to print.
 *
 * @return The debug string with every field named.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_spec const& spec) -> std::string {
  return std::format(
    "line_spec({}, chip {}, line {}, {}, {}, {}, {})",
    spec.name().empty() ? std::string_view{"unnamed"} : spec.name(),
    spec.chip().get(),
    spec.offset().get(),
    to_string(spec.direction()),
    to_string(spec.polarity()),
    to_string(spec.bias()),
    to_string(spec.drive())
  );
}

/**
 * @brief Streams a \c line_spec via its \c to_string.
 *
 * @param os Output stream.
 * @param spec Spec to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted spec has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_spec const& spec) -> std::ostream& {
  return os << to_string(spec);
}

/**
 * @brief Debug string for a \c line_config.
 *
 * Example: \c "line_config(edges=both, debounce=5000000ns, initial=low,
 * clock=monotonic)".
 *
 * @param config Config to print.
 *
 * @return The debug string with every knob named.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_config const& config) -> std::string {
  return std::format(
    "line_config(edges={}, debounce={}ns, initial={}, clock={})",
    to_string(config.edges()),
    config.debounce_period().count(),
    config.initial_value() ? "high" : "low",
    to_string(config.clock())
  );
}

/**
 * @brief Streams a \c line_config via its \c to_string.
 *
 * @param os Output stream.
 * @param config Config to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted config has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_config const& config) -> std::ostream& {
  return os << to_string(config);
}

/**
 * @brief Debug string for a \c line_event.
 *
 * Example: \c "line_event(chip 0, line 17, rising, physical=high, seq=7,
 * t=1200ns)".
 *
 * @param event Event to print.
 *
 * @return The debug string with every field named.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_event const& event) -> std::string {
  return std::format(
    "line_event(chip {}, line {}, {}, physical={}, seq={}, t={})",
    event.chip.get(),
    event.offset.get(),
    to_string(event.edge),
    event.physical ? "high" : "low",
    event.sequence.get(),
    to_string(event.timestamp)
  );
}

/**
 * @brief Streams a \c line_event via its \c to_string.
 *
 * @param os Output stream.
 * @param event Event to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted event has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_event const& event) -> std::ostream& {
  return os << to_string(event);
}

/**
 * @brief Debug string for a \c line_value.
 *
 * Example: \c "line_value(button, chip 0, line 17, logical=true, rising,
 * seq=7, t=1200ns)".
 *
 * @param value Observation to print.
 *
 * @return The debug string with every field named.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_value const& value) -> std::string {
  return std::format(
    "line_value({}, chip {}, line {}, logical={}, {}, seq={}, t={})",
    value.name().empty() ? std::string_view{"unnamed"} : value.name(),
    value.chip().get(),
    value.offset().get(),
    value.logical(),
    to_string(value.edge()),
    value.sequence().get(),
    to_string(value.timestamp())
  );
}

/**
 * @brief Streams a \c line_value via its \c to_string.
 *
 * @param os Output stream.
 * @param value Observation to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted observation has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_value const& value) -> std::ostream& {
  return os << to_string(value);
}

/**
 * @brief Debug string for a \c chip_info.
 *
 * Example: \c "chip_info(gpiochip0, INT34C6:00, 32 lines)".
 *
 * @param info Chip record to print.
 *
 * @return The debug string with the name, label, and line count.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(chip_info const& info) -> std::string {
  return std::format(
    "chip_info({}, {}, {} lines)",
    info.name().empty() ? std::string_view{"unnamed"} : info.name(),
    info.label().empty() ? std::string_view{"unlabeled"} : info.label(),
    info.lines()
  );
}

/**
 * @brief Streams a \c chip_info via its \c to_string.
 *
 * @param os Output stream.
 * @param info Chip record to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted record has been written to \p os.
 */
inline auto operator<<(std::ostream& os, chip_info const& info) -> std::ostream& {
  return os << to_string(info);
}

/**
 * @brief Debug string for a \c line_info.
 *
 * Example: \c "line_info(line 4, LED_1, used by leds, output, as_is,
 * push_pull, edges=none)".
 *
 * @param info Line record to print.
 *
 * @return The debug string with the identity, holder, and configuration.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_info const& info) -> std::string {
  auto const holder{
    info.used()
      ? std::format(
          "used by {}", info.consumer().empty() ? std::string_view{"unknown"} : info.consumer()
        )
      : std::string{"unused"}
  };
  return std::format(
    "line_info(line {}, {}, {}, {}{}, {}, {}, edges={})",
    info.offset().get(),
    info.name().empty() ? std::string_view{"unnamed"} : info.name(),
    holder,
    to_string(info.direction()),
    info.active_low() ? std::string_view{" active_low"} : std::string_view{},
    to_string(info.bias()),
    to_string(info.drive()),
    to_string(info.edges())
  );
}

/**
 * @brief Streams a \c line_info via its \c to_string.
 *
 * @param os Output stream.
 * @param info Line record to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted record has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_info const& info) -> std::ostream& {
  return os << to_string(info);
}

/**
 * @brief Name of a \c line_change_kind.
 *
 * @param kind Change kind to name.
 *
 * @return A static string view naming the enumerator.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(line_change_kind const kind) noexcept -> std::string_view {
  switch (kind) {
    case line_change_kind::requested:
      return "requested";
    case line_change_kind::released:
      return "released";
    case line_change_kind::reconfigured:
      return "reconfigured";
  }
  return "unknown";
}

/**
 * @brief Streams a \c line_change_kind by its \c to_string name.
 *
 * @param os Output stream.
 * @param kind Change kind to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_change_kind const kind) -> std::ostream& {
  return os << to_string(kind);
}

/**
 * @brief Debug string for a \c line_change.
 *
 * Example: \c "line_change(requested, t=1200ns, line_info(...))".
 *
 * @param change Change record to print.
 *
 * @return The debug string with the kind, timestamp, and updated info.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(line_change const& change) -> std::string {
  return std::format(
    "line_change({}, t={}, {})",
    to_string(change.kind),
    to_string(change.timestamp),
    to_string(change.info)
  );
}

/**
 * @brief Streams a \c line_change via its \c to_string.
 *
 * @param os Output stream.
 * @param change Change record to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted record has been written to \p os.
 */
inline auto operator<<(std::ostream& os, line_change const& change) -> std::ostream& {
  return os << to_string(change);
}

/**
 * @brief Renders a \c drain_report's two counters.
 *
 * Output looks like \c "drain_report(delivered=12, rejected=0)".
 *
 * @param report Report to render.
 *
 * @return A freshly allocated debug string.
 *
 * @pre None.
 * @post None.
 *
 * @throws std::bad_alloc if the string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(drain_report const& report) -> std::string {
  return std::format("drain_report(delivered={}, rejected={})", report.delivered, report.rejected);
}

/**
 * @brief Streams a \c drain_report via its \c to_string.
 *
 * @param os Output stream.
 * @param report Report to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The description of \p report has been written to \p os.
 */
inline auto operator<<(std::ostream& os, drain_report const& report) -> std::ostream& {
  return os << to_string(report);
}

/**
 * @brief Debug string for a \c sequence_tracker.
 *
 * Example: \c "sequence_tracker(last=7, dropped=2)".
 *
 * @param tracker Tracker to print.
 *
 * @return The debug string with the last sequence and the drop count.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(sequence_tracker const& tracker) -> std::string {
  return std::format(
    "sequence_tracker(last={}, dropped={})", tracker.last().get(), tracker.dropped()
  );
}

/**
 * @brief Streams a \c sequence_tracker via its \c to_string.
 *
 * @param os Output stream.
 * @param tracker Tracker to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted tracker has been written to \p os.
 */
inline auto operator<<(std::ostream& os, sequence_tracker const& tracker) -> std::ostream& {
  return os << to_string(tracker);
}

/**
 * @brief Debug string for an \c event_debounce.
 *
 * Example: \c "event_debounce(period=5000000ns, stable=low,
 * deadline=5002000ns)"; a missing settled level or deadline prints \c none.
 *
 * @param debounce Debouncer to print.
 *
 * @return The debug string with the period, settled level, and deadline.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(event_debounce const& debounce) -> std::string {
  auto const stable{debounce.stable()};
  auto const deadline{debounce.deadline()};
  return std::format(
    "event_debounce(period={}ns, stable={}, deadline={})",
    debounce.period().count(),
    stable.has_value() ? (*stable ? "high" : "low") : "none",
    deadline.has_value() ? to_string(*deadline) : std::string{"none"}
  );
}

/**
 * @brief Streams an \c event_debounce via its \c to_string.
 *
 * @param os Output stream.
 * @param debounce Debouncer to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted debouncer has been written to \p os.
 */
inline auto operator<<(std::ostream& os, event_debounce const& debounce) -> std::ostream& {
  return os << to_string(debounce);
}

/**
 * @brief Debug string for a \c callback_sink.
 *
 * Always \c "callback_sink()": the sink holds only its handler, which has no
 * printable state.
 *
 * @tparam Handler Callable type of the sink.
 * @param sink Sink to print.
 *
 * @return The debug string naming the sink.
 *
 * @pre None.
 * @post None.
 */
template <typename Handler>
[[nodiscard]] auto to_string([[maybe_unused]] callback_sink<Handler> const& sink) -> std::string {
  return std::string{"callback_sink()"};
}

/**
 * @brief Streams a \c callback_sink via its \c to_string.
 *
 * @tparam Handler Callable type of the sink.
 * @param os Output stream.
 * @param sink Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted sink has been written to \p os.
 */
template <typename Handler>
auto operator<<(std::ostream& os, callback_sink<Handler> const& sink) -> std::ostream& {
  return os << to_string(sink);
}

/**
 * @brief Debug string for a \c queue_sink.
 *
 * Example: \c "queue_sink(capacity=7, empty=false, dropped=1)". The
 * emptiness is the best-effort observation \c empty() makes.
 *
 * @tparam N Ring slot count of the sink.
 * @param sink Sink to print.
 *
 * @return The debug string with the capacity, emptiness, and drop count.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
[[nodiscard]] auto to_string(queue_sink<N> const& sink) -> std::string {
  return std::format(
    "queue_sink(capacity={}, empty={}, dropped={})", sink.capacity(), sink.empty(), sink.dropped()
  );
}

/**
 * @brief Streams a \c queue_sink via its \c to_string.
 *
 * @tparam N Ring slot count of the sink.
 * @param os Output stream.
 * @param sink Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted sink has been written to \p os.
 */
template <std::size_t N>
auto operator<<(std::ostream& os, queue_sink<N> const& sink) -> std::ostream& {
  return os << to_string(sink);
}

/**
 * @brief Debug string for a \c line handle.
 *
 * Example: \c "line(bound, line_spec(button, chip 0, line 17, input,
 * active_low, pull_up, push_pull))"; an unbound handle prints \c unbound.
 *
 * @tparam Backend Backend type of the handle.
 * @param handle Handle to print.
 *
 * @return The debug string with the binding state and the spec.
 *
 * @pre None.
 * @post None.
 */
template <gpio_backend Backend>
[[nodiscard]] auto to_string(line<Backend> const& handle) -> std::string {
  return std::format(
    "line({}, {})", handle.valid() ? "bound" : "unbound", to_string(handle.spec())
  );
}

/**
 * @brief Streams a \c line handle via its \c to_string.
 *
 * @tparam Backend Backend type of the handle.
 * @param os Output stream.
 * @param handle Handle to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted handle has been written to \p os.
 */
template <gpio_backend Backend>
auto operator<<(std::ostream& os, line<Backend> const& handle) -> std::ostream& {
  return os << to_string(handle);
}

/**
 * @brief Debug string for a \c chip handle.
 *
 * Example: \c "chip(open, lines={button: 17, led: 4})", mapping each line
 * name of the request set to its offset in spec-table order. The state is
 * \c unbound without a backend, otherwise \c open or \c closed as the
 * backend reports.
 *
 * @tparam Backend Backend type of the chip.
 * @param handle Chip to print.
 *
 * @return The debug string with the state and the name-to-offset table.
 *
 * @pre None.
 * @post None.
 */
template <gpio_backend Backend>
[[nodiscard]] auto to_string(chip<Backend> const& handle) -> std::string {
  auto const state{
    handle.backend() == nullptr ? std::string_view{"unbound"}
    : handle.is_open()          ? std::string_view{"open"}
                                : std::string_view{"closed"}
  };
  auto out{std::format("chip({}, lines={{", state)};
  auto separator{std::string_view{}};
  for (auto const& spec : handle.specs()) {
    out += std::format(
      "{}{}: {}",
      separator,
      spec.name().empty() ? std::string_view{"unnamed"} : spec.name(),
      spec.offset().get()
    );
    separator = ", ";
  }
  out += "})";
  return out;
}

/**
 * @brief Streams a \c chip handle via its \c to_string.
 *
 * @tparam Backend Backend type of the chip.
 * @param os Output stream.
 * @param handle Chip to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted chip has been written to \p os.
 */
template <gpio_backend Backend>
auto operator<<(std::ostream& os, chip<Backend> const& handle) -> std::ostream& {
  return os << to_string(handle);
}

/**
 * @brief Debug string for a \c mock_chip.
 *
 * Example: \c "mock_chip(open, 2 lines)".
 *
 * @tparam Capacity Line and event capacity of the mock.
 * @param backend Mock to print.
 *
 * @return The debug string with the open state and the request-set size.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Capacity>
[[nodiscard]] auto to_string(mock_chip<Capacity> const& backend) -> std::string {
  return std::format(
    "mock_chip({}, {} lines)", backend.is_open() ? "open" : "closed", backend.line_count()
  );
}

/**
 * @brief Streams a \c mock_chip via its \c to_string.
 *
 * @tparam Capacity Line and event capacity of the mock.
 * @param os Output stream.
 * @param backend Mock to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted mock has been written to \p os.
 */
template <std::size_t Capacity>
auto operator<<(std::ostream& os, mock_chip<Capacity> const& backend) -> std::ostream& {
  return os << to_string(backend);
}

/**
 * @brief Debug string for a \c chardev_chip.
 *
 * Example: \c "chardev_chip(chip 0, consumer=nexenne-gpio, open, 2 lines)";
 * an empty consumer label prints \c unlabeled.
 *
 * @param backend Backend to print.
 *
 * @return The debug string with the chip index, consumer label, open state,
 *         and request size.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(chardev_chip const& backend) -> std::string {
  return std::format(
    "chardev_chip(chip {}, consumer={}, {}, {} lines)",
    backend.chip().get(),
    backend.consumer().empty() ? std::string_view{"unlabeled"} : backend.consumer(),
    backend.is_open() ? "open" : "closed",
    backend.line_count()
  );
}

/**
 * @brief Streams a \c chardev_chip via its \c to_string.
 *
 * @param os Output stream.
 * @param backend Backend to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted backend has been written to \p os.
 */
inline auto operator<<(std::ostream& os, chardev_chip const& backend) -> std::ostream& {
  return os << to_string(backend);
}

/**
 * @brief Debug string for a \c chardev_watcher.
 *
 * Example: \c "chardev_watcher(chip 0, open)".
 *
 * @param watcher Watcher to print.
 *
 * @return The debug string with the chip index and the open state.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto to_string(chardev_watcher const& watcher) -> std::string {
  return std::format(
    "chardev_watcher(chip {}, {})", watcher.chip().get(), watcher.is_open() ? "open" : "closed"
  );
}

/**
 * @brief Streams a \c chardev_watcher via its \c to_string.
 *
 * @param os Output stream.
 * @param watcher Watcher to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The formatted watcher has been written to \p os.
 */
inline auto operator<<(std::ostream& os, chardev_watcher const& watcher) -> std::ostream& {
  return os << to_string(watcher);
}

}  // namespace nexenne::gpio

/**
 * @brief \c std::format support for \c gpio_error.
 *
 * Prints the error's \c to_string name, so \c std::format("{}", err) works
 * on a value taken from a \c result<T>. Inherits the string formatter, so a
 * spec (width, alignment) applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::gpio_error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param err Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::gpio_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_direction, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_direction> : std::formatter<std::string_view> {
  /**
   * @brief Formats the direction's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param direction Direction to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_direction const direction, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(direction), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_polarity, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_polarity> : std::formatter<std::string_view> {
  /**
   * @brief Formats the polarity's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param polarity Polarity to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_polarity const polarity, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(polarity), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_bias, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_bias> : std::formatter<std::string_view> {
  /**
   * @brief Formats the bias name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param bias Bias to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_bias const bias, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(bias), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_drive, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_drive> : std::formatter<std::string_view> {
  /**
   * @brief Formats the drive-topology name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param drive Drive topology to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_drive const drive, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(drive), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_clock, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_clock> : std::formatter<std::string_view> {
  /**
   * @brief Formats the clock-selection name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param clock Clock selection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_clock const clock, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(clock), ctx);
  }
};

/**
 * @brief \c std::format support for \c event_time, printing its \c to_string form.
 *
 * Inherits the string formatter, so a spec applies to the whole timestamp.
 */
template <>
struct std::formatter<nexenne::gpio::event_time> : std::formatter<std::string_view> {
  /**
   * @brief Formats the timestamp through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param t Timestamp to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The timestamp has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::event_time const t, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(t), ctx);
  }
};

/**
 * @brief \c std::format support for \c edge_kind, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::edge_kind> : std::formatter<std::string_view> {
  /**
   * @brief Formats the edge-direction name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param edge Edge direction to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::edge_kind const edge, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(edge), ctx);
  }
};

/**
 * @brief \c std::format support for \c edge_detection, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::edge_detection> : std::formatter<std::string_view> {
  /**
   * @brief Formats the subscription name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param edges Subscription to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::edge_detection const edges, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(edges), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_spec, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_spec> : std::formatter<std::string_view> {
  /**
   * @brief Formats the spec's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param spec Spec to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted spec has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_spec const& spec, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(spec), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_config, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_config> : std::formatter<std::string_view> {
  /**
   * @brief Formats the config's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param config Config to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted config has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_config const& config, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(config), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_event, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_event> : std::formatter<std::string_view> {
  /**
   * @brief Formats the event's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param event Event to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted event has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_event const& event, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(event), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_value, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_value> : std::formatter<std::string_view> {
  /**
   * @brief Formats the observation's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param value Observation to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted observation has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_value const& value, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(value), ctx);
  }
};

/**
 * @brief \c std::format support for \c chip_info, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::chip_info> : std::formatter<std::string_view> {
  /**
   * @brief Formats the chip record's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param info Chip record to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted record has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::chip_info const& info, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(info), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_change_kind, printing its name.
 *
 * Inherits the string formatter, so a spec applies to the name.
 */
template <>
struct std::formatter<nexenne::gpio::line_change_kind> : std::formatter<std::string_view> {
  /**
   * @brief Formats the change-kind name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param kind Change kind to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_change_kind const kind, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(kind), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_change, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_change> : std::formatter<std::string_view> {
  /**
   * @brief Formats the change record's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param change Change record to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted record has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_change const& change, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(change), ctx);
  }
};

/**
 * @brief \c std::format support for \c line_info, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 */
template <>
struct std::formatter<nexenne::gpio::line_info> : std::formatter<std::string_view> {
  /**
   * @brief Formats the line record's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param info Line record to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted record has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line_info const& info, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(info), ctx);
  }
};

/**
 * @brief \c std::format support for \c drain_report.
 *
 * Prints the same text as \c to_string. Inherits the string formatter, so a
 * spec (width, alignment) applies to the whole rendered string.
 */
template <>
struct std::formatter<nexenne::gpio::drain_report> : std::formatter<std::string_view> {
  /**
   * @brief Formats the report's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param report Report to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted report has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::drain_report const& report, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(report), ctx);
  }
};

/**
 * @brief \c std::format support for \c sequence_tracker, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::gpio::sequence_tracker> : std::formatter<std::string_view> {
  /**
   * @brief Formats the tracker's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param tracker Tracker to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted tracker has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::sequence_tracker const& tracker, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(tracker), ctx);
  }
};

/**
 * @brief \c std::format support for \c event_debounce, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::gpio::event_debounce> : std::formatter<std::string_view> {
  /**
   * @brief Formats the debouncer's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param debounce Debouncer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted debouncer has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::event_debounce const& debounce, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(debounce), ctx);
  }
};

/**
 * @brief \c std::format support for \c callback_sink, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @tparam Handler Callable type of the sink.
 *
 * @pre None.
 * @post None.
 */
template <typename Handler>
struct std::formatter<nexenne::gpio::callback_sink<Handler>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sink Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted sink has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::callback_sink<Handler> const& sink, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(sink), ctx);
  }
};

/**
 * @brief \c std::format support for \c queue_sink, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @tparam N Ring slot count of the sink.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::formatter<nexenne::gpio::queue_sink<N>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param sink Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted sink has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::queue_sink<N> const& sink, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(sink), ctx);
  }
};

/**
 * @brief \c std::format support for a \c line handle, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @tparam Backend Backend type of the handle.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::gpio::gpio_backend Backend>
struct std::formatter<nexenne::gpio::line<Backend>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the handle's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param handle Handle to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted handle has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::line<Backend> const& handle, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(handle), ctx);
  }
};

/**
 * @brief \c std::format support for a \c chip handle, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @tparam Backend Backend type of the chip.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::gpio::gpio_backend Backend>
struct std::formatter<nexenne::gpio::chip<Backend>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the chip's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param handle Chip to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted chip has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::chip<Backend> const& handle, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(handle), ctx);
  }
};

/**
 * @brief \c std::format support for \c mock_chip, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @tparam Capacity Line and event capacity of the mock.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t Capacity>
struct std::formatter<nexenne::gpio::mock_chip<Capacity>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the mock's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param backend Mock to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted mock has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::mock_chip<Capacity> const& backend, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(backend), ctx);
  }
};

/**
 * @brief \c std::format support for \c chardev_chip, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::gpio::chardev_chip> : std::formatter<std::string_view> {
  /**
   * @brief Formats the backend's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param backend Backend to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted backend has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::chardev_chip const& backend, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(backend), ctx);
  }
};

/**
 * @brief \c std::format support for \c chardev_watcher, printing its \c to_string.
 *
 * Inherits the string formatter, so a spec applies to the whole text.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::gpio::chardev_watcher> : std::formatter<std::string_view> {
  /**
   * @brief Formats the watcher's \c to_string through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param watcher Watcher to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The formatted watcher has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::gpio::chardev_watcher const& watcher, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::gpio::to_string(watcher), ctx);
  }
};
