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
 *   - \c %%  literal percent sign
 *   - any other character: emitted verbatim (the percent is kept)
 *
 * The default pattern \c "[%T] [%L] [%n] %m (%f:%#)" mimics a typical console
 * log line. Output is appended to a caller-supplied \c std::string so the
 * formatter never allocates per character; \c std::format and string appends are
 * bound to the output buffer.
 *
 * The level names here are deliberately unpadded and the long critical spelling
 * is used, so they differ from \c to_string and \c to_char in \c level.hpp,
 * which pad to a fixed column for aligned default-formatted lines. Hence the
 * local helpers rather than reuse.
 */

#include <chrono>
#include <cstddef>
#include <ctime>
#include <format>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>

#include <nexenne/logging/level.hpp>
#include <nexenne/logging/record.hpp>

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

  [[nodiscard]] static auto basename_of(std::string_view const path) noexcept -> std::string_view {
    auto const pos{path.find_last_of("/\\")};
    return pos == std::string_view::npos ? path : path.substr(pos + 1);
  }

  [[nodiscard]] static auto level_short(level const l) noexcept -> char {
    switch (l) {
      case level::trace:
        return 'T';
      case level::debug:
        return 'D';
      case level::info:
        return 'I';
      case level::warn:
        return 'W';
      case level::error:
        return 'E';
      case level::critical:
        return 'C';
      case level::off:
        return '-';
    }
    return '?';
  }

  [[nodiscard]] static auto level_name(level const l) noexcept -> std::string_view {
    switch (l) {
      case level::trace:
        return "TRACE";
      case level::debug:
        return "DEBUG";
      case level::info:
        return "INFO";
      case level::warn:
        return "WARN";
      case level::error:
        return "ERROR";
      case level::critical:
        return "CRITICAL";
      case level::off:
        return "OFF";
    }
    return "UNKNOWN";
  }

  static auto append_time(
    std::string& out, std::chrono::system_clock::time_point const t, bool const include_date
  ) -> void {
    auto const tt{std::chrono::system_clock::to_time_t(t)};
    std::tm tm{};
#ifdef _WIN32
    static_cast<void>(gmtime_s(&tm, &tt));
#else
    static_cast<void>(gmtime_r(&tt, &tm));
#endif
    auto const ms{
      std::chrono::duration_cast<std::chrono::milliseconds>(t.time_since_epoch()).count() % 1000
    };
    if (include_date) {
      std::format_to(
        std::back_inserter(out),
        "{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}Z",
        tm.tm_year + 1900,
        tm.tm_mon + 1,
        tm.tm_mday,
        tm.tm_hour,
        tm.tm_min,
        tm.tm_sec,
        ms
      );
    } else {
      std::format_to(
        std::back_inserter(out), "{:02}:{:02}:{:02}.{:03}", tm.tm_hour, tm.tm_min, tm.tm_sec, ms
      );
    }
  }

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
  explicit pattern_formatter(std::string pattern) : m_pattern{std::move(pattern)} {}

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
  auto set_pattern(std::string pattern) noexcept -> void {
    m_pattern = std::move(pattern);
  }

  /**
   * @brief Current pattern string.
   *
   * @return A const reference to the active pattern.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto pattern() const noexcept -> std::string const& {
    return m_pattern;
  }

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
  auto format(record const& r, std::string& out) const -> void {
    for (std::size_t i{0}; i < m_pattern.size(); ++i) {
      auto const c{m_pattern[i]};
      if (c != '%' || i + 1 >= m_pattern.size()) {
        out.push_back(c);
        continue;
      }
      ++i;
      switch (m_pattern[i]) {
        case 't':
          append_time(out, r.timestamp, true);
          break;
        case 'T':
          append_time(out, r.timestamp, false);
          break;
        case 'l':
          out += level_name(r.severity);
          break;
        case 'L':
          out.push_back(level_short(r.severity));
          break;
        case 'n':
          out += r.logger_name;
          break;
        case 'm':
          out += r.message;
          break;
        case 'f': {
          auto const* const file{r.location.file_name()};
          out += file != nullptr ? basename_of(file) : std::string_view{"?"};
          break;
        }
        case '#':
          std::format_to(std::back_inserter(out), "{}", r.location.line());
          break;
        case 's': {
          auto const* const fn{r.location.function_name()};
          out += fn != nullptr ? std::string_view{fn} : std::string_view{"?"};
          break;
        }
        case '%':
          out.push_back('%');
          break;
        default:
          out.push_back('%');
          out.push_back(m_pattern[i]);
          break;
      }
    }
  }

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
  [[nodiscard]] auto format(record const& r) const -> std::string {
    auto out{std::string{}};
    out.reserve(64 + r.message.size());
    format(r, out);
    return out;
  }
};

}  // namespace nexenne::logging
