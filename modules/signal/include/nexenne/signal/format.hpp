#pragma once

/**
 * @file
 * @brief Debug printing and formatting for the signal module's handle types.
 *
 * Three layers, like the rest of the library's format headers: \c to_string(x)
 * builds a readable string, \c operator<<(std::ostream&, x) streams it, and a
 * \c std::formatter specialization makes \c std::format("{}", x) work. The output
 * is for diagnostics, not serialisation, and is not stable across versions.
 *
 * Only the public handle value types are covered: \c connection (slot id plus
 * whether the owning signal is still alive) and \c static_connection (slot id
 * plus whether it names a slot). \c scoped_connection and
 * \c static_scoped_connection print through their owned handle, so a dedicated
 * formatter would only duplicate them.
 *
 * As with every module, the standard \c format header is heavy, so this header is opt-in: include
 * it only where you print a handle.
 */

#include <format>
#include <ostream>
#include <string>

#include <nexenne/signal/connection.hpp>
#include <nexenne/signal/static_signal.hpp>

namespace nexenne::signal {

/**
 * @brief Debug string for a \c connection.
 *
 * @param c Connection to describe.
 *
 * @return A string of the form \c "connection(id=N, valid=B)".
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(connection const& c) -> std::string {
  return std::format("connection(id={}, valid={})", c.slot_id(), c.valid());
}

/**
 * @brief Streams a \c connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, connection const& c) -> std::ostream& {
  return os << to_string(c);
}

/**
 * @brief Debug string for a \c static_connection.
 *
 * @param c Connection to describe.
 *
 * @return A string of the form \c "static_connection(id=N, has_target=B)".
 *
 * @pre None.
 * @post \p c is unchanged.
 */
[[nodiscard]] inline auto to_string(static_connection const& c) -> std::string {
  return std::format("static_connection(id={}, has_target={})", c.id(), c.has_target());
}

/**
 * @brief Streams a \c static_connection via \c to_string.
 *
 * @param os Output stream.
 * @param c Connection to stream.
 *
 * @return \p os, for chaining.
 *
 * @pre None.
 * @post \p c is unchanged; \p os has the debug string appended.
 */
inline auto operator<<(std::ostream& os, static_connection const& c) -> std::ostream& {
  return os << to_string(c);
}

}  // namespace nexenne::signal

/**
 * @brief Formats a \c connection via \c nexenne::signal::to_string.
 */
template <>
struct std::formatter<nexenne::signal::connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};

/**
 * @brief Formats a \c static_connection via \c nexenne::signal::to_string.
 */
template <>
struct std::formatter<nexenne::signal::static_connection> {
  /**
   * @brief Accepts an empty format spec.
   *
   * @param ctx Parse context positioned at the format spec.
   *
   * @return Iterator to the end of the parsed spec.
   *
   * @pre The spec between the braces is empty.
   * @post None.
   */
  static constexpr auto parse(std::format_parse_context& ctx) {
    return ctx.begin();
  }

  /**
   * @brief Writes the connection's debug string to the output context.
   *
   * @tparam FormatContext Deduced output context type.
   * @param c Connection to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post \p c is unchanged; its debug string has been written to \p ctx.
   */
  template <typename FormatContext>
  static auto format(nexenne::signal::static_connection const& c, FormatContext& ctx) {
    return std::format_to(ctx.out(), "{}", nexenne::signal::to_string(c));
  }
};
