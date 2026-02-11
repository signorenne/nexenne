#pragma once

/**
 * @file
 * @brief Common error codes for the nexenne::serialization module.
 *
 * Every fallible operation in the module returns
 * \c std::expected<T, error>. The error codes cover both the
 * JSON subsystem (parsing and DOM access) and the binary
 * subsystem (writer / reader bounds checks).
 *
 * \c to_string returns a human-readable name suitable for
 * logging or error messages.
 */

#include <cstdint>
#include <string_view>

namespace nexenne::serialization {

/**
 * @brief Error codes returned by every fallible operation in the module.
 *
 * The underlying type is \c std::uint8_t so the code fits in a register
 * and travels cheaply inside \c std::expected. The first group is shared
 * by all subsystems, the second is JSON-specific, and the third is raised
 * only by the binary writer / reader bounds checks.
 *
 * @note Codes are not stable across releases, do not serialise the
 *       numeric value. Use \c to_string for a stable textual name.
 */
enum class error : std::uint8_t {
  invalid_input,  ///< Malformed payload, generic catch-all.

  unexpected_character,  ///< Encountered a char not allowed at this position.
  unexpected_end,        ///< Input ended mid-token.
  invalid_number,        ///< Number literal failed to parse.
  invalid_string,        ///< String literal contains illegal bytes.
  invalid_escape,        ///< Bad \\-escape sequence inside a string.
  duplicate_key,         ///< Object had two members with the same key.
  depth_limit_exceeded,  ///< Nesting deeper than \c parse_options::max_depth.
  type_mismatch,         ///< \c as_int() called on a non-integer, etc.
  path_not_found,        ///< \c at_path() failed to resolve a JSON Pointer.

  buffer_full,      ///< Writer ran out of destination space.
  buffer_underrun,  ///< Reader hit end-of-buffer mid-value.
  string_too_long,  ///< String length prefix exceeded a sane limit.
};

/**
 * @brief Human-readable name of error code \p e.
 *
 * Returns the enumerator spelling (e.g. \c "buffer_underrun") for logging
 * and diagnostics. The returned view points at a static string literal and
 * outlives every caller.
 *
 * @param e  Error code to name.
 *
 * @return Static string view naming \p e, or \c "?" if \p e is not a
 *         declared enumerator.
 *
 * @pre None.
 * @post The returned view is non-empty and references storage with static
 *       lifetime.
 */
[[nodiscard]] constexpr auto to_string(error const e) noexcept -> std::string_view {
  switch (e) {
    case error::invalid_input:
      return "invalid_input";
    case error::unexpected_character:
      return "unexpected_character";
    case error::unexpected_end:
      return "unexpected_end";
    case error::invalid_number:
      return "invalid_number";
    case error::invalid_string:
      return "invalid_string";
    case error::invalid_escape:
      return "invalid_escape";
    case error::duplicate_key:
      return "duplicate_key";
    case error::depth_limit_exceeded:
      return "depth_limit_exceeded";
    case error::type_mismatch:
      return "type_mismatch";
    case error::path_not_found:
      return "path_not_found";
    case error::buffer_full:
      return "buffer_full";
    case error::buffer_underrun:
      return "buffer_underrun";
    case error::string_too_long:
      return "string_too_long";
  }
  return "?";
}

}  // namespace nexenne::serialization
