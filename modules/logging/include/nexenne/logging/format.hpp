#pragma once

/**
 * @file
 * @brief The three formatting layers for the nexenne::logging types and enums.
 *
 * Each type and enum gets a \c to_string, an \c operator<< and a
 * \c std::formatter, all printing the same text, so \c std::format("{}", x), a
 * stream insertion and \c to_string(x) agree. Kept out of the type headers by
 * the library rule; the sinks and \c stream_logger name a level through
 * \c detail::padded_name, which needs no \c format header.
 *
 * \c level prints its fixed five-character name (\c "INFO ", \c "CRIT ") so log
 * columns align; \c overflow_action and \c console_sink::stream print their
 * enumerator names. Every class prints a one-line summary of its observable
 * state, such as \c ring_sink(size=2, capacity=8, min_level=TRACE): a level
 * inside a summary is its unpadded \c to_token, and free text (a message, a
 * pattern, a format string) is quoted and escaped so the summary stays on one
 * line.
 *
 * \c record, \c config, \c format_string, \c file_writer and
 * \c basic_stream_logger are freestanding. The sinks, \c async_sink_config,
 * \c pattern_formatter, the managers and \c basic_logger exist only in a build
 * with the host sinks, so a build configured with
 * \c NEXENNE_LOGGING_HOST_SINKS=OFF gets the freestanding layers alone; the
 * \c esp_log_sink layers exist only where \c esp_log.h does.
 */

#include <cstddef>
#include <cstdio>
#include <format>
#include <ostream>
#include <string>
#include <string_view>

#include <nexenne/logging/config.hpp>
#include <nexenne/logging/format_string.hpp>
#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/logging/stream_logger.hpp>

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)
#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/json_sink.hpp>
#include <nexenne/logging/logger.hpp>
#include <nexenne/logging/manager.hpp>
#include <nexenne/logging/multi_sink.hpp>
#include <nexenne/logging/pattern_formatter.hpp>
#include <nexenne/logging/rotating_file_sink.hpp>
#include <nexenne/logging/sink.hpp>
#endif

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS) && __has_include(<esp_log.h>)
#include <nexenne/logging/esp_log_sink.hpp>
#endif

namespace nexenne::logging {

/**
 * @brief Fixed-width upper-case name of a severity level.
 *
 * The name is padded to five characters so messages align in a fixed column.
 *
 * @param l Level to name.
 *
 * @return The five-character level name, or "?????" for an invalid value.
 *
 * @pre None.
 * @post The result is exactly five characters long.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto to_string(level const l) noexcept -> std::string_view {
  return detail::padded_name(l);
}

/**
 * @brief Streams a severity level by its padded name.
 *
 * @param os Output stream.
 * @param l Level to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The five-character name of \p l has been written to \p os.
 */
inline auto operator<<(std::ostream& os, level const l) -> std::ostream& {
  return os << to_string(l);
}

/**
 * @brief One-line summary of a \c record: severity, logger, call site and message.
 *
 * Example: \c record(level=WARN, logger=net, location=app.cpp:42, message="down").
 * The call site is \c file:line and the message is quoted and escaped. The
 * timestamp and thread id are left out: they differ on every record, and a
 * sink's line format is where they belong.
 *
 * @param r Record to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(record const& r) -> std::string {
  return std::format(
    "record(level={}, logger={}, location={}:{}, message={:?})",
    to_token(r.severity),
    r.logger_name,
    r.location.file_name(),
    r.location.line(),
    r.message
  );
}

/**
 * @brief Streams a \c record via its \c to_string.
 *
 * @param os Output stream.
 * @param r Record to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p r has been written to \p os.
 */
inline auto operator<<(std::ostream& os, record const& r) -> std::ostream& {
  return os << to_string(r);
}

/**
 * @brief One-line summary of a \c config: its queue size and dispatch mode.
 *
 * Example: \c config(queue_size=1024, async=true).
 *
 * @tparam QueueSize Capacity of the async backend queue.
 * @tparam Async Whether the configuration dispatches asynchronously.
 * @param cfg Configuration to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <std::size_t QueueSize, bool Async>
[[nodiscard]] auto to_string(config<QueueSize, Async> const cfg) -> std::string {
  return std::format("config(queue_size={}, async={})", cfg.queue_size, cfg.async);
}

/**
 * @brief Streams a \c config via its \c to_string.
 *
 * @tparam QueueSize Capacity of the async backend queue.
 * @tparam Async Whether the configuration dispatches asynchronously.
 * @param os Output stream.
 * @param cfg Configuration to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p cfg has been written to \p os.
 */
template <std::size_t QueueSize, bool Async>
auto operator<<(std::ostream& os, config<QueueSize, Async> const cfg) -> std::ostream& {
  return os << to_string(cfg);
}

/**
 * @brief One-line summary of a \c format_string: its spec and captured call site.
 *
 * Example: \c format_string(fmt="x={}", location=app.cpp:12). The spec is
 * quoted and escaped.
 *
 * @tparam Args Argument types the format string expects.
 * @param fs Format string to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <typename... Args>
[[nodiscard]] auto to_string(format_string<Args...> const& fs) -> std::string {
  return std::format(
    "format_string(fmt={:?}, location={}:{})", fs.fmt.get(), fs.loc.file_name(), fs.loc.line()
  );
}

/**
 * @brief Streams a \c format_string via its \c to_string.
 *
 * @tparam Args Argument types the format string expects.
 * @param os Output stream.
 * @param fs Format string to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p fs has been written to \p os.
 */
template <typename... Args>
auto operator<<(std::ostream& os, format_string<Args...> const& fs) -> std::ostream& {
  return os << to_string(fs);
}

/**
 * @brief One-line summary of a \c file_writer: the stream it writes to.
 *
 * Names \c stdout and \c stderr, prints \c null for a disabled writer, and the
 * handle's address for any other stream, as in \c file_writer(0x5581a2c0).
 *
 * @param w Writer to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(file_writer const w) -> std::string {
  if (w.stream == nullptr) {
    return "file_writer(null)";
  }
  if (w.stream == stdout) {
    return "file_writer(stdout)";
  }
  if (w.stream == stderr) {
    return "file_writer(stderr)";
  }
  return std::format("file_writer({})", static_cast<void const*>(w.stream));
}

/**
 * @brief Streams a \c file_writer via its \c to_string.
 *
 * @param os Output stream.
 * @param w Writer to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p w has been written to \p os.
 */
inline auto operator<<(std::ostream& os, file_writer const w) -> std::ostream& {
  return os << to_string(w);
}

/**
 * @brief One-line summary of a \c basic_stream_logger.
 *
 * Example: \c basic_stream_logger(name=uart, min_level=INFO, buffer_size=256,
 * writer=file_writer(stdout)). The writer is printed only when \p Writer is
 * itself formattable; a plain callable is left out.
 *
 * @tparam Writer Output adapter type of the logger.
 * @tparam BufferSize Per-call stack buffer size in bytes.
 * @param lgr Logger to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <typename Writer, std::size_t BufferSize>
[[nodiscard]] auto to_string(basic_stream_logger<Writer, BufferSize> const& lgr) -> std::string {
  auto text{std::format(
    "basic_stream_logger(name={}, min_level={}, buffer_size={}",
    lgr.name(),
    to_token(lgr.min_level()),
    BufferSize
  )};
  if constexpr (std::formattable<Writer, char>) {
    text += std::format(", writer={}", lgr.writer());
  }
  text += ')';
  return text;
}

/**
 * @brief Streams a \c basic_stream_logger via its \c to_string.
 *
 * @tparam Writer Output adapter type of the logger.
 * @tparam BufferSize Per-call stack buffer size in bytes.
 * @param os Output stream.
 * @param lgr Logger to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p lgr has been written to \p os.
 */
template <typename Writer, std::size_t BufferSize>
auto operator<<(std::ostream& os, basic_stream_logger<Writer, BufferSize> const& lgr)
  -> std::ostream& {
  return os << to_string(lgr);
}

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)

/**
 * @brief Name of an async-sink overflow action.
 *
 * @param action Action to name.
 *
 * @return The enumerator's name, or \c "?" for a value outside the enum.
 *
 * @pre None.
 * @post The returned view refers to a static string.
 */
[[nodiscard]] constexpr auto to_string(overflow_action const action) noexcept -> std::string_view {
  switch (action) {
    case overflow_action::block:
      return "block";
    case overflow_action::drop_oldest:
      return "drop_oldest";
    case overflow_action::drop_newest:
      return "drop_newest";
  }
  return "?";
}

/**
 * @brief Streams an async-sink overflow action by its name.
 *
 * @param os Output stream.
 * @param action Action to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p action has been written to \p os.
 */
inline auto operator<<(std::ostream& os, overflow_action const action) -> std::ostream& {
  return os << to_string(action);
}

/**
 * @brief Name of a console-sink stream-routing policy.
 *
 * @param s Policy to name.
 *
 * @return The enumerator's name, or \c "?" for a value outside the enum.
 *
 * @pre None.
 * @post The returned view refers to a static string.
 */
[[nodiscard]] constexpr auto to_string(console_sink::stream const s) noexcept -> std::string_view {
  switch (s) {
    case console_sink::stream::stdout_only:
      return "stdout_only";
    case console_sink::stream::stderr_only:
      return "stderr_only";
    case console_sink::stream::auto_split:
      return "auto_split";
  }
  return "?";
}

/**
 * @brief Streams a console-sink stream-routing policy by its name.
 *
 * @param os Output stream.
 * @param s Policy to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The name of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, console_sink::stream const s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of an \c async_sink_config: queue bound and overflow policy.
 *
 * Example: \c async_sink_config(queue_size_limit=1024, on_overflow=block).
 *
 * @param cfg Configuration to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(async_sink_config const cfg) -> std::string {
  return std::format(
    "async_sink_config(queue_size_limit={}, on_overflow={})",
    cfg.queue_size_limit,
    to_string(cfg.on_overflow)
  );
}

/**
 * @brief Streams an \c async_sink_config via its \c to_string.
 *
 * @param os Output stream.
 * @param cfg Configuration to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p cfg has been written to \p os.
 */
inline auto operator<<(std::ostream& os, async_sink_config const cfg) -> std::ostream& {
  return os << to_string(cfg);
}

/**
 * @brief One-line summary of a \c sink seen through its base: its minimum level.
 *
 * Example: \c sink(min_level=TRACE). A bundled sink has its own overload that
 * adds its target; this one covers a sink reached by a base reference and any
 * user-defined sink.
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(sink const& s) -> std::string {
  return std::format("sink(min_level={})", to_token(s.min_level()));
}

/**
 * @brief Streams a \c sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c console_sink: routing policy and minimum level.
 *
 * Example: \c console_sink(routing=auto_split, min_level=TRACE).
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(console_sink const& s) -> std::string {
  return std::format(
    "console_sink(routing={}, min_level={})", to_string(s.routing()), to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c console_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, console_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c file_sink: open state and minimum level.
 *
 * Example: \c file_sink(open, min_level=TRACE). The sink keeps no copy of its
 * path, so the summary has none.
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(file_sink const& s) -> std::string {
  return std::format(
    "file_sink({}, min_level={})", s.is_open() ? "open" : "closed", to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c file_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, file_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c ring_sink: buffered lines, capacity and minimum level.
 *
 * Example: \c ring_sink(size=2, capacity=8, min_level=TRACE). Reading the size
 * briefly takes the sink's mutex, as \c size does.
 *
 * @tparam N Number of lines the sink retains.
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <std::size_t N>
[[nodiscard]] auto to_string(ring_sink<N> const& s) -> std::string {
  return std::format(
    "ring_sink(size={}, capacity={}, min_level={})", s.size(), N, to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c ring_sink via its \c to_string.
 *
 * @tparam N Number of lines the sink retains.
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
template <std::size_t N>
auto operator<<(std::ostream& os, ring_sink<N> const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of an \c async_sink: queue bound, overflow policy and level.
 *
 * Example: \c async_sink(queue_size_limit=1024, on_overflow=block,
 * min_level=TRACE). The wrapped sink is reached only through the base \c sink
 * interface, so it is not described.
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(async_sink const& s) -> std::string {
  return std::format(
    "async_sink(queue_size_limit={}, on_overflow={}, min_level={})",
    s.configuration().queue_size_limit,
    to_string(s.configuration().on_overflow),
    to_token(s.min_level())
  );
}

/**
 * @brief Streams an \c async_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, async_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c json_sink: open state and minimum level.
 *
 * Example: \c json_sink(open, min_level=TRACE).
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(json_sink const& s) -> std::string {
  return std::format(
    "json_sink({}, min_level={})", s.is_open() ? "open" : "closed", to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c json_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, json_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c multi_sink: child count and minimum level.
 *
 * Example: \c multi_sink(children=3, min_level=TRACE).
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(multi_sink const& s) -> std::string {
  return std::format(
    "multi_sink(children={}, min_level={})", s.child_count(), to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c multi_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, multi_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c rotating_file_sink: path, state, limits and level.
 *
 * Example: \c rotating_file_sink(path=app.log, open, size=120, max_bytes=4096,
 * max_files=3, min_level=TRACE), where \c size is the byte count of the active
 * file.
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(rotating_file_sink const& s) -> std::string {
  return std::format(
    "rotating_file_sink(path={}, {}, size={}, max_bytes={}, max_files={}, min_level={})",
    s.base_path(),
    s.is_open() ? "open" : "closed",
    s.current_size(),
    s.max_bytes(),
    s.max_files(),
    to_token(s.min_level())
  );
}

/**
 * @brief Streams a \c rotating_file_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, rotating_file_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

/**
 * @brief One-line summary of a \c pattern_formatter: its pattern, quoted and escaped.
 *
 * A default formatter prints \c pattern_formatter(pattern=...) with
 * \c default_pattern inside the quotes; a tab or newline in the pattern prints
 * as its escape, so the summary stays on one line.
 *
 * @param pf Formatter to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(pattern_formatter const& pf) -> std::string {
  return std::format("pattern_formatter(pattern={:?})", pf.pattern());
}

/**
 * @brief Streams a \c pattern_formatter via its \c to_string.
 *
 * @param os Output stream.
 * @param pf Formatter to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p pf has been written to \p os.
 */
inline auto operator<<(std::ostream& os, pattern_formatter const& pf) -> std::ostream& {
  return os << to_string(pf);
}

/**
 * @brief One-line summary of a \c basic_async_manager: queue size, sinks and drops.
 *
 * Example: \c basic_async_manager(queue_size=1024, sinks=2, dropped=0). The
 * manager tracks sinks, not loggers, so the sink count is what it reports.
 *
 * @tparam Config Configuration policy of the manager.
 * @param mgr Manager to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <config_like Config>
[[nodiscard]] auto to_string(basic_async_manager<Config> const& mgr) -> std::string {
  return std::format(
    "basic_async_manager(queue_size={}, sinks={}, dropped={})",
    basic_async_manager<Config>::queue_size,
    mgr.sink_count(),
    mgr.dropped_count()
  );
}

/**
 * @brief Streams a \c basic_async_manager via its \c to_string.
 *
 * @tparam Config Configuration policy of the manager.
 * @param os Output stream.
 * @param mgr Manager to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p mgr has been written to \p os.
 */
template <config_like Config>
auto operator<<(std::ostream& os, basic_async_manager<Config> const& mgr) -> std::ostream& {
  return os << to_string(mgr);
}

/**
 * @brief One-line summary of a \c basic_sync_manager: its sink count.
 *
 * Example: \c basic_sync_manager(sinks=1). A sync manager has no queue and never
 * drops, so the sink count is its whole observable state.
 *
 * @tparam Config Configuration policy of the manager.
 * @param mgr Manager to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <config_like Config>
[[nodiscard]] auto to_string(basic_sync_manager<Config> const& mgr) -> std::string {
  return std::format("basic_sync_manager(sinks={})", mgr.sink_count());
}

/**
 * @brief Streams a \c basic_sync_manager via its \c to_string.
 *
 * @tparam Config Configuration policy of the manager.
 * @param os Output stream.
 * @param mgr Manager to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p mgr has been written to \p os.
 */
template <config_like Config>
auto operator<<(std::ostream& os, basic_sync_manager<Config> const& mgr) -> std::ostream& {
  return os << to_string(mgr);
}

/**
 * @brief One-line summary of a \c basic_logger: its name and minimum level.
 *
 * Example: \c basic_logger(name=net, min_level=INFO). The logger owns no sinks
 * (its manager does), and printing never touches the manager, so formatting a
 * logger cannot start an async backend thread.
 *
 * @tparam Config Configuration policy of the logger.
 * @param lgr Logger to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
template <config_like Config>
[[nodiscard]] auto to_string(basic_logger<Config> const& lgr) -> std::string {
  return std::format("basic_logger(name={}, min_level={})", lgr.name(), to_token(lgr.min_level()));
}

/**
 * @brief Streams a \c basic_logger via its \c to_string.
 *
 * @tparam Config Configuration policy of the logger.
 * @param os Output stream.
 * @param lgr Logger to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p lgr has been written to \p os.
 */
template <config_like Config>
auto operator<<(std::ostream& os, basic_logger<Config> const& lgr) -> std::ostream& {
  return os << to_string(lgr);
}

#endif

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS) && __has_include(<esp_log.h>)

/**
 * @brief One-line summary of an \c esp_log_sink: its minimum level.
 *
 * Example: \c esp_log_sink(min_level=TRACE). The destination is always
 * ESP-IDF's log stream, so the level is the only state to show.
 *
 * @param s Sink to summarise.
 *
 * @return The summary string.
 *
 * @pre None.
 * @post None.
 * @throws std::bad_alloc if the result string cannot be allocated.
 */
[[nodiscard]] inline auto to_string(esp_log_sink const& s) -> std::string {
  return std::format("esp_log_sink(min_level={})", to_token(s.min_level()));
}

/**
 * @brief Streams an \c esp_log_sink via its \c to_string.
 *
 * @param os Output stream.
 * @param s Sink to print.
 *
 * @return Reference to \p os.
 *
 * @pre None.
 * @post The summary of \p s has been written to \p os.
 */
inline auto operator<<(std::ostream& os, esp_log_sink const& s) -> std::ostream& {
  return os << to_string(s);
}

#endif

}  // namespace nexenne::logging

/**
 * @brief \c std::format support for \c level: prints its padded name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::level> : std::formatter<std::string_view> {
  /**
   * @brief Formats the level's padded name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param l Level to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::level const l, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(l), ctx);
  }
};

/**
 * @brief \c std::format support for \c record: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::record> : std::formatter<std::string_view> {
  /**
   * @brief Formats the record's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param r Record to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::record const& r, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(r), ctx);
  }
};

/**
 * @brief \c std::format support for \c config: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam QueueSize Capacity of the async backend queue.
 * @tparam Async Whether the configuration dispatches asynchronously.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t QueueSize, bool Async>
struct std::formatter<nexenne::logging::config<QueueSize, Async>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the configuration's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param cfg Configuration to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::config<QueueSize, Async> const cfg, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(cfg), ctx);
  }
};

/**
 * @brief \c std::format support for \c format_string: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam Args Argument types the format string expects.
 *
 * @pre None.
 * @post None.
 */
template <typename... Args>
struct std::formatter<nexenne::logging::format_string<Args...>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the format string's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param fs Format string to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::format_string<Args...> const& fs, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(fs), ctx);
  }
};

/**
 * @brief \c std::format support for \c file_writer: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::file_writer> : std::formatter<std::string_view> {
  /**
   * @brief Formats the writer's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param w Writer to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::file_writer const w, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(w), ctx);
  }
};

/**
 * @brief \c std::format support for \c basic_stream_logger: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam Writer Output adapter type of the logger.
 * @tparam BufferSize Per-call stack buffer size in bytes.
 *
 * @pre None.
 * @post None.
 */
template <typename Writer, std::size_t BufferSize>
struct std::formatter<nexenne::logging::basic_stream_logger<Writer, BufferSize>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the logger's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param lgr Logger to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(
    nexenne::logging::basic_stream_logger<Writer, BufferSize> const& lgr, FormatContext& ctx
  ) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(lgr), ctx);
  }
};

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)

/**
 * @brief \c std::format support for \c overflow_action: prints its name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::overflow_action> : std::formatter<std::string_view> {
  /**
   * @brief Formats the action's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param action Action to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::overflow_action const action, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(action), ctx);
  }
};

/**
 * @brief \c std::format support for \c console_sink::stream: prints its name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::console_sink::stream> : std::formatter<std::string_view> {
  /**
   * @brief Formats the policy's name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Policy to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::console_sink::stream const s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c async_sink_config: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::async_sink_config> : std::formatter<std::string_view> {
  /**
   * @brief Formats the configuration's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param cfg Configuration to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::async_sink_config const cfg, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(cfg), ctx);
  }
};

/**
 * @brief \c std::format support for \c sink: prints its base-class summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c console_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::console_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::console_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c file_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::file_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::file_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c ring_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam N Number of lines the sink retains.
 *
 * @pre None.
 * @post None.
 */
template <std::size_t N>
struct std::formatter<nexenne::logging::ring_sink<N>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::ring_sink<N> const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c async_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::async_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::async_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c json_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::json_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::json_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c multi_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::multi_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::multi_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c rotating_file_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::rotating_file_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::rotating_file_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

/**
 * @brief \c std::format support for \c pattern_formatter: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::pattern_formatter> : std::formatter<std::string_view> {
  /**
   * @brief Formats the formatter's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param pf Formatter to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::pattern_formatter const& pf, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(pf), ctx);
  }
};

/**
 * @brief \c std::format support for \c basic_async_manager: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam Config Configuration policy of the manager.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::logging::config_like Config>
struct std::formatter<nexenne::logging::basic_async_manager<Config>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the manager's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param mgr Manager to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::basic_async_manager<Config> const& mgr, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(mgr), ctx);
  }
};

/**
 * @brief \c std::format support for \c basic_sync_manager: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam Config Configuration policy of the manager.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::logging::config_like Config>
struct std::formatter<nexenne::logging::basic_sync_manager<Config>>
    : std::formatter<std::string_view> {
  /**
   * @brief Formats the manager's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param mgr Manager to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::basic_sync_manager<Config> const& mgr, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(mgr), ctx);
  }
};

/**
 * @brief \c std::format support for \c basic_logger: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @tparam Config Configuration policy of the logger.
 *
 * @pre None.
 * @post None.
 */
template <nexenne::logging::config_like Config>
struct std::formatter<nexenne::logging::basic_logger<Config>> : std::formatter<std::string_view> {
  /**
   * @brief Formats the logger's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param lgr Logger to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::basic_logger<Config> const& lgr, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(lgr), ctx);
  }
};

#endif

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS) && __has_include(<esp_log.h>)

/**
 * @brief \c std::format support for \c esp_log_sink: prints its one-line summary.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the summary.
 *
 * @pre None.
 * @post None.
 */
template <>
struct std::formatter<nexenne::logging::esp_log_sink> : std::formatter<std::string_view> {
  /**
   * @brief Formats the sink's summary through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param s Sink to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The summary has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::logging::esp_log_sink const& s, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::logging::to_string(s), ctx);
  }
};

#endif
