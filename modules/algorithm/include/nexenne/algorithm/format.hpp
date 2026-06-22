#pragma once

/**
 * @file
 * @brief Opt-in \c std::format support for the algorithm error enums.
 *
 * Each algorithm error enum already carries a \c to_string; this header wires
 * those names into \c std::format so a value returned from an
 * \c std::expected<T, error> prints directly with \c std::format("{}", err),
 * with width and alignment specs for free. Keeping the formatters here (rather
 * than in the core error headers) leaves those headers free of \c \<format\>:
 * callers pay for the dependency only when they include this header.
 */

#include <format>
#include <string_view>

#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/algorithm/numerical/numerical_error.hpp>

/**
 * @brief \c std::format support for \c codec_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * and so \c std::format("{}", err) works directly on a value returned from a
 * \c codec_result without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::algorithm::codec_error> : std::formatter<std::string_view> {
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
  auto format(nexenne::algorithm::codec_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::algorithm::to_string(err), ctx);
  }
};

/**
 * @brief \c std::format support for \c numerical_error: prints its \c to_string name.
 *
 * Inherits the string formatter so a spec (width, alignment) applies to the name,
 * and so \c std::format("{}", err) works directly on a value returned from an
 * \c std::expected<T, numerical_error> without a manual \c to_string call.
 */
template <>
struct std::formatter<nexenne::algorithm::numerical_error> : std::formatter<std::string_view> {
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
  auto format(nexenne::algorithm::numerical_error const err, FormatContext& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::algorithm::to_string(err), ctx);
  }
};
