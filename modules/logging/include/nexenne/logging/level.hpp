#pragma once

/**
 * @file
 * @brief Severity levels for log messages.
 *
 * Lower numeric values are more verbose. A logger or sink configured at a given
 * level accepts that level and every more severe one and drops the rest;
 * \c level::off silences everything. The set mirrors the common syslog, spdlog,
 * and quill levels so user code ports between libraries without surprises.
 *
 * \c NEXENNE_LOG_MIN_LEVEL gates the \c LOG_* macros at compile time: levels
 * below it expand to nothing, costing zero runtime and zero binary size.
 */

#include <cstdint>
#include <string_view>

namespace nexenne::logging {

/**
 * @brief Severity levels for log messages, ordered by verbosity.
 *
 * Lower numeric values are more verbose. A logger or sink at a given level
 * accepts that level and every more severe one and drops the rest;
 * \c level::off silences everything.
 *
 * @pre None.
 * @post None.
 */
enum class level : std::uint8_t {
  trace = 0,     ///< Most verbose: function entry/exit, every iteration.
  debug = 1,     ///< Diagnostic information useful only when debugging.
  info = 2,      ///< Normal operation events worth recording.
  warn = 3,      ///< Something unusual but recoverable.
  error = 4,     ///< A failure that needs attention.
  critical = 5,  ///< System-level failure: data loss, security event.
  off = 6,       ///< Silence the logger or sink entirely.
};

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
  switch (l) {
    case level::trace:
      return "TRACE";
    case level::debug:
      return "DEBUG";
    case level::info:
      return "INFO ";
    case level::warn:
      return "WARN ";
    case level::error:
      return "ERROR";
    case level::critical:
      return "CRIT ";
    case level::off:
      return "OFF  ";
  }
  return "?????";
}

/**
 * @brief Single-character tag of a severity level, for compact log lines.
 *
 * @param l Level to name.
 *
 * @return The one-character level tag, or '?' for an invalid value.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto to_char(level const l) noexcept -> char {
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
      return ' ';
  }
  return '?';
}

}  // namespace nexenne::logging

/**
 * @def NEXENNE_LOG_MIN_LEVEL
 * @brief Compile-time minimum-level threshold for the \c LOG_* macros.
 *
 * Define it before including the logging headers to gate every \c LOG_* macro
 * at compile time: levels below the threshold expand to nothing, so they cost
 * zero runtime and zero binary size. The default is \c level::trace (everything
 * compiled in); set it to \c nexenne::logging::level::off to strip all logging
 * from a release build.
 */
#ifndef NEXENNE_LOG_MIN_LEVEL
#  define NEXENNE_LOG_MIN_LEVEL ::nexenne::logging::level::trace
#endif
