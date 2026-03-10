#pragma once

/**
 * @file
 * @brief Compile-time-validated format string paired with a captured call site.
 *
 * Lives in its own tiny header (only the standard format, source_location,
 * string_view, and concepts headers) so the heap-free \c stream_logger can reuse
 * it without dragging in the manager backend's thread, mutex, and vector
 * machinery.
 */

#include <concepts>
#include <format>
#include <source_location>
#include <string_view>

namespace nexenne::logging {

/**
 * @brief Format string paired with an auto-captured source location.
 *
 * Wraps a \c std::format_string and the call site's \c source_location so a
 * logging call captures file and line without the caller passing them. The
 * converting constructor is \c consteval, validating the format spec at compile
 * time; it is deliberately implicit so a string literal becomes a
 * \c format_string at the call site.
 *
 * @tparam Args Argument types the format string expects.
 *
 * @pre None.
 * @post None.
 */
template <typename... Args>
struct format_string {
  std::format_string<Args...> fmt;  ///< The validated format string.
  std::source_location loc;         ///< Captured call-site location.

  /**
   * @brief Builds from a compile-time format spec, capturing the call site.
   *
   * @tparam S String-view-convertible format spec type.
   * @param format_spec The format string literal or constant.
   * @param where Source location; defaults to the call site.
   *
   * @pre \p format_spec is a valid format string for \p Args.
   * @post \c fmt holds the spec and \c loc holds \p where.
   */
  template <typename S>
    requires std::convertible_to<S const&, std::string_view>
  consteval format_string(
    S const& format_spec, std::source_location const where = std::source_location::current()
  )
      : fmt{format_spec}, loc{where} {}
};

}  // namespace nexenne::logging
