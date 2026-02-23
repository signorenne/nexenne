#pragma once

/**
 * @file
 * @brief Modular-sum checksums: the Adler and Fletcher family.
 *
 * A modular-sum checksum keeps two running sums modulo \c M: \c sum1 adds each
 * input unit, \c sum2 adds each \c sum1, and the result is
 * \c (sum2 << width) | sum1. The family members differ only in the input unit
 * size, the sum width, and the modulus, so they are one \c modular_sum_spec
 * apart, exactly as the CRC variants are one \c crc_spec apart.
 *
 * Adler-32 is the prime-modulus member (modulus 65521), used by zlib and PNG;
 * Fletcher-16/32/64 use the composite moduli 2^8-1, 2^16-1, and 2^32-1 over
 * 1-, 2-, and 4-byte units. Multi-byte units are assembled little-endian, with
 * the final partial unit zero-padded, matching the published vectors. The
 * modulus is deferred to the end of each block of \c detail::modular_sum_block
 * units, which is faster than a per-unit modulo and gives an identical result.
 *
 * Not cryptographically secure.
 */

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>

namespace nexenne::algorithm {

namespace detail {

// The unsigned result type holding two sums of SumBits each: 2*SumBits bits.
template <std::size_t SumBits>
using modsum_uint_t = std::conditional_t<
  SumBits <= 8,
  std::uint16_t,
  std::conditional_t<SumBits <= 16, std::uint32_t, std::uint64_t>>;

// Units summed before each modulo. Safe for every family member with a 64-bit
// accumulator: with the largest unit (2^32-1) and modulus (2^32-1), the worst
// sum2 after a block stays near 1.3e17, well below 2^64.
inline constexpr std::size_t modular_sum_block{5552};

}  // namespace detail

/// @brief The unsigned result type of \c modular_sum at the given \c SumBits.
template <std::size_t SumBits>
using modular_sum_result_t = detail::modsum_uint_t<SumBits>;

/**
 * @brief Compile-time description of one modular-sum checksum.
 *
 * Pass as a non-type template parameter to \c modular_sum. The result type is
 * \c modular_sum_result_t of \c sum_bits (twice as wide as a single sum).
 */
struct modular_sum_spec {
  std::size_t unit_bytes{1};  ///< Bytes per input unit (1, 2, or 4), little-endian.
  std::size_t sum_bits{16};   ///< Width of each running sum; output is twice this.
  std::uint64_t modulus{};    ///< Modulus applied to both sums.
  std::uint64_t init1{};      ///< Initial value of \c sum1 (1 for Adler, 0 for Fletcher).
};

/**
 * @brief Computes the modular-sum checksum of a byte span under \p Spec.
 *
 * Assembles each input unit little-endian (zero-padding a final partial unit),
 * folds it into the two sums modulo \c Spec.modulus, and returns
 * \c (sum2 << Spec.sum_bits) | sum1. The modulo is deferred to block boundaries.
 *
 * @tparam Spec One of the named presets below, or a custom \c modular_sum_spec.
 * @param bytes Bytes to checksum. An empty span yields \p seed reduced.
 * @param seed Initial checksum value; pass the previous result to continue a
 *        checksum across calls. Defaults to the preset's initial value. For a
 *        multi-byte unit, continuation is only well-defined when the earlier
 *        input was a whole number of units.
 *
 * @return The checksum of \p bytes given \p seed.
 *
 * @pre None.
 * @post Equal inputs under the same \p seed always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
template <modular_sum_spec Spec>
[[nodiscard]] constexpr auto modular_sum(
  std::span<std::uint8_t const> const bytes,
  modular_sum_result_t<Spec.sum_bits> const seed =
    static_cast<modular_sum_result_t<Spec.sum_bits>>(Spec.init1)
) noexcept -> modular_sum_result_t<Spec.sum_bits> {
  using value_type = modular_sum_result_t<Spec.sum_bits>;
  constexpr auto w{Spec.sum_bits};
  constexpr auto m{Spec.modulus};
  constexpr auto unit{Spec.unit_bytes};
  constexpr auto low_mask{(std::uint64_t{1} << w) - 1};

  auto sum1{(static_cast<std::uint64_t>(seed) & low_mask) % m};
  auto sum2{((static_cast<std::uint64_t>(seed) >> w) & low_mask) % m};

  auto const n{bytes.size()};
  auto i{std::size_t{0}};
  auto since{std::size_t{0}};
  while (i < n) {
    auto u{std::uint64_t{0}};
    for (auto j{std::size_t{0}}; j < unit; ++j) {
      if (i + j < n) {
        u |= static_cast<std::uint64_t>(bytes[i + j]) << (8 * j);
      }
    }
    i += unit;
    sum1 += u;
    sum2 += sum1;
    since += 1;
    if (since == detail::modular_sum_block) {
      sum1 %= m;
      sum2 %= m;
      since = 0;
    }
  }
  sum1 %= m;
  sum2 %= m;
  return static_cast<value_type>((sum2 << w) | sum1);
}

/**
 * @brief Computes the modular-sum checksum of a string view under \p Spec.
 *
 * Reinterprets the characters of \p s as bytes and forwards to the byte-span
 * overload.
 *
 * @tparam Spec One of the named presets below, or a custom \c modular_sum_spec.
 * @param s Characters to checksum.
 * @param seed Initial checksum value; defaults to the preset's initial value.
 *
 * @return The checksum of \p s given \p seed.
 *
 * @pre None.
 * @post Equal inputs under the same \p seed always produce the same value, and
 *       the result matches the byte-span overload over the same bytes.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 *
 * @warning Not cryptographically secure.
 */
template <modular_sum_spec Spec>
[[nodiscard]] inline auto modular_sum(
  std::string_view const s,
  modular_sum_result_t<Spec.sum_bits> const seed =
    static_cast<modular_sum_result_t<Spec.sum_bits>>(Spec.init1)
) noexcept -> modular_sum_result_t<Spec.sum_bits> {
  return modular_sum<Spec>(
    std::span<std::uint8_t const>{reinterpret_cast<std::uint8_t const*>(s.data()), s.size()}, seed
  );
}

/// @brief Adler-32 (zlib, PNG): byte units, two 16-bit sums modulo 65521.
inline constexpr modular_sum_spec adler32_spec{
  .unit_bytes = 1, .sum_bits = 16, .modulus = 65521u, .init1 = 1u
};
/// @brief Fletcher-16: byte units, two 8-bit sums modulo 255.
inline constexpr modular_sum_spec fletcher16_spec{
  .unit_bytes = 1, .sum_bits = 8, .modulus = 255u, .init1 = 0u
};
/// @brief Fletcher-32: 16-bit little-endian units, two 16-bit sums modulo 65535.
inline constexpr modular_sum_spec fletcher32_spec{
  .unit_bytes = 2, .sum_bits = 16, .modulus = 65535u, .init1 = 0u
};
/// @brief Fletcher-64: 32-bit little-endian units, two 32-bit sums modulo 2^32-1.
inline constexpr modular_sum_spec fletcher64_spec{
  .unit_bytes = 4, .sum_bits = 32, .modulus = 4294967295u, .init1 = 0u
};

/**
 * @brief Adler-32 checksum of a byte span (zlib, PNG).
 *
 * @param bytes Bytes to checksum.
 * @param seed Initial value; pass a previous result to continue. Defaults to 1.
 *
 * @return The Adler-32 checksum of \p bytes.
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
  return modular_sum<adler32_spec>(bytes, seed);
}

/**
 * @brief Adler-32 checksum of a string view (zlib, PNG).
 *
 * @param s Characters to checksum.
 * @param seed Initial value; pass a previous result to continue. Defaults to 1.
 *
 * @return The Adler-32 checksum of \p s.
 *
 * @pre None.
 * @post Equal inputs under the same \p seed always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] inline auto
adler32(std::string_view const s, std::uint32_t const seed = 1u) noexcept -> std::uint32_t {
  return modular_sum<adler32_spec>(s, seed);
}

/**
 * @brief Fletcher-16 checksum of a byte span.
 *
 * @param bytes Bytes to checksum.
 *
 * @return The Fletcher-16 checksum of \p bytes.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto fletcher16(std::span<std::uint8_t const> const bytes
) noexcept -> std::uint16_t {
  return modular_sum<fletcher16_spec>(bytes);
}

/**
 * @brief Fletcher-16 checksum of a string view.
 *
 * @param s Characters to checksum.
 *
 * @return The Fletcher-16 checksum of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] inline auto fletcher16(std::string_view const s) noexcept -> std::uint16_t {
  return modular_sum<fletcher16_spec>(s);
}

/**
 * @brief Fletcher-32 checksum of a byte span (16-bit little-endian units).
 *
 * @param bytes Bytes to checksum; a final odd byte is zero-padded to a unit.
 *
 * @return The Fletcher-32 checksum of \p bytes.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto fletcher32(std::span<std::uint8_t const> const bytes
) noexcept -> std::uint32_t {
  return modular_sum<fletcher32_spec>(bytes);
}

/**
 * @brief Fletcher-32 checksum of a string view (16-bit little-endian units).
 *
 * @param s Characters to checksum.
 *
 * @return The Fletcher-32 checksum of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] inline auto fletcher32(std::string_view const s) noexcept -> std::uint32_t {
  return modular_sum<fletcher32_spec>(s);
}

/**
 * @brief Fletcher-64 checksum of a byte span (32-bit little-endian units).
 *
 * @param bytes Bytes to checksum; a final partial unit is zero-padded.
 *
 * @return The Fletcher-64 checksum of \p bytes.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the size \c N of \p bytes.
 *
 * @warning Not cryptographically secure.
 */
[[nodiscard]] constexpr auto fletcher64(std::span<std::uint8_t const> const bytes
) noexcept -> std::uint64_t {
  return modular_sum<fletcher64_spec>(bytes);
}

/**
 * @brief Fletcher-64 checksum of a string view (32-bit little-endian units).
 *
 * @param s Characters to checksum.
 *
 * @return The Fletcher-64 checksum of \p s.
 *
 * @pre None.
 * @post Equal inputs always produce the same value.
 *
 * @complexity \c O(N) in the length \c N of \p s.
 */
[[nodiscard]] inline auto fletcher64(std::string_view const s) noexcept -> std::uint64_t {
  return modular_sum<fletcher64_spec>(s);
}

}  // namespace nexenne::algorithm
