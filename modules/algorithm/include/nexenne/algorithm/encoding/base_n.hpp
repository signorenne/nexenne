#pragma once

/**
 * @file
 * @brief Base-N encodings: the generic engine plus hex, base32, and base64.
 *
 * RFC 4648 defines base16, base32, and base64 as one family: each repacks the
 * input bitstream into symbols of a fixed width (4, 5, or 6 bits) drawn from an
 * alphabet, padding the final group to a boundary. A \c base_n_spec captures
 * that as a non-type template parameter, exactly as \c crc_spec does for CRCs,
 * so \c base_n_encode and \c base_n_decode generate one monomorphized, fully
 * unrolled codec per spec, with no runtime cost for the genericity.
 *
 * The named codecs (\c hex, \c base32 / \c base32hex, \c base64 / \c base64url)
 * are defined here too, as thin faces over the engine; each has a heap-free
 * overload returning \c codec_result and a heap-allocating one. Symbol count is
 * the template parameter, so the bit width is \c log2(Symbols); 16, 32, and 64
 * are the standard members of the family.
 */

#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <numeric>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <nexenne/algorithm/encoding/alphabet.hpp>
#include <nexenne/algorithm/encoding/codec_error.hpp>

namespace nexenne::algorithm {

/**
 * @brief Compile-time description of one base-N encoding.
 *
 * Pass as a non-type template parameter to \c base_n_encode or
 * \c base_n_decode. \c Symbols (a power of two) fixes the alphabet size and so
 * the bit width per symbol; the remaining fields select padding and case
 * folding.
 *
 * @tparam Symbols Number of alphabet symbols: 16, 32, or 64.
 */
template <std::size_t Symbols>
  requires(std::has_single_bit(Symbols) && Symbols >= 2 && Symbols <= 64)
struct base_n_spec {
  static constexpr std::size_t symbols{Symbols};
  static constexpr std::size_t bits{static_cast<std::size_t>(std::bit_width(Symbols)) - 1u};
  static constexpr std::size_t group_in{std::lcm(std::size_t{8}, bits) / 8};
  static constexpr std::size_t group_out{std::lcm(std::size_t{8}, bits) / bits};

  codec_alphabet<Symbols> alphabet;  ///< Symbol table (forward and reverse).
  bool padded{true};                 ///< Pad the final group to a boundary.
  char pad{'='};                     ///< Padding character when \c padded.
  bool case_insensitive{false};      ///< Fold ASCII lowercase to uppercase on decode.
};

/**
 * @brief Exact encoded length for \p n_bytes input bytes under \p Spec.
 *
 * @tparam Spec The base-N encoding.
 * @param n_bytes Number of raw bytes to encode.
 *
 * @return The number of symbols produced, including padding when the spec pads.
 *
 * @pre None.
 * @post None.
 */
template <base_n_spec Spec>
[[nodiscard]] constexpr auto base_n_encoded_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  constexpr auto gin{decltype(Spec)::group_in};
  constexpr auto gout{decltype(Spec)::group_out};
  if constexpr (Spec.padded) {
    return ((n_bytes + gin - 1) / gin) * gout;
  } else {
    auto const full{n_bytes / gin};
    auto const tail_bytes{n_bytes % gin};
    auto const tail_syms{(tail_bytes * 8 + decltype(Spec)::bits - 1) / decltype(Spec)::bits};
    return full * gout + tail_syms;
  }
}

/**
 * @brief Safe upper bound on the decoded byte count for \p n_chars symbols.
 *
 * @tparam Spec The base-N encoding.
 * @param n_chars Number of encoded characters.
 *
 * @return A byte count at least as large as any valid decoding of \p n_chars
 *         characters.
 *
 * @pre None.
 * @post None.
 */
template <base_n_spec Spec>
[[nodiscard]] constexpr auto base_n_decoded_max_size(std::size_t const n_chars
) noexcept -> std::size_t {
  return (n_chars * decltype(Spec)::bits) / 8 + 1;
}

/**
 * @brief Generic base-N encode into a caller-provided buffer.
 *
 * Repacks the input bitstream into \c Spec.bits-wide symbols, MSB first, and
 * pads the final group when the spec pads. No allocation. Size \p out with
 * \c base_n_encoded_size.
 *
 * @tparam Spec The base-N encoding.
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c base_n_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c base_n_encoded_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
template <base_n_spec Spec>
[[nodiscard]] constexpr auto base_n_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out
) noexcept -> codec_result {
  constexpr auto bits{static_cast<int>(decltype(Spec)::bits)};
  constexpr auto mask{(std::uint32_t{1} << bits) - 1};
  constexpr auto gout{decltype(Spec)::group_out};

  if (out.size() < base_n_encoded_size<Spec>(in.size())) {
    return std::unexpected{codec_error::buffer_too_small};
  }

  auto acc{std::uint32_t{0}};
  auto nbits{0};
  auto o{std::size_t{0}};
  for (auto const b : in) {
    acc = (acc << 8) | b;
    nbits += 8;
    while (nbits >= bits) {
      nbits -= bits;
      out[o++] = Spec.alphabet.encode(static_cast<std::size_t>((acc >> nbits) & mask));
    }
    acc &= (std::uint32_t{1} << nbits) - 1;
  }
  if (nbits > 0) {
    out[o++] = Spec.alphabet.encode(static_cast<std::size_t>((acc << (bits - nbits)) & mask));
  }
  if constexpr (Spec.padded) {
    while (o % gout != 0) {
      out[o++] = Spec.pad;
    }
  }
  return o;
}

/**
 * @brief Generic base-N decode into a caller-provided buffer.
 *
 * Reverses \c base_n_encode. ASCII whitespace is skipped, omitted trailing
 * padding is tolerated, and case is folded when the spec is case-insensitive.
 * No allocation. Size \p out with \c base_n_decoded_max_size.
 *
 * @tparam Spec The base-N encoding.
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-alphabet character or padding mid-stream,
 *         \c codec_error::incomplete_input for a truncated final group, or
 *         \c codec_error::buffer_too_small when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c base_n_decoded_max_size of
 *       \p in size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
template <base_n_spec Spec>
[[nodiscard]] constexpr auto base_n_decode(
  std::string_view const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  constexpr auto bits{static_cast<int>(decltype(Spec)::bits)};

  auto acc{std::uint32_t{0}};
  auto nbits{0};
  auto o{std::size_t{0}};
  auto saw_pad{false};

  for (auto const raw : in) {
    auto ch{raw};
    if constexpr (Spec.case_insensitive) {
      if (ch >= 'a' && ch <= 'z') {
        ch = static_cast<char>(ch - 'a' + 'A');
      }
    }
    if (ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n') {
      continue;
    }
    if constexpr (Spec.padded) {
      if (ch == Spec.pad) {
        saw_pad = true;
        continue;
      }
    }
    if (saw_pad) {
      return std::unexpected{codec_error::invalid_input};
    }
    auto const v{Spec.alphabet.decode(ch)};
    if (v < 0) {
      return std::unexpected{codec_error::invalid_input};
    }
    acc = (acc << bits) | static_cast<std::uint32_t>(v);
    nbits += bits;
    if (nbits >= 8) {
      nbits -= 8;
      if (o >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = static_cast<std::uint8_t>((acc >> nbits) & 0xFFu);
      acc &= (std::uint32_t{1} << nbits) - 1;
    }
  }
  // Leftover bits below one symbol are padding; a whole symbol left over means
  // the final group was truncated.
  if (nbits >= bits) {
    return std::unexpected{codec_error::incomplete_input};
  }
  return o;
}

/// @brief Base16 (hex) with the lowercase alphabet; decode accepts either case.
inline constexpr auto base16_lower_spec{
  base_n_spec<16>{.alphabet = {"0123456789abcdef"}, .padded = false, .case_insensitive = true}
};
/// @brief Base16 (hex) with the uppercase alphabet; decode accepts either case.
inline constexpr auto base16_upper_spec{
  base_n_spec<16>{.alphabet = {"0123456789ABCDEF"}, .padded = false, .case_insensitive = true}
};
/// @brief Standard RFC 4648 base32 (A-Z 2-7), padded, case-insensitive decode.
inline constexpr auto base32_std_spec{base_n_spec<32>{
  .alphabet = {"ABCDEFGHIJKLMNOPQRSTUVWXYZ234567"}, .padded = true, .case_insensitive = true
}};
/// @brief Extended-hex RFC 4648 base32 (0-9 A-V), padded, case-insensitive decode.
inline constexpr auto base32_hex_spec{base_n_spec<32>{
  .alphabet = {"0123456789ABCDEFGHIJKLMNOPQRSTUV"}, .padded = true, .case_insensitive = true
}};
/// @brief Standard RFC 4648 base64 (with plus and slash), padded, case-sensitive.
inline constexpr auto base64_std_spec{base_n_spec<64>{
  .alphabet = {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"},
  .padded = true,
  .case_insensitive = false
}};
/// @brief URL-safe RFC 4648 base64 (with hyphen and underscore), unpadded.
inline constexpr auto base64_url_spec{base_n_spec<64>{
  .alphabet = {"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"},
  .padded = false,
  .case_insensitive = false
}};

static_assert(base16_lower_spec.alphabet.is_distinct());
static_assert(base16_upper_spec.alphabet.is_distinct());
static_assert(base32_std_spec.alphabet.is_distinct());
static_assert(base32_hex_spec.alphabet.is_distinct());
static_assert(base64_std_spec.alphabet.is_distinct());
static_assert(base64_url_spec.alphabet.is_distinct());

// hex (base16)

/**
 * @brief Exact output size for hex-encoding \p n_bytes raw bytes.
 *
 * @param n_bytes Number of raw bytes to encode.
 *
 * @return The number of hex characters produced, always \c 2 * n_bytes.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto hex_encoded_size(std::size_t const n_bytes) noexcept -> std::size_t {
  return base_n_encoded_size<base16_lower_spec>(n_bytes);
}

/**
 * @brief Heap-free hex encode into a caller-provided buffer.
 *
 * Writes two hex characters per input byte, lowercase by default or uppercase
 * when \p uppercase is \c true. No allocation. Size \p out with
 * \c hex_encoded_size.
 *
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 * @param uppercase When \c true, emit A-F instead of a-f.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c hex_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c hex_encoded_size of \p in size;
 *       on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto hex_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out, bool const uppercase = false
) noexcept -> codec_result {
  return uppercase ? base_n_encode<base16_upper_spec>(in, out)
                   : base_n_encode<base16_lower_spec>(in, out);
}

/**
 * @brief Heap-allocating hex encoder.
 *
 * @param in Source bytes to encode.
 * @param uppercase When \c true, emit uppercase A-F, otherwise lowercase.
 *
 * @return A hex-encoded string of length \c 2 * in.size().
 *
 * @pre None.
 * @post The returned string has size \c hex_encoded_size of \p in size.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto
hex_encode(std::span<std::uint8_t const> const in, bool const uppercase = false) -> std::string {
  auto out{std::string(hex_encoded_size(in.size()), '\0')};
  out.resize(hex_encode(in, std::span<char>{out.data(), out.size()}, uppercase).value_or(0));
  return out;
}

/**
 * @brief Heap-free hex decode into a caller-provided buffer.
 *
 * Decodes two hex characters per output byte; ASCII whitespace is skipped and
 * mixed case accepted. No allocation. An \p out as large as \c in.size() / 2
 * always suffices.
 *
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-hex character, \c codec_error::incomplete_input for an odd nibble
 *         count, or \c codec_error::buffer_too_small when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals the number of decoded byte pairs;
 *       on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] constexpr auto
hex_decode(std::string_view const in, std::span<std::uint8_t> const out) noexcept -> codec_result {
  return base_n_decode<base16_upper_spec>(in, out);
}

/**
 * @brief Heap-allocating hex decoder.
 *
 * ASCII whitespace is skipped and mixed case accepted.
 *
 * @param in Hex-encoded string to decode.
 *
 * @return On success, the decoded bytes; on failure, the \c codec_error for the
 *         first error encountered.
 *
 * @pre None.
 * @post On success the result holds the bytes encoded by \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto hex_decode(std::string_view const in) -> codec_bytes {
  auto out{std::vector<std::uint8_t>(base_n_decoded_max_size<base16_upper_spec>(in.size()))};
  auto const r{hex_decode(in, std::span<std::uint8_t>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

// base32

/**
 * @brief Exact output size for padded Base32 encoding of \p n_bytes bytes.
 *
 * @param n_bytes Number of raw bytes to encode.
 *
 * @return The number of Base32 characters produced, including padding.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto base32_encoded_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  return base_n_encoded_size<base32_std_spec>(n_bytes);
}

/**
 * @brief Safe upper bound on the decoded byte count for \p n_chars Base32
 *        characters.
 *
 * @param n_chars Number of Base32 characters in the input.
 *
 * @return The maximum number of bytes the decoded output can occupy.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto base32_decoded_max_size(std::size_t const n_chars
) noexcept -> std::size_t {
  return base_n_decoded_max_size<base32_std_spec>(n_chars);
}

/**
 * @brief Standard RFC 4648 (section 6) Base32 encode into a buffer.
 *
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c base32_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c base32_encoded_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto base32_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out
) noexcept -> codec_result {
  return base_n_encode<base32_std_spec>(in, out);
}

/**
 * @brief Heap-allocating standard Base32 encoder.
 *
 * @param in Source bytes to encode.
 *
 * @return The Base32-encoded string, with padding.
 *
 * @pre None.
 * @post The returned string has size \c base32_encoded_size of \p in size.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto base32_encode(std::span<std::uint8_t const> const in) -> std::string {
  auto out{std::string(base32_encoded_size(in.size()), '\0')};
  out.resize(base32_encode(in, std::span<char>{out.data(), out.size()}).value_or(0));
  return out;
}

/**
 * @brief Standard RFC 4648 (section 6) Base32 decode into a buffer.
 *
 * Case-insensitive, tolerating omitted trailing padding. Size \p out with
 * \c base32_decoded_max_size.
 *
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-alphabet character, \c codec_error::incomplete_input for a
 *         truncated final group, or \c codec_error::buffer_too_small when
 *         \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c base32_decoded_max_size of
 *       \p in size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] constexpr auto base32_decode(
  std::string_view const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return base_n_decode<base32_std_spec>(in, out);
}

/**
 * @brief Heap-allocating standard Base32 decoder.
 *
 * @param in Base32-encoded string to decode.
 *
 * @return On success, the decoded bytes; on failure, the \c codec_error for the
 *         first error encountered.
 *
 * @pre None.
 * @post On success the result holds the decoded bytes of \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto base32_decode(std::string_view const in) -> codec_bytes {
  auto out{std::vector<std::uint8_t>(base32_decoded_max_size(in.size()))};
  auto const r{base32_decode(in, std::span<std::uint8_t>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

/**
 * @brief RFC 4648 (section 7) Base32hex encode into a buffer.
 *
 * Uses the extended-hex alphabet, which preserves the source byte order.
 *
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c base32_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c base32_encoded_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto base32hex_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out
) noexcept -> codec_result {
  return base_n_encode<base32_hex_spec>(in, out);
}

/**
 * @brief Heap-allocating Base32hex encoder.
 *
 * @param in Source bytes to encode.
 *
 * @return The Base32hex-encoded string, with padding.
 *
 * @pre None.
 * @post The returned string has size \c base32_encoded_size of \p in size.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto base32hex_encode(std::span<std::uint8_t const> const in) -> std::string {
  auto out{std::string(base32_encoded_size(in.size()), '\0')};
  out.resize(base32hex_encode(in, std::span<char>{out.data(), out.size()}).value_or(0));
  return out;
}

/**
 * @brief RFC 4648 (section 7) Base32hex decode into a buffer.
 *
 * Case-insensitive, tolerating omitted trailing padding. Size \p out with
 * \c base32_decoded_max_size.
 *
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-alphabet character, \c codec_error::incomplete_input for a
 *         truncated final group, or \c codec_error::buffer_too_small when
 *         \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c base32_decoded_max_size of
 *       \p in size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] constexpr auto base32hex_decode(
  std::string_view const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return base_n_decode<base32_hex_spec>(in, out);
}

/**
 * @brief Heap-allocating Base32hex decoder.
 *
 * @param in Base32hex-encoded string to decode.
 *
 * @return On success, the decoded bytes; on failure, the \c codec_error for the
 *         first error encountered.
 *
 * @pre None.
 * @post On success the result holds the decoded bytes of \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto base32hex_decode(std::string_view const in) -> codec_bytes {
  auto out{std::vector<std::uint8_t>(base32_decoded_max_size(in.size()))};
  auto const r{base32hex_decode(in, std::span<std::uint8_t>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

// base64

/**
 * @brief Exact output size for standard Base64 encoding of \p n_bytes bytes.
 *
 * @param n_bytes Number of raw bytes to encode.
 *
 * @return The number of Base64 characters produced, including padding.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto base64_encoded_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  return base_n_encoded_size<base64_std_spec>(n_bytes);
}

/**
 * @brief Exact output size for URL-safe Base64 encoding of \p n_bytes bytes.
 *
 * @param n_bytes Number of raw bytes to encode.
 *
 * @return The number of URL-safe Base64 characters produced (no padding).
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto base64url_encoded_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  return base_n_encoded_size<base64_url_spec>(n_bytes);
}

/**
 * @brief Safe upper bound on the decoded byte count for \p n_chars Base64
 *        characters.
 *
 * @param n_chars Number of Base64 characters in the input.
 *
 * @return The maximum number of bytes the decoded output can occupy.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto base64_decoded_max_size(std::size_t const n_chars
) noexcept -> std::size_t {
  return base_n_decoded_max_size<base64_std_spec>(n_chars);
}

/**
 * @brief Standard Base64 encode into a caller-provided buffer (RFC 4648 4).
 *
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c base64_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c base64_encoded_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto base64_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out
) noexcept -> codec_result {
  return base_n_encode<base64_std_spec>(in, out);
}

/**
 * @brief Heap-allocating standard Base64 encoder.
 *
 * @param in Source bytes to encode.
 *
 * @return The Base64-encoded string, with padding.
 *
 * @pre None.
 * @post The returned string has size \c base64_encoded_size of \p in size.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto base64_encode(std::span<std::uint8_t const> const in) -> std::string {
  auto out{std::string(base64_encoded_size(in.size()), '\0')};
  out.resize(base64_encode(in, std::span<char>{out.data(), out.size()}).value_or(0));
  return out;
}

/**
 * @brief URL-safe Base64 encode into a caller-provided buffer (RFC 4648 5).
 *
 * @param in Source bytes to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is shorter than \c base64url_encoded_size of \p in size.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count equals \c base64url_encoded_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] constexpr auto base64url_encode(
  std::span<std::uint8_t const> const in, std::span<char> const out
) noexcept -> codec_result {
  return base_n_encode<base64_url_spec>(in, out);
}

/**
 * @brief Heap-allocating URL-safe Base64 encoder.
 *
 * @param in Source bytes to encode.
 *
 * @return The URL-safe Base64-encoded string, without padding.
 *
 * @pre None.
 * @post The returned string has size \c base64url_encoded_size of \p in size.
 *
 * @complexity \c O(N) in the size \c N of \p in.
 */
[[nodiscard]] inline auto base64url_encode(std::span<std::uint8_t const> const in) -> std::string {
  auto out{std::string(base64url_encoded_size(in.size()), '\0')};
  out.resize(base64url_encode(in, std::span<char>{out.data(), out.size()}).value_or(0));
  return out;
}

/**
 * @brief Standard Base64 decode into a caller-provided buffer.
 *
 * ASCII whitespace is skipped and omitted trailing padding tolerated. Size
 * \p out with \c base64_decoded_max_size.
 *
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-alphabet character or padding mid-stream,
 *         \c codec_error::incomplete_input for a truncated final quad, or
 *         \c codec_error::buffer_too_small when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c base64_decoded_max_size of
 *       \p in size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] constexpr auto base64_decode(
  std::string_view const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return base_n_decode<base64_std_spec>(in, out);
}

/**
 * @brief Heap-allocating standard Base64 decoder.
 *
 * @param in Base64-encoded string to decode.
 *
 * @return On success, the decoded bytes; on failure, the \c codec_error for the
 *         first error encountered.
 *
 * @pre None.
 * @post On success the result holds the decoded bytes of \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto base64_decode(std::string_view const in) -> codec_bytes {
  auto out{std::vector<std::uint8_t>(base64_decoded_max_size(in.size()))};
  auto const r{base64_decode(in, std::span<std::uint8_t>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

/**
 * @brief URL-safe Base64 decode into a caller-provided buffer.
 *
 * Only the URL-safe alphabet is accepted. ASCII whitespace is skipped and
 * omitted trailing padding tolerated. Size \p out with
 * \c base64_decoded_max_size.
 *
 * @param in Source characters to decode.
 * @param out Destination byte buffer.
 *
 * @return The number of bytes written, or \c codec_error::invalid_input for a
 *         non-alphabet character, \c codec_error::incomplete_input for a
 *         truncated final quad, or \c codec_error::buffer_too_small when \p out
 *         is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c base64_decoded_max_size of
 *       \p in size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] constexpr auto base64url_decode(
  std::string_view const in, std::span<std::uint8_t> const out
) noexcept -> codec_result {
  return base_n_decode<base64_url_spec>(in, out);
}

/**
 * @brief Heap-allocating URL-safe Base64 decoder.
 *
 * @param in URL-safe Base64-encoded string to decode.
 *
 * @return On success, the decoded bytes; on failure, the \c codec_error for the
 *         first error encountered.
 *
 * @pre None.
 * @post On success the result holds the decoded bytes of \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto base64url_decode(std::string_view const in) -> codec_bytes {
  auto out{std::vector<std::uint8_t>(base64_decoded_max_size(in.size()))};
  auto const r{base64url_decode(in, std::span<std::uint8_t>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

}  // namespace nexenne::algorithm
