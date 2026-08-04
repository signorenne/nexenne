#pragma once

/**
 * @file
 * @brief Pattern-based record-to-string formatter.
 *
 * Renders a \c record into a string using a pattern of \c % tokens
 * (printf-style spelling, but the substitution is done in C++: no \c vprintf,
 * no format-string ambiguity). Supported tokens:
 *
 *   - \c %t  timestamp (ISO 8601 UTC: \c YYYY-MM-DDTHH:MM:SS.fffZ)
 *   - \c %T  time only (\c HH:MM:SS.fff)
 *   - \c %l  full level name (TRACE, DEBUG, INFO, WARN, ERROR, CRITICAL, OFF)
 *   - \c %L  single-char level (T, D, I, W, E, C, -)
 *   - \c %n  logger name
 *   - \c %m  message body
 *   - \c %f  short source file (basename only)
 *   - \c %#  source line number
 *   - \c %s  source function name
 *   - \c %o  producing thread id
 *   - \c %%  literal percent sign
 *   - any other character: emitted verbatim (the percent is kept)
 *
 * The default pattern \c "[%T] [%L] [%n] %m (%f:%#)" mimics a typical console
 * log line. Output is appended to a caller-supplied \c std::string so the
 * formatter never allocates per character; \c std::format and string appends are
 * bound to the output buffer.
 *
 * The full level name (\c %l) comes from \c to_token in \c level.hpp, the same
 * unpadded vocabulary \c json_sink emits, so a severity spells identically across
 * both structured emitters. The single-char tag (\c %L) uses a local helper
 * because it renders \c off as '-' rather than the space \c to_char yields.
 */

#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::logging {

/**
 * @brief Pattern-based record-to-string formatter.
 *
 * Renders a \c record into a string using a pattern of \c % tokens. The pattern
 * string is mutable at runtime; the default pattern emits
 * \c "[HH:MM:SS.mmm] [L] [logger] message (file:line)".
 *
 * @pre None.
 * @post A default-constructed formatter uses \c default_pattern.
 */
class pattern_formatter {
public:
  /// Default pattern: [time] [level-char] [logger] message (file:line).
  static constexpr std::string_view default_pattern{"[%T] [%L] [%n] %m (%f:%#)"};

private:
  std::string m_pattern{default_pattern};

  /**
   * @brief Returns the basename of \p path, the part after the last separator.
   *
   * @param path Source path to strip.
   *
   * @return The trailing component, or \p path itself when it has no separator.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(|path|).
   */
  [[nodiscard]] static auto basename_of(std::string_view path) noexcept -> std::string_view;

  /**
   * @brief Single-character tag of a severity for the \c %L token.
   *
   * Differs from \c to_char in that it renders \c level::off as '-' rather than
   * the space \c to_char yields.
   *
   * @param l Severity to tag.
   *
   * @return The one-character tag, or '?' for an invalid value.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] static auto level_short(level l) noexcept -> char;

  /**
   * @brief Appends \p t to \p out as UTC, with or without the calendar date.
   *
   * Emits \c YYYY-MM-DDTHH:MM:SS.fffZ when \p include_date is set, otherwise
   * \c HH:MM:SS.fff. The seconds are floored so the millisecond remainder stays
   * non-negative for pre-epoch timestamps.
   *
   * @param out String to append the rendered time to.
   * @param t Time point to render.
   * @param include_date Whether to prefix the calendar date.
   *
   * @pre None.
   * @post \p out has the rendered time appended.
   * @throws std::bad_alloc if appending to \p out fails to allocate.
   *
   * @complexity \c O(1).
   */
  static auto
  append_time(std::string& out, std::chrono::system_clock::time_point t, bool include_date) -> void;

public:
  /**
   * @brief Default constructor; uses \c default_pattern.
   *
   * @pre None.
   * @post \c pattern() returns \c default_pattern.
   */
  pattern_formatter() = default;

  /**
   * @brief Constructs with a custom pattern string.
   *
   * Not \c noexcept: \p pattern is moved into the owned field, but the value
   * argument's allocation happens at the call site and \c bad_alloc is allowed
   * to propagate per the module's error policy.
   *
   * @param pattern Format pattern to use; moved in.
   *
   * @pre None.
   * @post \c pattern() returns \p pattern.
   */
  explicit pattern_formatter(std::string pattern);

  /**
   * @brief Replaces the active pattern.
   *
   * @param pattern New pattern string; moved in.
   *
   * @pre None.
   * @post \c pattern() returns \p pattern.
   *
   * @complexity \c O(1) amortised (a \c std::string move).
   */
  auto set_pattern(std::string pattern) noexcept -> void;

  /**
   * @brief Current pattern string.
   *
   * @return A const reference to the active pattern.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto pattern() const noexcept -> std::string const&;

  /**
   * @brief Renders \p r by appending to \p out.
   *
   * Pattern tokens are expanded and the result is appended to any existing
   * content in \p out; \p out is not cleared first. A null \c file_name or
   * \c function_name on the record's source location renders as "?".
   *
   * @param r Record to render.
   * @param out String to append the formatted output to.
   *
   * @pre None.
   * @post \p out has been extended with the formatted record.
   * @throws std::bad_alloc if appending to \p out fails to allocate.
   *
   * @complexity \c O(|pattern| + |output|).
   */
  auto format(record const& r, std::string& out) const -> void;

  /**
   * @brief Renders \p r and returns the result as a new string.
   *
   * @param r Record to render.
   *
   * @return The formatted line.
   *
   * @pre None.
   * @post None.
   * @throws std::bad_alloc if the result string cannot be allocated.
   *
   * @complexity \c O(|pattern| + |output|).
   */
  [[nodiscard]] auto format(record const& r) const -> std::string;
};

}  // namespace nexenne::logging
