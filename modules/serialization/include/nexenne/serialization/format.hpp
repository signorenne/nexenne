#pragma once

/**
 * @file
 * @brief \c std::format support for the nexenne::serialization public types.
 *
 * Opt-in printing layer so callers can write \c std::format("{}", v) directly:
 *
 *   - \c error : prints its \c to_string enumerator name;
 *   - \c json::value : prints its compact \c serialize output.
 *
 * The core headers stay free of the \c \<format\> include; pull this header in
 * only where the formatting hooks are wanted, keeping it off the hot path.
 *
 * @note \c json::parse_error carries no \c to_string, so it has no formatter
 *       here; format its \c code member (an \c error) and its position fields
 *       individually if a diagnostic string is needed.
 */

#include <format>
#include <string_view>

#include <nexenne/serialization/error.hpp>
#include <nexenne/serialization/json/serialize.hpp>
#include <nexenne/serialization/json/value.hpp>

/**
 * @brief \c std::format support for \c error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * name, and so \c std::format("{}", err) works directly on a value returned
 * from a \c std::expected without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::serialization::error> : std::formatter<std::string_view> {
  /**
   * @brief Formats the error's \c to_string name through the string formatter.
   *
   * @tparam FormatContext Deduced output context type.
   * @param e Error to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The error name has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::error const e, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::serialization::to_string(e), ctx);
  }
};

/**
 * @brief \c std::format support for \c json::value: prints its compact JSON.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the
 * serialised text, and forwards to \c json::serialize, so
 * \c std::format("{}", v) yields the same one-line JSON a manual
 * \c serialize(v) would. Use \c serialize_pretty directly when indented
 * output is wanted, the format spec covers only the compact form.
 */
template <>
struct std::formatter<nexenne::serialization::json::value> : std::formatter<std::string_view> {
  /**
   * @brief Formats the value's compact \c serialize output as a string.
   *
   * @tparam FormatContext Deduced output context type.
   * @param v Value to format.
   * @param ctx Format context receiving the output.
   *
   * @return Iterator past the last character written.
   *
   * @pre None.
   * @post The serialised JSON has been written to \p ctx.
   */
  template <typename FormatContext>
  auto format(nexenne::serialization::json::value const& v, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(
      nexenne::serialization::json::serialize(v), ctx
    );
  }
};
