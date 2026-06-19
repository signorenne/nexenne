#pragma once

/**
 * @file
 * @brief Z-function of a string, and Z-based substring search.
 *
 * \c z[i] is the length of the longest substring starting at index \c i that
 * matches a prefix of the input, with the convention \c z[0] = |s|. It is the
 * workhorse behind prefix-based string problems: pattern search (concatenate
 * needle, a separator, and haystack, then look for \c z entries equal to the
 * needle length), period detection, and longest-common-prefix queries. The
 * Z-box algorithm computes all entries in one linear pass.
 */

#include <algorithm>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Computes the Z-function of \p s.
 *
 * Entry \c z[i] is the length of the longest substring starting at index \c i
 * that matches a prefix of \p s, with \c z[0] == |s|.
 *
 * @param s String to analyse.
 *
 * @return A vector of size \c |s| holding the Z-values; empty for empty \p s.
 *
 * @pre None.
 * @post The result has size \c s.size(); when non-empty \c z[0] == s.size() and
 *       every other \c z[i] is in \c [0, s.size() - i].
 *
 * @complexity \c O(N) time and space in the length \c N of \p s.
 */
[[nodiscard]] constexpr auto z_function(std::string_view const s) -> std::vector<std::size_t> {
  auto const n{s.size()};
  auto z{std::vector<std::size_t>(n, 0)};
  if (n == 0) {
    return z;
  }
  z[0] = n;

  auto l{std::size_t{0}};
  auto r{std::size_t{0}};
  for (auto i{std::size_t{1}}; i < n; ++i) {
    if (i < r) {
      z[i] = std::min(r - i, z[i - l]);
    }
    while (i + z[i] < n && s[z[i]] == s[i + z[i]]) {
      ++z[i];
    }
    if (i + z[i] > r) {
      l = i;
      r = i + z[i];
    }
  }
  return z;
}

/**
 * @brief Positions of every occurrence of \p needle in \p haystack.
 *
 * Builds the string \c needle + '\0' + haystack, runs the Z-function over it,
 * and collects each haystack offset whose Z-value reaches \c |needle|. Matches
 * may overlap. For repeated searches on the same haystack, prefer
 * \c aho_corasick or \c kmp_find_all.
 *
 * @param haystack Text to search.
 * @param needle Pattern to find.
 *
 * @return Ascending start indices of every occurrence of \p needle; empty when
 *         \p needle is empty or longer than \p haystack.
 *
 * @pre None.
 * @post Every returned index \c i satisfies
 *       \c haystack.substr(i, needle.size()) == needle.
 *
 * @complexity \c O(H + N) time and space, where \c H and \c N are the sizes of
 *             \p haystack and \p needle.
 */
[[nodiscard]] constexpr auto z_find_all(
  std::string_view const haystack, std::string_view const needle
) -> std::vector<std::size_t> {
  auto matches{std::vector<std::size_t>{}};
  if (needle.empty() || needle.size() > haystack.size()) {
    return matches;
  }
  auto combined{std::string{}};
  combined.reserve(needle.size() + 1 + haystack.size());
  combined.append(needle);
  combined.push_back('\0');  // a separator that caps cross-boundary matches
  combined.append(haystack);

  auto const z{z_function(combined)};
  auto const offset{needle.size() + 1};
  for (auto i{std::size_t{0}}; i < haystack.size(); ++i) {
    if (z[offset + i] >= needle.size()) {
      matches.push_back(i);
    }
  }
  return matches;
}

}  // namespace nexenne::algorithm
