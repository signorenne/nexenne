#pragma once

/**
 * @file
 * @brief Consistent Overhead Byte Stuffing (COBS) encode / decode.
 *
 * COBS removes every zero byte from a payload, so a single \c 0x00 can be used
 * as an unambiguous packet delimiter on a byte stream (UART, RS-485, a TCP
 * framing layer). Overhead is at most one byte per 254 payload bytes plus one,
 * regardless of content, hence "consistent overhead". This is the standard
 * link-layer framing primitive for embedded serial protocols.
 *
 * These functions are a thin wrapper over the canonical codec in
 * \c nexenne::utility::cobs: the algorithm lives there once, and this layer only
 * remaps \c nexenne::utility::cobs::error onto the module-wide
 * \c nexenne::serialization::error vocabulary so serialization callers stay on
 * one error enum. Both directions write into a caller-provided buffer (no
 * allocation) and return the number of bytes produced, or an \c error. Size your
 * output buffer with \c max_encoded_size for encoding; a decode never produces
 * more than its input. The encoded form contains no \c 0x00, so append one
 * yourself as the delimiter when framing a stream.
 */

#include <cstddef>
#include <expected>
#include <span>

#include <nexenne/serialization/error.hpp>
#include <nexenne/utility/cobs.hpp>

namespace nexenne::serialization::cobs {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Maps a canonical COBS error onto the serialization error vocabulary.
 *
 * @param e Canonical error reported by \c nexenne::utility::cobs.
 *
 * @return The matching \c nexenne::serialization::error enumerator.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto to_error(nexenne::utility::cobs::error const e) noexcept -> error {
  switch (e) {
    case nexenne::utility::cobs::error::invalid_input:
      return error::invalid_input;
    case nexenne::utility::cobs::error::truncated_input:
      return error::buffer_underrun;
    case nexenne::utility::cobs::error::output_too_small:
      return error::buffer_full;
  }
  return error::invalid_input;
}

}  // namespace detail
/// @endcond

/**
 * @brief Worst-case encoded size for \p payload_len bytes (excludes delimiter).
 *
 * @param payload_len  Number of payload bytes to encode.
 *
 * @return The maximum number of bytes \c encode can produce.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto max_encoded_size(std::size_t const payload_len
) noexcept -> std::size_t {
  return nexenne::utility::cobs::max_encoded_size(payload_len);
}

/**
 * @brief Deprecated spelling of \c max_encoded_size.
 *
 * Kept as a thin forwarder during the rename away from the redundant
 * \c cobs_ prefix inside namespace \c cobs. Prefer \c max_encoded_size.
 *
 * @param payload_len  Number of payload bytes to encode.
 *
 * @return The maximum number of bytes \c encode can produce.
 *
 * @pre None.
 * @post None.
 *
 * @deprecated Use \c max_encoded_size instead.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto cobs_max_encoded_size(std::size_t const payload_len
) noexcept -> std::size_t {
  return max_encoded_size(payload_len);
}

/**
 * @brief COBS-encode \p in into \p out.
 *
 * @param in   Payload bytes (may contain zeros; may be empty).
 * @param out  Destination; size it with \c max_encoded_size(in.size()).
 *
 * @return Number of bytes written, or \c error::buffer_full if \p out is too
 *         small.
 *
 * @pre None.
 * @post On success \p out[0..return) is the zero-free encoding of \p in.
 *
 * @complexity \c O(in.size()).
 */
[[nodiscard]] inline auto encode(
  std::span<std::byte const> const in, std::span<std::byte> const out
) noexcept -> std::expected<std::size_t, error> {
  return nexenne::utility::cobs::encode(in, out).transform_error(detail::to_error);
}

/**
 * @brief COBS-decode \p in into \p out.
 *
 * @param in   Encoded bytes (one frame, delimiter byte NOT included).
 * @param out  Destination for the recovered payload.
 *
 * @return Number of bytes written, or \c error::invalid_input on a malformed
 *         frame, \c error::buffer_underrun on a truncated frame, or
 *         \c error::buffer_full if \p out is too small. An empty \p in decodes
 *         to zero payload bytes and succeeds, even though no COBS encoding
 *         produces an empty frame (an empty payload encodes to \c {0x01}); a
 *         framing layer that must tell an idle line apart from a real empty
 *         packet should reject an empty frame before calling.
 *
 * @pre \p in does not include the \c 0x00 delimiter.
 * @post On success \p out[0..return) is the decoded payload.
 *
 * @complexity \c O(in.size()).
 */
[[nodiscard]] inline auto decode(
  std::span<std::byte const> const in, std::span<std::byte> const out
) noexcept -> std::expected<std::size_t, error> {
  return nexenne::utility::cobs::decode(in, out).transform_error(detail::to_error);
}

}  // namespace nexenne::serialization::cobs
