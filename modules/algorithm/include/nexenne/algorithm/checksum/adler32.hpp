#pragma once

/**
 * @file
 * @brief Adler-32, zlib's lightweight integrity check.
 *
 * Cheaper to compute than CRC-32 (no lookup table, two additions per byte) but
 * with weaker error detection on short messages. It maintains two 16-bit sums
 * modulo 65521 (the largest prime below 2^16) and returns their concatenation
 * \c (b << 16) | a. The \p seed parameter lets a checksum be continued across
 * chunked input by feeding back the previous result.
 *
 * Reach for Adler-32 when you want a fast running checksum over non-adversarial
 * data; for storage or wire formats prefer \c crc (CRC-32 detects more burst
 * error patterns). Not cryptographically secure.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace nexenne::algorithm {

namespace detail {

inline constexpr auto adler32_mod{std::uint32_t{65521u}};

/// @brief Bytes summed before each modulo, the largest count that keeps the
///        \c b accumulator below 2^32 (zlib's NMAX).
inline constexpr auto adler32_block{std::size_t{5552u}};

}  // namespace detail

/**
 * @brief Computes the Adler-32 checksum of a byte span.
 *
 * Maintains two accumulators \c a and \c b modulo 65521 and returns
 * \c (b << 16) | a. The modulo is deferred to the end of each block of
 * \c detail::adler32_block bytes rather than taken per byte, which is the
 * standard zlib speed-up and produces an identical result.
 *
 * @param bytes Bytes to checksum. An empty span yields \p seed reduced.
 * @param seed Initial checksum value; pass the previous result to continue a
 *        checksum across calls. Defaults to \c 1, the standard initial value.
 *
 * @return The Adler-32 checksum of \p bytes given \p seed.
 *
 * @pre None.
 * @post Equal inputs under the same \p seed always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto adler32(
  std::span<std::uint8_t const> const bytes, std::uint32_t const seed = 1u
) noexcept -> std::uint32_t {
  auto a{(seed & 0xFFFFu) % detail::adler32_mod};
  auto b{((seed >> 16u) & 0xFFFFu) % detail::adler32_mod};

  auto remaining{bytes.size()};
  auto const* p{bytes.data()};
  while (remaining > 0) {
    auto const block{remaining < detail::adler32_block ? remaining : detail::adler32_block};
    for (auto i{std::size_t{0}}; i < block; ++i) {
      a += p[i];
      b += a;
    }
    a %= detail::adler32_mod;
    b %= detail::adler32_mod;
    p += block;
    remaining -= block;
  }

  return (b << 16u) | a;
}

/**
 * @brief Computes the Adler-32 checksum of a string view.
 *
 * Reinterprets the characters of \p s as bytes and forwards to the byte-span
 * overload.
 *
 * @param s Characters to checksum. An empty view yields \p seed reduced.
 * @param seed Initial checksum value; defaults to \c 1.
 *
 * @return The Adler-32 checksum of \p s given \p seed.
 *
 * @pre None.
 * @post Equal inputs under the same \p seed always produce the same value, and
 *       the result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] inline auto
adler32(std::string_view const s, std::uint32_t const seed = 1u) noexcept -> std::uint32_t {
  return adler32(
    std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}, seed
  );
}

}  // namespace nexenne::algorithm
