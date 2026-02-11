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
 * Both functions write into a caller-provided buffer (no allocation) and return
 * the number of bytes produced, or an \c error. Size your output buffer with
 * \c cobs_max_encoded_size for encoding; a decode never produces more than its
 * input. The encoded form contains no \c 0x00, so append one yourself as the
 * delimiter when framing a stream.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>

#include <nexenne/serialization/error.hpp>

namespace nexenne::serialization::cobs {

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
[[nodiscard]] constexpr auto cobs_max_encoded_size(std::size_t const payload_len
) noexcept -> std::size_t {
  return payload_len + payload_len / 254 + 1;
}

/**
 * @brief COBS-encode \p in into \p out.
 *
 * @param in   Payload bytes (may contain zeros; may be empty).
 * @param out  Destination; size it with \c cobs_max_encoded_size(in.size()).
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
  auto const* const src{reinterpret_cast<std::uint8_t const*>(in.data())};
  auto* const dst{reinterpret_cast<std::uint8_t*>(out.data())};
  auto const cap{out.size()};

  if (cap < 1)
    return std::unexpected{error::buffer_full};

  std::size_t write{1};  // out[0] reserved for the first code byte
  std::size_t code_idx{0};
  std::uint8_t code{1};

  for (std::size_t read{0}; read < in.size(); ++read) {
    if (src[read] != 0) {
      if (write >= cap)
        return std::unexpected{error::buffer_full};
      dst[write++] = src[read];
      if (++code == 0xFF) {
        dst[code_idx] = code;
        if (write >= cap)
          return std::unexpected{error::buffer_full};
        code_idx = write++;
        code = 1;
      }
    } else {
      dst[code_idx] = code;
      if (write >= cap)
        return std::unexpected{error::buffer_full};
      code_idx = write++;
      code = 1;
    }
  }
  dst[code_idx] = code;
  return write;
}

/**
 * @brief COBS-decode \p in into \p out.
 *
 * @param in   Encoded bytes (one frame, delimiter byte NOT included).
 * @param out  Destination for the recovered payload.
 *
 * @return Number of bytes written, or \c error::invalid_input on a malformed
 *         frame, \c error::buffer_underrun on a truncated frame, or
 *         \c error::buffer_full if \p out is too small.
 *
 * @pre \p in does not include the \c 0x00 delimiter.
 * @post On success \p out[0..return) is the decoded payload.
 *
 * @complexity \c O(in.size()).
 */
[[nodiscard]] inline auto decode(
  std::span<std::byte const> const in, std::span<std::byte> const out
) noexcept -> std::expected<std::size_t, error> {
  auto const* const src{reinterpret_cast<std::uint8_t const*>(in.data())};
  auto* const dst{reinterpret_cast<std::uint8_t*>(out.data())};
  auto const n{in.size()};

  std::size_t read{0};
  std::size_t write{0};

  while (read < n) {
    std::uint8_t const code{src[read++]};
    if (code == 0)
      return std::unexpected{error::invalid_input};  // no zeros in COBS
    for (std::uint8_t i{1}; i < code; ++i) {
      if (read >= n)
        return std::unexpected{error::buffer_underrun};
      std::uint8_t const b{src[read++]};
      if (b == 0)
        return std::unexpected{error::invalid_input};  // a valid frame has no zeros
      if (write >= out.size())
        return std::unexpected{error::buffer_full};
      dst[write++] = b;
    }
    if (code != 0xFF && read < n) {
      if (write >= out.size())
        return std::unexpected{error::buffer_full};
      dst[write++] = 0;
    }
  }
  return write;
}

}  // namespace nexenne::serialization::cobs
