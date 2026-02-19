#pragma once

/**
 * @file
 * @brief URL percent-encoding (RFC 3986) and the form-urlencoded variant.
 *
 * Two modes, chosen by which function you call. \c url_encode / \c url_decode
 * follow strict RFC 3986: only the unreserved set (A-Z, a-z, 0-9, and the four
 * characters hyphen, period, underscore, tilde) passes through, and a space
 * encodes as the percent-escape for 0x20. Use these for any URL component.
 * \c form_url_encode / \c form_url_decode are the
 * \c application/x-www-form-urlencoded variant: identical except a space maps
 * to and from a plus sign, as browsers produce for query strings and form
 * bodies.
 *
 * The encoded form is at most three times the input; the decoded form is never
 * longer than its input. All four buffered overloads are \c noexcept and return
 * \c codec_result. The decoder reports \c invalid_input for a non-hex digit
 * after a percent sign, \c incomplete_input for a percent sign without two
 * following digits, and \c buffer_too_small when the destination runs out.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>

#include <nexenne/algorithm/encoding/codec_error.hpp>

namespace nexenne::algorithm {

namespace detail {

[[nodiscard]] consteval auto make_unreserved_table() noexcept -> std::array<bool, 256> {
  auto t{std::array<bool, 256>{}};
  for (char c{'A'}; c <= 'Z'; ++c) {
    t[static_cast<std::uint8_t>(c)] = true;
  }
  for (char c{'a'}; c <= 'z'; ++c) {
    t[static_cast<std::uint8_t>(c)] = true;
  }
  for (char c{'0'}; c <= '9'; ++c) {
    t[static_cast<std::uint8_t>(c)] = true;
  }
  t[static_cast<std::uint8_t>('-')] = true;
  t[static_cast<std::uint8_t>('.')] = true;
  t[static_cast<std::uint8_t>('_')] = true;
  t[static_cast<std::uint8_t>('~')] = true;
  return t;
}

inline constexpr auto unreserved{make_unreserved_table()};
inline constexpr auto url_hex_digits{std::string_view{"0123456789ABCDEF"}};

[[nodiscard]] constexpr auto url_hex_value(char const c) noexcept -> int {
  if (c >= '0' && c <= '9') {
    return c - '0';
  }
  if (c >= 'a' && c <= 'f') {
    return c - 'a' + 10;
  }
  if (c >= 'A' && c <= 'F') {
    return c - 'A' + 10;
  }
  return -1;
}

template <bool SpaceAsPlus>
[[nodiscard]] inline auto
url_encode_into(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  auto o{std::size_t{0}};
  for (auto const c : in) {
    auto const u{static_cast<std::uint8_t>(c)};
    if (unreserved[u]) {
      if (o >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = c;
    } else if constexpr (SpaceAsPlus) {
      if (c == ' ') {
        if (o >= out.size()) {
          return std::unexpected{codec_error::buffer_too_small};
        }
        out[o++] = '+';
        continue;
      }
      if (o + 3 > out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = '%';
      out[o++] = url_hex_digits[(u >> 4) & 0x0F];
      out[o++] = url_hex_digits[u & 0x0F];
    } else {
      if (o + 3 > out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = '%';
      out[o++] = url_hex_digits[(u >> 4) & 0x0F];
      out[o++] = url_hex_digits[u & 0x0F];
    }
  }
  return o;
}

template <bool PlusAsSpace>
[[nodiscard]] inline auto
url_decode_into(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  auto o{std::size_t{0}};
  for (auto i{std::size_t{0}}; i < in.size(); ++i) {
    auto const c{in[i]};
    if (c == '%') {
      if (i + 2 >= in.size()) {
        return std::unexpected{codec_error::incomplete_input};
      }
      auto const h{url_hex_value(in[i + 1])};
      auto const l{url_hex_value(in[i + 2])};
      if (h < 0 || l < 0) {
        return std::unexpected{codec_error::invalid_input};
      }
      if (o >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = static_cast<char>((h << 4) | l);
      i += 2;
    } else if constexpr (PlusAsSpace) {
      if (o >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = (c == '+') ? ' ' : c;
    } else {
      if (o >= out.size()) {
        return std::unexpected{codec_error::buffer_too_small};
      }
      out[o++] = c;
    }
  }
  return o;
}

}  // namespace detail

/**
 * @brief Upper bound on the percent-encoded length of \p n_bytes input bytes.
 *
 * A worst-case byte expands to a three-character escape, so the encoded form
 * never exceeds three times the input. Size an output buffer with this before
 * calling \c url_encode or \c form_url_encode.
 *
 * @param n_bytes Length in bytes of the input to encode.
 *
 * @return The maximum number of characters the encoded form can occupy.
 *
 * @pre \p n_bytes is small enough that \c n_bytes * 3 does not overflow.
 * @post The result is at least the exact encoded length.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto url_encoded_max_size(std::size_t const n_bytes
) noexcept -> std::size_t {
  return n_bytes * 3;
}

/**
 * @brief Strict RFC 3986 percent-encode into a caller-provided buffer.
 *
 * Percent-encodes every character of \p in outside the unreserved set; a space
 * becomes the escape for 0x20. No allocation. Size \p out with
 * \c url_encoded_max_size.
 *
 * @param in Source characters to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c url_encoded_max_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto
url_encode(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  return detail::url_encode_into<false>(in, out);
}

/**
 * @brief Heap-allocating strict RFC 3986 percent-encoder.
 *
 * @param in Source string to encode.
 *
 * @return The percent-encoded string.
 *
 * @pre None.
 * @post The returned string contains no character that RFC 3986 reserves.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto url_encode(std::string_view const in) -> std::string {
  auto out{std::string(url_encoded_max_size(in.size()), '\0')};
  auto const r{url_encode(in, std::span<char>{out.data(), out.size()})};
  out.resize(r.value_or(0));
  return out;
}

/**
 * @brief Strict RFC 3986 percent-decode into a caller-provided buffer.
 *
 * Decodes percent-escapes from \p in into \p out; a plus sign is taken
 * literally. No allocation. An \p out as large as \p in always suffices.
 *
 * @param in Source characters to decode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::invalid_input
 *         for a malformed escape, \c codec_error::incomplete_input for an
 *         escape truncated at end of input, or \c codec_error::buffer_too_small
 *         when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \p in size; on failure \p out
 *       is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto
url_decode(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  return detail::url_decode_into<false>(in, out);
}

/**
 * @brief Heap-allocating strict RFC 3986 percent-decoder.
 *
 * @param in Percent-encoded string to decode.
 *
 * @return On success, the decoded string; on failure, the \c codec_error for
 *         the first error encountered.
 *
 * @pre None.
 * @post On success the returned string holds the decoded characters of \p in.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto url_decode(std::string_view const in) -> codec_string {
  auto out{std::string(in.size(), '\0')};
  auto const r{url_decode(in, std::span<char>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

/**
 * @brief Form-urlencoded encode into a caller-provided buffer.
 *
 * Like \c url_encode, but a space becomes a plus sign, matching the
 * \c application/x-www-form-urlencoded serialization. No allocation. Size
 * \p out with \c url_encoded_max_size.
 *
 * @param in Source characters to encode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::buffer_too_small
 *         when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \c url_encoded_max_size of \p in
 *       size; on failure \p out is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto
form_url_encode(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  return detail::url_encode_into<true>(in, out);
}

/**
 * @brief Heap-allocating form-urlencoded encoder.
 *
 * @param in Source string to encode.
 *
 * @return The form-urlencoded string, using a plus sign for spaces.
 *
 * @pre None.
 * @post The returned string uses a plus sign for spaces and percent-escapes for
 *       all other characters needing it.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto form_url_encode(std::string_view const in) -> std::string {
  auto out{std::string(url_encoded_max_size(in.size()), '\0')};
  auto const r{form_url_encode(in, std::span<char>{out.data(), out.size()})};
  out.resize(r.value_or(0));
  return out;
}

/**
 * @brief Form-urlencoded decode into a caller-provided buffer.
 *
 * Like \c url_decode, but a plus sign decodes to a space. No allocation. An
 * \p out as large as \p in always suffices.
 *
 * @param in Source characters to decode.
 * @param out Destination character buffer.
 *
 * @return The number of characters written, or \c codec_error::invalid_input
 *         for a malformed escape, \c codec_error::incomplete_input for an
 *         escape truncated at end of input, or \c codec_error::buffer_too_small
 *         when \p out is exhausted.
 *
 * @pre \p in and \p out do not overlap.
 * @post On success the written count is at most \p in size; on failure \p out
 *       is left unspecified.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto
form_url_decode(std::string_view const in, std::span<char> const out) noexcept -> codec_result {
  return detail::url_decode_into<true>(in, out);
}

/**
 * @brief Heap-allocating form-urlencoded decoder.
 *
 * @param in Form-urlencoded string to decode.
 *
 * @return On success, the decoded string; on failure, the \c codec_error for
 *         the first error encountered.
 *
 * @pre None.
 * @post On success the returned string has plus signs expanded to spaces and
 *       percent-escapes decoded.
 *
 * @complexity \c O(N) in the length \c N of \p in.
 */
[[nodiscard]] inline auto form_url_decode(std::string_view const in) -> codec_string {
  auto out{std::string(in.size(), '\0')};
  auto const r{form_url_decode(in, std::span<char>{out.data(), out.size()})};
  if (!r.has_value()) {
    return std::unexpected{r.error()};
  }
  out.resize(*r);
  return out;
}

}  // namespace nexenne::algorithm
