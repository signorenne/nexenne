#pragma once

/**
 * @file
 * @brief The scalar types the binary format reads and writes.
 *
 * A wire value must take the same number of bytes on every platform, or a
 * stream written on one ABI fails to read on another. This header names the
 * accepted set once, for both \c writer and \c reader.
 */

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace nexenne::serialization::binary {

/// @cond INTERNAL
namespace detail {

/**
 * @brief Whether the cv-unqualified \c T is one of the fixed-width scalars.
 *
 * @tparam T Candidate type, already stripped of cv-qualifiers.
 */
template <typename T>
concept fixed_width_unqualified =
  std::same_as<T, bool> || std::same_as<T, char> || std::same_as<T, char8_t>
  || std::same_as<T, std::int8_t> || std::same_as<T, std::uint8_t> || std::same_as<T, std::int16_t>
  || std::same_as<T, std::uint16_t> || std::same_as<T, std::int32_t>
  || std::same_as<T, std::uint32_t> || std::same_as<T, std::int64_t>
  || std::same_as<T, std::uint64_t> || std::same_as<T, float> || std::same_as<T, double>;

}  // namespace detail

/// @endcond

/**
 * @brief Whether \c T is a scalar the binary format encodes at one width everywhere.
 *
 * Accepts the exact-width integers \c std::int8_t to \c std::uint64_t,
 * \c bool, \c char, \c char8_t, \c float and \c double, with any
 * cv-qualifiers. Rejects \c wchar_t, \c char16_t, \c char32_t,
 * \c long \c double, and every integer type that is not one of the exact-width
 * aliases on the target.
 *
 * The test is type identity, so a type that is an alias on the target passes:
 * where \c long is \c std::int64_t (LP64 Linux) a \c long is accepted as that
 * alias, and where \c std::int32_t is \c long (arm-none-eabi) a plain \c int is
 * rejected. Spell wire fields with the \c cstdint aliases and the code compiles,
 * and encodes the same bytes, on every target.
 *
 * @tparam T Candidate scalar type.
 */
template <typename T>
concept fixed_width_scalar = detail::fixed_width_unqualified<std::remove_cv_t<T>>;

}  // namespace nexenne::serialization::binary
