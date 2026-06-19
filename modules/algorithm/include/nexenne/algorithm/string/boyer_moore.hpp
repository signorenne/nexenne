#pragma once

/**
 * @file
 * @brief Boyer-Moore string search (bad-character heuristic).
 *
 * Scans the haystack right to left against the needle, using a 256-entry
 * bad-character shift table so a mismatching byte skips several positions at
 * once. On large alphabets and typical text this beats KMP in practice, often
 * sublinear in the haystack length. The good-suffix heuristic is deliberately
 * omitted: the bad-character table alone covers the common case and keeps setup
 * to one cache-friendly array. The trade-off is a quadratic worst case on small
 * alphabets (e.g. needle "aaaa" in "aaaa...a"); use \c kmp_find when worst-case
 * linearity is required. An empty needle matches at position 0.
 */

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace nexenne::algorithm {

/**
 * @brief Index of the first occurrence of \p needle in \p haystack.
 *
 * Uses Boyer-Moore with the bad-character heuristic: a 256-entry shift table
 * lets a mismatching byte skip several positions at once. An empty \p needle
 * matches at position 0.
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
[[nodiscard]] constexpr auto boyer_moore_find(
  std::string_view const haystack, std::string_view const needle
) noexcept -> std::size_t {
  auto const n{haystack.size()};
  auto const m{needle.size()};
  if (m == 0) {
    return 0;
  }
  if (m > n) {
    return std::string_view::npos;
  }

  // Bad-character table: skip[c] is the shift distance on a mismatch against
  // byte c, the needle length by default and shorter for bytes in the needle.
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
