#pragma once

/**
 * @file
 * @brief Compile-time alphabet primitive shared by the symbol-based codecs.
 *
 * A \c codec_alphabet wraps a string-literal alphabet of \c N distinct
 * characters and builds, at compile time, a forward table (index to character)
 * and a 256-entry reverse table (character to index, with -1 for non-members).
 * Base64, base32, and base16 share it for their alphabet handling: construct
 * one as an \c inline \c constexpr value and pass it as a non-type template
 * parameter to the codec. A deduction guide infers \c N from the literal, and
 * \c is_distinct lets a \c static_assert reject an alphabet with duplicates.
 */

#include <array>
#include <cstddef>
#include <cstdint>

namespace nexenne::algorithm {

/**
 * @brief A compile-time alphabet of \c N distinct characters with forward and
 *        reverse lookup tables.
 *
 * @tparam N Number of symbols in the alphabet.
 */
template <std::size_t N>
struct codec_alphabet {
  static constexpr std::size_t size{N};  ///< Number of symbols in the alphabet.

  std::array<char, N> chars{};  ///< Forward table: index to character.
  std::array<std::int8_t, 256> reverse{
  };  ///< Reverse table: character to index, -1 for non-members.

  /**
   * @brief Builds the alphabet from a string literal of length \c N.
   *
   * Fills \c chars with the literal and builds \c reverse so that
   * \c reverse[c] is the index of \c c, or -1 when \c c is not a member.
   *
   * @param literal Null-terminated string of exactly \c N distinct ASCII
   *        characters; the null terminator is not included.
   *
   * @pre \p literal has exactly \c N distinct characters; duplicates produce an
   *      inconsistent reverse table (catch them with \c is_distinct).
   * @post \c chars holds the first \c N characters of \p literal and \c reverse
   *       is their inverse mapping.
   */
  consteval codec_alphabet(char const (&literal)[N + 1]) noexcept {
    for (auto& v : reverse) {
      v = -1;
    }
    for (auto i{std::size_t{0}}; i < N; ++i) {
      chars[i] = literal[i];
      reverse[static_cast<std::uint8_t>(literal[i])] = static_cast<std::int8_t>(i);
    }
  }

  /**
   * @brief Forward lookup: the character at position \p idx.
   *
   * @param idx Symbol index.
   *
   * @return The character at \p idx.
   *
   * @pre \p idx < N.
   * @post None.
   */
  [[nodiscard]] constexpr auto encode(std::size_t const idx) const noexcept -> char {
    return chars[idx];
  }

  /**
   * @brief Reverse lookup: the index of character \p c.
   *
   * @param c Character to look up.
   *
   * @return The zero-based index of \p c, or -1 when \p c is not a member.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto decode(char const c) const noexcept -> std::int8_t {
    return reverse[static_cast<std::uint8_t>(c)];
  }

  /**
   * @brief Compile-time check that every character in \c chars is distinct.
   *
   * @return \c true when no character repeats, \c false otherwise.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] consteval auto is_distinct() const noexcept -> bool {
    auto seen{std::array<bool, 256>{}};
    for (auto const c : chars) {
      auto const u{static_cast<std::uint8_t>(c)};
      if (seen[u]) {
        return false;
      }
      seen[u] = true;
    }
    return true;
  }
};

// Deduction guide: infer N from the literal so users write codec_alphabet{"..."}.
template <std::size_t M>
codec_alphabet(char const (&)[M]) -> codec_alphabet<M - 1>;

}  // namespace nexenne::algorithm
