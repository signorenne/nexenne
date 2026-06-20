#pragma once

/**
 * @file
 * @brief Heap-free synchronous logger writing through a compile-time \c Writer.
 *
 * \c stream_logger is the stripped-down sibling of \c basic_logger for
 * heap-averse targets (an MCU, a Cortex-M, an ESP32): the same call surface
 * (\c info / \c warn / ...) and the same \c format_string source-location
 * capture, but with no \c manager, no backend thread, no queue, no mutex, and
 * no \c std::string allocation. Each call formats with \c std::format_to_n into
 * a stack buffer of compile-time size \p BufferSize; an overlong message is
 * truncated at the buffer boundary and marked with "...".
 *
 * The destination is a compile-time \c Writer: a callable handed the formatted
 * bytes (a \c std::span). It defaults to \c file_writer (a \c FILE*), but a
 * target can plug in a UART, a SEGGER RTT channel, or any sink with no \c FILE*
 * and zero call overhead (the writer inlines). Trade-offs: every call is
 * synchronous; there are no structured fields and no fan-out; thread safety is
 * whatever the writer provides. For any of those, reach for \c basic_logger.
 */

#include <array>
#include <atomic>
#include <concepts>
#include <cstddef>
#include <cstdio>
#include <format>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>

#include <nexenne/logging/format_string.hpp>
#include <nexenne/logging/level.hpp>

namespace nexenne::logging {

/**
 * @brief Default \c stream_logger output adapter: writes the bytes to a \c FILE*.
 *
 * A trivial, copyable writer. Guards a null handle, so a logger configured with
 * a null \c FILE* simply emits nothing rather than dereferencing it.
 *
 * @pre None.
 * @post None.
 */
struct file_writer {
  std::FILE* stream{stdout};  ///< Destination stream; null disables output.

  /**
   * @brief Writes the formatted bytes to the stream.
   *
   * @param bytes The formatted log line.
   *
   * @pre None.
   * @post The bytes are handed to the stream when it is non-null.
   */
  auto operator()(std::span<char const> const bytes) const noexcept -> void {
    if (stream != nullptr) {
      static_cast<void>(std::fwrite(bytes.data(), 1, bytes.size(), stream));
    }
  }
};

/**
 * @brief Heap-free synchronous logger writing through a \c Writer and a stack buffer.
 *
 * Per-call stack buffer of \p BufferSize bytes; an overlong message is truncated
 * and marked with "...". No heap allocation, no queue, no mutex.
 *
 * @tparam Writer Callable invoked with the formatted bytes (\c std::span of
 *         \c char); defaults to \c file_writer. Resolved at compile time, so the
 *         write inlines with no indirection.
 * @tparam BufferSize Per-call stack buffer size in bytes (default 256); messages
 *         longer than this are truncated.
 *
 * @pre \p BufferSize >= 32 and \p Writer is invocable with \c std::span<char const>.
 * @post None.
 */
template <typename Writer = file_writer, std::size_t BufferSize = 256>
  requires(BufferSize >= 32) && std::invocable<Writer&, std::span<char const>>
class basic_stream_logger {
public:
  using size_type = std::size_t;
  static constexpr size_type buffer_size = BufferSize;

private:
  // Strips leading directories from a source path so a long build path does not
  // blow the buffer. Like basename(3) but constexpr-friendly and noexcept.
  [[nodiscard]] static constexpr auto short_filename(char const* path) noexcept -> char const* {
    if (path == nullptr) {
      return "?";
    }
    auto const* last_slash{path};
    for (auto const* p{path}; *p != '\0'; ++p) {
      if (*p == '/' || *p == '\\') {
        last_slash = p + 1;
      }
    }
    return last_slash;
  }

  std::string_view m_name;
  Writer m_writer;
  std::atomic<level> m_min_level;

public:
  /**
   * @brief Constructs a stream logger with the given output writer.
   *
   * @param name Stable string view; the caller keeps it alive for the logger's
   *        lifetime. A string literal is the typical, cheapest choice.
   * @param min Minimum runtime level; below-threshold calls early-out before
   *        formatting.
   * @param writer Output writer; defaults to a default-constructed \c Writer
   *        (for \c file_writer, that is \c stdout).
   *
   * @pre None.
   * @post \c name() is \p name, \c min_level() is \p min, output goes through
   *       \p writer.
   */
  explicit basic_stream_logger(
    std::string_view const name, level const min = level::trace, Writer writer = Writer{}
  ) noexcept(std::is_nothrow_move_constructible_v<Writer>)
      : m_name{name}, m_writer{std::move(writer)}, m_min_level{min} {}

  basic_stream_logger(basic_stream_logger const&) = delete;
  auto operator=(basic_stream_logger const&) -> basic_stream_logger& = delete;

  // Move operations are intentionally not declared: the atomic m_min_level is
  // not movable, and the logger is meant to be constructed where it lives.

  /**
   * @brief Logger name.
   *
   * @return A view of the name, valid for the logger's lifetime.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto name() const noexcept -> std::string_view {
    return m_name;
  }

  /**
   * @brief Sets the runtime minimum severity filter.
   *
   * @param l New minimum level; calls below it are dropped cheaply.
   *
   * @pre None.
   * @post \c min_level() returns \p l.
   */
  auto set_min_level(level const l) noexcept -> void {
    m_min_level.store(l, std::memory_order_relaxed);
  }

  /**
   * @brief Current runtime minimum severity filter.
   *
   * @return The level below which log calls are dropped.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto min_level() const noexcept -> level {
    return m_min_level.load(std::memory_order_relaxed);
  }

  /**
   * @brief Whether a call at level \p l would be emitted.
   *
   * @param l Candidate severity.
   *
   * @return \c true if \p l is at or above \c min_level().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto enabled(level const l) const noexcept -> bool {
    return l >= min_level();
  }

  /**
   * @brief Mutable access to the output writer, e.g. to retarget it.
   *
   * @return A reference to the writer (for \c file_writer, set \c .stream).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto writer() noexcept -> Writer& {
    return m_writer;
  }

  /**
   * @brief Read-only access to the output writer.
   *
   * @return A const reference to the writer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto writer() const noexcept -> Writer const& {
    return m_writer;
  }

  /**
   * @brief Emits a record at the explicit level \p lvl.
   *
   * Drops the call when \p lvl is below \c min_level(); otherwise formats into
   * the stack buffer and hands the bytes to the writer in one call. Overlong
   * messages are truncated and marked with "...". Allocation-free, so
   * \c noexcept (the writer is required to be \c noexcept).
   *
   * @tparam Args Argument types matching \p fmt.
   * @param lvl Severity of the record.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is handed to the writer when \c enabled(lvl).
   *
   * @complexity \c O(line length).
   */
  template <typename... Args>
  auto log(
    level const lvl, format_string<std::type_identity_t<Args>...> const fmt, Args&&... args
  ) noexcept -> void {
    if (!enabled(lvl)) {
      return;
    }

    // Stack buffer holding prefix plus user message; -1 leaves room for a
    // terminating newline.
    auto buf{std::array<char, BufferSize>{}};
    auto out_it{buf.data()};
    auto const end_it{buf.data() + buf.size() - 1};

    // Prefix "[LEVEL] [name] file:line -- ". format_to_n never allocates: it
    // writes up to n chars to the iterator and stops.
    auto prefix_result{std::format_to_n(
      out_it,
      static_cast<std::ptrdiff_t>(end_it - out_it),
      "[{}] [{}] {}:{} -- ",
      to_string(lvl),
      m_name,
      short_filename(fmt.loc.file_name()),
      fmt.loc.line()
    )};
    out_it = prefix_result.out;
    if (out_it > end_it) {
      out_it = end_it;
    }

    // Append the formatted user message into whatever room remains.
    auto msg_result{std::format_to_n(
      out_it, static_cast<std::ptrdiff_t>(end_it - out_it), fmt.fmt, std::forward<Args>(args)...
    )};
    out_it = msg_result.out;
    if (out_it > end_it) {
      out_it = end_it;
    }

    // Mark truncation with an ellipsis when format_to_n wanted more room than
    // was left. out_it is pinned at end_it on truncation, so the last three
    // written bytes are clobbered in place.
    auto const truncated{msg_result.size > (end_it - prefix_result.out)};
    if (truncated && (out_it - buf.data()) >= 3) {
      out_it[-3] = '.';
      out_it[-2] = '.';
      out_it[-1] = '.';
    }

    if (out_it < end_it) {
      *out_it = '\n';
      out_it += 1;
    }
    m_writer(std::span<char const>{buf.data(), static_cast<std::size_t>(out_it - buf.data())});
  }

  /**
   * @brief Emits a record at \c level::trace.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::trace).
   */
  template <typename... Args>
  auto
  trace(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept -> void {
    log(level::trace, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::debug.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::debug).
   */
  template <typename... Args>
  auto
  debug(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept -> void {
    log(level::debug, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::info.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::info).
   */
  template <typename... Args>
  auto
  info(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept -> void {
    log(level::info, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::warn.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::warn).
   */
  template <typename... Args>
  auto
  warn(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept -> void {
    log(level::warn, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::error.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::error).
   */
  template <typename... Args>
  auto
  error(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept -> void {
    log(level::error, fmt, std::forward<Args>(args)...);
  }

  /**
   * @brief Emits a record at \c level::critical.
   *
   * @tparam Args Argument types matching \p fmt.
   * @param fmt Format string with captured source location.
   * @param args Arguments to format.
   *
   * @pre None.
   * @post The line is written when \c enabled(level::critical).
   */
  template <typename... Args>
  auto critical(format_string<std::type_identity_t<Args>...> const fmt, Args&&... args) noexcept
    -> void {
    log(level::critical, fmt, std::forward<Args>(args)...);
  }
};

/**
 * @brief Default stream logger: writes to a \c FILE* with a 256-byte buffer.
 *
 * @pre None.
 * @post None.
 */
using stream_logger = basic_stream_logger<file_writer, 256>;

}  // namespace nexenne::logging
