#pragma once

/**
 * @file
 * @brief Boyer-Moore-Horspool string search.
 *
 * Compares each window right to left against the needle and, on any mismatch,
 * shifts by the entry of a 256-entry table keyed on the byte currently aligned
 * with the LAST needle position, regardless of where the mismatch occurred. This
 * is Horspool's simplification of Boyer-Moore: one cache-friendly array, no
 * good-suffix table, and a shift that is always at least 1. On large alphabets
 * and typical text it beats KMP in practice, often sublinear in the haystack
 * length; the trade-off is a quadratic worst case on small alphabets (e.g.
 * needle "aaaa" in "aaaa...a"), so use \c kmp_find when worst-case linearity is
 * required. An empty needle matches at position 0.
 *
 * @see R. N. Horspool, "Practical fast searching in strings", Software:
 *      Practice and Experience 10(6), 1980.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexenne::algorithm {

/**
 * @brief Index of the first occurrence of \p needle in \p haystack.
 *
 * Uses Boyer-Moore-Horspool: on a mismatch it shifts by the table entry for the
 * haystack byte aligned with the last needle position, so a byte absent from the
 * needle skips the whole needle length at once. An empty \p needle matches at
 * position 0.
 *
 * @param haystack Text to search.
 * @param needle Pattern to find.
 *
 * @return Index of the first occurrence, or \c std::string_view::npos when
 *         \p needle does not occur.
 *
 * @pre None.
 * @post The returned index, when not \c npos, satisfies
 *       \c haystack.substr(index, needle.size()) == needle.
 *
 * @complexity Sublinear on typical text; \c O(H * N) worst case on small
 *             alphabets, where \c H and \c N are the sizes of \p haystack and
 *             \p needle. Setup is \c O(N + 256).
 */
[[nodiscard]] constexpr auto
boyer_moore_find(std::string_view const haystack, std::string_view const needle) noexcept
  -> std::size_t {
  auto const n{haystack.size()};
  auto const m{needle.size()};
  if (m == 0) {
    return 0;
  }
  if (m > n) {
    return std::string_view::npos;
  }

  // Horspool table: skip[c] is the distance from the last needle position back
  // to the rightmost earlier occurrence of byte c in the needle, or the needle
  // length when c does not occur before the last position. The final needle byte
  // is excluded from the table (loop stops at m - 1) so a mismatch there still
  // shifts by at least 1.
  auto skip{std::array<std::size_t, 256>{}};
  skip.fill(m);
  for (auto i{std::size_t{0}}; i + 1 < m; ++i) {
    skip[static_cast<std::uint8_t>(needle[i])] = m - 1 - i;
  }

  auto i{std::size_t{0}};
  while (i <= n - m) {
    auto j{m};
    while (j > 0 && needle[j - 1] == haystack[i + j - 1]) {
      j -= 1;
    }
    if (j == 0) {
      return i;
    }
    i += skip[static_cast<std::uint8_t>(haystack[i + m - 1])];
  }
  return std::string_view::npos;
}

}  // namespace nexenne::algorithm
