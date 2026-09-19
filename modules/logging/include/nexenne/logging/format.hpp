#pragma once

/**
 * @file
 * @brief The three formatting layers for the nexenne::logging enums.
 *
 * Each enum gets a \c to_string, an \c operator<< and a \c std::formatter, all
 * printing the same text, so \c std::format("{}", lvl), a stream insertion and
 * \c to_string(lvl) agree. Kept out of the type headers by the library rule; the
 * sinks and \c stream_logger name a level through \c detail::padded_name, which
 * needs no \c format header.
 *
 * \c level prints its fixed five-character name (\c "INFO ", \c "CRIT ") so log
 * columns align; \c overflow_action and \c console_sink::stream print their
 * enumerator names. The last two exist only in a build with the host sinks, so
 * a build configured with \c NEXENNE_LOGGING_HOST_SINKS=OFF gets the \c level
 * layers alone.
 */

#include <format>
#include <ostream>
#include <string_view>

#include <nexenne/logging/level.hpp>

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)
#include <nexenne/logging/async_sink.hpp>
#include <nexenne/logging/sink.hpp>
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

#endif

}  // namespace nexenne::logging

/**
 * @brief \c std::format support for \c level: prints its padded name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
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

#if !defined(NEXENNE_LOGGING_NO_HOST_SINKS)

/**
 * @brief \c std::format support for \c overflow_action: prints its name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name.
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

#endif
