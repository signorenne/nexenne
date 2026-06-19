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
 * Decode expects the bytes between two delimiters with the delimiters excluded,
 * not a trailing \c 0x00. Both directions are heap-free and return
 * \c codec_result. The decoded payload is always one byte shorter than its
 * encoded input.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <nexenne/algorithm/encoding/codec_error.hpp>

namespace nexenne::algorithm {

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
  return n_bytes + n_bytes / 254u + 1u;
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
 *         \p out is shorter than \c cobs_encoded_max_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success \p out contains no \c 0x00 byte and the written count is at
 *       most \c cobs_encoded_max_size of \p in size; on failure \p out is left
 *       unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto cobs_encode(
  std::span<std::uint8_t const> const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  if (out.size() < cobs_encoded_max_size(in.size())) {
    return std::unexpected{codec_error::buffer_too_small};
  }

  auto out_i{std::size_t{1}};  // Slot 0 holds the first code byte.
  auto code_i{std::size_t{0}};
  auto code{std::uint8_t{1}};

  auto const finish_block{[&]() noexcept {
    out[code_i] = code;
    code_i = out_i++;
    code = 1;
  }};

  for (auto const b : in) {
    if (b == 0u) {
      finish_block();
    } else {
      out[out_i++] = b;
      ++code;
      if (code == 0xFFu) {
        finish_block();
      }
    }
  }
  out[code_i] = code;
  return out_i;
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
 *         \c 0x00 code byte, \c codec_error::incomplete_input when a code byte
 *         points past the end of \p in, or \c codec_error::buffer_too_small
 *         when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c in.size() minus one; on
 *       failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto cobs_decode(
  std::span<std::uint8_t const> const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  auto in_i{std::size_t{0}};
  auto out_i{std::size_t{0}};

  while (in_i < in.size()) {
    auto const code{in[in_i++]};
    if (code == 0u) {
      return std::unexpected{codec_error::invalid_input};
    }
    for (auto k{std::uint8_t{1}}; k < code; ++k) {
      if (in_i >= in.size()) {
        return std::unexpected{codec_error::incomplete_input};
      }
      if (out_i >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[out_i++] = in[in_i++];
    }
    if (code < 0xFFu && in_i < in.size()) {
      if (out_i >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[out_i++] = 0u;
    }
  }
  return out_i;
}

}  // namespace nexenne::algorithm
