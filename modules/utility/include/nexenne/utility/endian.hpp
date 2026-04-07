#pragma once

/**
 * @file
 * @brief Read and write unsigned integers to byte spans in a fixed byte order.
 */

#include <bit>
#include <concepts>
#include <cstddef>
#include <span>

namespace nexenne::utility {

/**
 * @brief Writes \p value into \p out as big-endian (most significant byte first).
 *
 * The destination is a fixed-extent span, so the byte count is checked by the
 * type: \p out must be exactly \c sizeof(U) bytes and \p U is deduced from
 * \p value. The implementation is plain shifts, which every compiler lowers to a
 * single store (and a \c BSWAP / \c REV on a little-endian host), and it is
 * \c constexpr so it also runs at compile time.
 *
 * @tparam U Unsigned integer type to encode, deduced from \p value.
 * @param out Destination of exactly \c sizeof(U) bytes.
 * @param value Value to encode.
 *
 * @pre None.
 * @post \p out holds \p value in big-endian order.
 */
template <std::unsigned_integral U>
constexpr auto write_be(std::span<std::byte, sizeof(U)> const out, U const value) noexcept -> void {
  for (auto i{std::size_t{0}}; i < sizeof(U); ++i) {
    out[sizeof(U) - 1U - i] = static_cast<std::byte>(static_cast<unsigned char>(value >> (8U * i)));
  }
}

/**
 * @brief Reads a big-endian unsigned integer from \p in.
 *
 * The source is a fixed-extent span of exactly \c sizeof(U) bytes, so the read
 * cannot run off the end. \p U is given explicitly.
 *
 * @tparam U Unsigned integer type to decode.
 * @param in Source of exactly \c sizeof(U) bytes.
 *
 * @return The decoded value.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] constexpr auto read_be(std::span<std::byte const, sizeof(U)> const in) noexcept -> U {
  auto value{U{0}};
  for (auto i{std::size_t{0}}; i < sizeof(U); ++i) {
    value = static_cast<U>(static_cast<U>(value << 8U) | std::to_integer<U>(in[i]));
  }
  return value;
}

/**
 * @brief Writes \p value into \p out as little-endian (least significant byte
 *        first).
 *
 * The mirror of \c write_be: \p out is a fixed-extent span of exactly
 * \c sizeof(U) bytes and \p U is deduced from \p value.
 *
 * @tparam U Unsigned integer type to encode, deduced from \p value.
 * @param out Destination of exactly \c sizeof(U) bytes.
 * @param value Value to encode.
 *
 * @pre None.
 * @post \p out holds \p value in little-endian order.
 */
template <std::unsigned_integral U>
constexpr auto write_le(std::span<std::byte, sizeof(U)> const out, U const value) noexcept -> void {
  for (auto i{std::size_t{0}}; i < sizeof(U); ++i) {
    out[i] = static_cast<std::byte>(static_cast<unsigned char>(value >> (8U * i)));
  }
}

/**
 * @brief Reads a little-endian unsigned integer from \p in.
 *
 * @tparam U Unsigned integer type to decode.
 * @param in Source of exactly \c sizeof(U) bytes.
 *
 * @return The decoded value.
 *
 * @pre None.
 * @post None.
 */
template <std::unsigned_integral U>
[[nodiscard]] constexpr auto read_le(std::span<std::byte const, sizeof(U)> const in) noexcept -> U {
  auto value{U{0}};
  for (auto i{std::size_t{0}}; i < sizeof(U); ++i) {
    value = static_cast<U>(value | static_cast<U>(std::to_integer<U>(in[i]) << (8U * i)));
  }
  return value;
}

}  // namespace nexenne::utility
