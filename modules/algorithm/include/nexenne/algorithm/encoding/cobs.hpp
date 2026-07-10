#pragma once

/**
 * @file
 * @brief Consistent Overhead Byte Stuffing (COBS) encode and decode.
 *
 * COBS removes every \c 0x00 byte from a payload so that \c 0x00 can serve as a
 * frame delimiter on a byte stream, the recurring problem on UART, RS-485, and
 * USB-CDC links carrying variable-length packets. Overhead is at most one byte
 * per 254 payload bytes (roughly 0.4 percent), and the encoded form is
 * guaranteed to contain no \c 0x00. Reference: Cheshire and Baker, "Consistent
 * Overhead Byte Stuffing", IEEE/ACM Transactions on Networking, 1999.
 *
 * These functions are a thin wrapper over the canonical codec in
 * \c nexenne::utility::cobs: the algorithm lives there once, and this layer only
 * adapts the \c std::uint8_t span surface (via \c std::as_bytes) and remaps
 * \c nexenne::utility::cobs::error onto \c codec_error. Decode expects the bytes
 * between two delimiters with the delimiters excluded, not a trailing \c 0x00. A
 * conformant COBS frame is zero-free by construction, so decode rejects any
 * \c 0x00 it meets, in a code position or a data position, as a lost delimiter
 * or bit error rather than accepting it as payload. Both directions are heap-free
 * and return \c codec_result. The decoded payload is always one byte shorter than
 * its encoded input.
 */

#include <cstddef>
#include <cstdint>
#include <span>

#include <nexenne/algorithm/encoding/codec_error.hpp>
#include <nexenne/utility/cobs.hpp>

namespace nexenne::algorithm {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Maps a canonical COBS error onto the \c codec_error vocabulary.
 *
 * @param e Canonical error reported by \c nexenne::utility::cobs.
 *
 * @return The matching \c codec_error enumerator.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto cobs_to_codec_error(nexenne::utility::cobs::error const e) noexcept
  -> codec_error {
  switch (e) {
    case nexenne::utility::cobs::error::invalid_input:
      return codec_error::invalid_input;
    case nexenne::utility::cobs::error::truncated_input:
      return codec_error::incomplete_input;
    case nexenne::utility::cobs::error::output_too_small:
      return codec_error::buffer_too_small;
  }
  return codec_error::invalid_input;
}

}  // namespace detail
/// @endcond

/**
 * @brief Upper bound on the COBS-encoded size of a \p n_bytes payload.
 *
 * @param n_bytes Number of raw payload bytes.
 *
 * @return The maximum number of bytes the encoded form can occupy, excluding
 *         the trailing frame delimiter.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto cobs_encoded_max_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  return nexenne::utility::cobs::max_encoded_size(n_bytes);
}

/**
 * @brief COBS-encode \p in into a caller-provided buffer.
 *
 * Stuffs the payload so the encoded form contains no \c 0x00 byte, leaving
 * \c 0x00 free to delimit frames. No trailing delimiter is written and no
 * allocation is performed. Size \p out with \c cobs_encoded_max_size.
 *
 * @param in Source payload bytes to encode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::buffer_too_small when
 *         \p out is too small to hold the encoding of \p in.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success \p out contains no \c 0x00 byte and the written count is at
 *       most \c cobs_encoded_max_size of \p in size; on failure \p out is left
 *       unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto cobs_encode(
  std::span<std::uint8_t const> const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return nexenne::utility::cobs::encode(std::as_bytes(in), std::as_writable_bytes(out))
    .transform_error(detail::cobs_to_codec_error);
}

/**
 * @brief COBS-decode \p in into a caller-provided buffer.
 *
 * Reverses \c cobs_encode. Pass the bytes between two \c 0x00 frame delimiters
 * with the delimiters excluded; no trailing delimiter is expected. No
 * allocation is performed. The decoded payload is one byte shorter than \p in.
 *
 * @param in Source COBS-encoded bytes to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         \c 0x00 byte in a code or data position (a valid frame is zero-free),
 *         \c codec_error::incomplete_input when a code byte points past the end
 *         of \p in, or \c codec_error::buffer_too_small when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c in.size() minus one; on
 *       failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto cobs_decode(
  std::span<std::uint8_t const> const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return nexenne::utility::cobs::decode(std::as_bytes(in), std::as_writable_bytes(out))
    .transform_error(detail::cobs_to_codec_error);
}

}  // namespace nexenne::algorithm
