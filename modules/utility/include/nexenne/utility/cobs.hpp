#pragma once

/**
 * @file
 * @brief Canonical Consistent Overhead Byte Stuffing (COBS) encode and decode.
 *
 * COBS removes every \c 0x00 byte from a payload so that a single \c 0x00 can
 * serve as an unambiguous frame delimiter on a byte stream (UART, RS-485,
 * USB-CDC, a TCP framing layer). Overhead is at most one byte per 254 payload
 * bytes plus one, regardless of content, hence "consistent overhead". This is
 * the standard link-layer framing primitive for embedded serial protocols.
 * Reference: Cheshire and Baker, "Consistent Overhead Byte Stuffing", IEEE/ACM
 * Transactions on Networking, 1999.
 *
 * Both functions write into a caller-provided buffer (no allocation), work on
 * \c std::span<std::byte>, and return the number of bytes produced or an
 * \c error. Size an encode destination with \c max_encoded_size; a decode never
 * produces more bytes than its input. The encoded form contains no \c 0x00, so
 * append one yourself as the delimiter when framing a stream, and strip it (pass
 * the bytes between two delimiters) before decoding.
 *
 * A conformant COBS frame is zero-free by construction, so \c decode rejects any
 * \c 0x00 it meets, in a code position or a data position, as a lost delimiter
 * or bit error rather than accepting it as payload. This is the single canonical
 * codec: \c nexenne::serialization::cobs and \c nexenne::algorithm::cobs_encode
 * are thin wrappers over it that only remap the error enum to their module
 * vocabulary.
 */

#include <cstddef>
#include <cstdint>
#include <expected>
#include <format>
#include <span>
#include <string_view>

namespace nexenne::utility::cobs {

/**
 * @brief Failure reason returned by \c encode or \c decode.
 *
 * The three cases separate a recoverable output-sizing problem from an
 * unrecoverable input defect: \c output_too_small is fixed by enlarging the
 * destination, whereas \c invalid_input and \c truncated_input mean the source
 * frame itself is malformed.
 */
enum class error : std::uint8_t {
  invalid_input,     ///< A \c 0x00 byte appears inside a frame (never legal).
  truncated_input,   ///< A code byte references data past the end of the frame.
  output_too_small,  ///< The destination span was exhausted before finishing.
};

/**
 * @brief Human-readable name of error code \p e.
 *
 * Returns the enumerator spelling (for example \c "invalid_input") for logging
 * and diagnostics. The returned view points at a static string literal.
 *
 * @param e Error code to name.
 *
 * @return Static string view naming \p e, or \c "?" if \p e is not a declared
 *         enumerator.
 *
 * @pre None.
 * @post The returned view is non-empty and references storage with static
 *       lifetime.
 */
[[nodiscard]] constexpr auto to_string(error const e) noexcept -> std::string_view {
  switch (e) {
    case error::invalid_input:
      return "invalid_input";
    case error::truncated_input:
      return "truncated_input";
    case error::output_too_small:
      return "output_too_small";
  }
  return "?";
}

/**
 * @brief Worst-case encoded size for \p payload_len bytes (excludes delimiter).
 *
 * A COBS frame adds one leading code byte plus one extra code byte for every
 * full 254-byte run of non-zero data, so the bound is
 * \c payload_len + payload_len / 254 + 1.
 *
 * @param payload_len Number of payload bytes to encode.
 *
 * @return The maximum number of bytes \c encode can produce.
 *
 * @pre None.
 * @post None.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto max_encoded_size(std::size_t const payload_len) noexcept
  -> std::size_t {
  return payload_len + payload_len / 254 + 1;
}

/**
 * @brief COBS-encode \p in into \p out.
 *
 * Stuffs the payload so the encoded form contains no \c 0x00 byte, leaving
 * \c 0x00 free to delimit frames. No trailing delimiter is written and no
 * allocation is performed. The destination is filled incrementally and the
 * bound is checked before every write, so a buffer sized to the exact encoded
 * length (which may be smaller than \c max_encoded_size) still succeeds.
 *
 * @param in Payload bytes (may contain zeros; may be empty).
 * @param out Destination; size it with \c max_encoded_size(in.size()).
 *
 * @return Number of bytes written, or \c error::output_too_small if \p out is
 *         too small.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success \p out[0..return) is the zero-free encoding of \p in.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto
encode(std::span<std::byte const> const in, std::span<std::byte> const out) noexcept
  -> std::expected<std::size_t, error> {
  auto const cap{out.size()};
  if (cap < 1) {
    return std::unexpected{error::output_too_small};
  }

  std::size_t write{1};     // out[0] is reserved for the first code byte.
  std::size_t code_idx{0};  // Slot the current run's code byte will land in.
  std::uint8_t code{1};     // One more than the non-zero bytes seen in the run.

  for (std::size_t read{0}; read < in.size(); ++read) {
    if (in[read] != std::byte{0}) {
      if (write >= cap) {
        return std::unexpected{error::output_too_small};
      }
      out[write++] = in[read];
      if (++code == 0xFF) {  // The run is a full block; flush it and open a new one.
        out[code_idx] = static_cast<std::byte>(code);
        if (write >= cap) {
          return std::unexpected{error::output_too_small};
        }
        code_idx = write++;
        code = 1;
      }
    } else {  // A zero closes the run: write its code byte and start the next run.
      out[code_idx] = static_cast<std::byte>(code);
      if (write >= cap) {
        return std::unexpected{error::output_too_small};
      }
      code_idx = write++;
      code = 1;
    }
  }
  out[code_idx] = static_cast<std::byte>(code);
  return write;
}

/**
 * @brief COBS-decode \p in into \p out.
 *
 * Reverses \c encode. Pass one frame, the bytes between two \c 0x00 delimiters
 * with the delimiters excluded; no trailing delimiter is expected. A conformant
 * frame is zero-free, so any \c 0x00 met in a code or data position is rejected.
 * No allocation is performed.
 *
 * @param in Encoded bytes (one frame, delimiter byte NOT included).
 * @param out Destination for the recovered payload.
 *
 * @return Number of bytes written, or \c error::invalid_input on a \c 0x00 byte
 *         in the frame, \c error::truncated_input when a code byte points past
 *         the end of \p in, or \c error::output_too_small if \p out is too
 *         small. An empty \p in decodes to zero payload bytes and succeeds, even
 *         though no COBS encoding produces an empty frame (an empty payload
 *         encodes to \c {0x01}); a framing layer that must tell an idle line
 *         apart from a real empty packet should reject an empty frame before
 *         calling.
 *
 * @pre \p in does not include the \c 0x00 delimiter.
 * @pre \p in and \p out do not overlap.
 * @post On success \p out[0..return) is the decoded payload.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto
decode(std::span<std::byte const> const in, std::span<std::byte> const out) noexcept
  -> std::expected<std::size_t, error> {
  auto const n{in.size()};

  std::size_t read{0};
  std::size_t write{0};

  while (read < n) {
    std::uint8_t const code{std::to_integer<std::uint8_t>(in[read++])};
    if (code == 0) {
      return std::unexpected{error::invalid_input};  // No zeros in a COBS frame.
    }
    for (std::uint8_t i{1}; i < code; ++i) {
      if (read >= n) {
        return std::unexpected{error::truncated_input};
      }
      std::byte const b{in[read++]};
      if (b == std::byte{0}) {
        return std::unexpected{error::invalid_input};  // A valid frame has no zeros.
      }
      if (write >= out.size()) {
        return std::unexpected{error::output_too_small};
      }
      out[write++] = b;
    }
    if (code != 0xFF && read < n) {  // A non-full block implies a payload zero.
      if (write >= out.size()) {
        return std::unexpected{error::output_too_small};
      }
      out[write++] = std::byte{0};
    }
  }
  return write;
}

}  // namespace nexenne::utility::cobs

/**
 * @brief \c std::formatter specialisation printing a COBS \c error by its name.
 *
 * Forwards to \c to_string, so \c std::format("{}", error::invalid_input)
 * yields \c "invalid_input" and a string spec such as "{:>16}" pads the name.
 */
template <>
struct std::formatter<nexenne::utility::cobs::error> : std::formatter<std::string_view> {
  /**
   * @brief Formats \p e by writing its name.
   *
   * @tparam Context Formatting context type.
   * @param e Error code to format.
   * @param ctx Format context to write into.
   *
   * @return The output iterator past the formatted text.
   *
   * @pre None.
   * @post None.
   */
  template <typename Context>
  auto format(nexenne::utility::cobs::error const e, Context& ctx) const {
    return std::formatter<std::string_view>::format(nexenne::utility::cobs::to_string(e), ctx);
  }
};
