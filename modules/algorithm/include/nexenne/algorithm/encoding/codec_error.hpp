#pragma once

/**
 * @file
 * @brief Error vocabulary shared by every encoding codec (hex, base64, COBS).
 *
 * Encoders and decoders return \c codec_result, a
 * \c std::expected<std::size_t, codec_error> carrying the number of bytes or
 * characters written on success. Separating "the input is bad" from "your
 * output buffer is too small" matters for embedded callers: the latter is
 * recoverable by enlarging the destination, the former is a protocol error.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Failure reason returned by an encoding or decoding codec.
 *
 * The three cases separate recoverable output sizing from unrecoverable input
 * defects: \c buffer_too_small is fixed by enlarging the destination, whereas
 * \c invalid_input and \c incomplete_input mean the source itself is malformed.
 */
enum class codec_error : std::uint8_t {
  invalid_input,     ///< Source contains a byte the codec rejects.
  buffer_too_small,  ///< Destination span exhausted before the codec finished.
  incomplete_input,  ///< Source ended mid-token (odd nibble, truncated quad).
};

/**
 * @brief The result of a heap-free codec call.
 *
 * Holds the count of bytes or characters written, or the reason it failed.
 */
using codec_result = std::expected<std::size_t, codec_error>;

/// @brief The result of a codec call returning freshly allocated bytes.
using codec_bytes = std::expected<std::vector<std::uint8_t>, codec_error>;

/// @brief The result of a codec call returning a freshly allocated string.
using codec_string = std::expected<std::string, codec_error>;

/**
 * @brief Returns a stable human-readable name for \p e.
 *
 * @param e Error to name.
 *
 * @return The enumerator's name as a string view (e.g. "invalid_input").
 *
 * @pre None.
 * @post The returned view refers to a static string and outlives the call.
 */
[[nodiscard]] constexpr auto to_string(codec_error const e) noexcept -> std::string_view {
  switch (e) {
    case codec_error::invalid_input:
      return "invalid_input";
    case codec_error::buffer_too_small:
      return "buffer_too_small";
    case codec_error::incomplete_input:
      return "incomplete_input";
  }
  return "?";
}

}  // namespace nexenne::algorithm
