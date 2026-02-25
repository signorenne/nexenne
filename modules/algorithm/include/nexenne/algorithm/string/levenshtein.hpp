#pragma once

/**
 * @file
 * @brief Levenshtein edit distance between two strings.
 *
 * The minimum number of single-character insertions, deletions, or
 * substitutions that turn one string into the other: the basis of fuzzy search,
 * spell-check candidates, name deduplication, and typo-tolerant routing. Uses
 * the two-row dynamic-programming variant, so the working memory is
 * \c O(min(|a|, |b|)) rather than the full \c O(|a| * |b|) matrix (which is only
 * needed to reconstruct the edit script).
 */

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <utility>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Levenshtein edit distance between \p a and \p b.
 *
 * Counts the minimum number of single-character insertions, deletions, and
 * substitutions that turn \p a into \p b, using a two-row table whose width is
 * the shorter input.
 *
 * @param a First string.
 * @param b Second string.
 *
 * @return The edit distance. Zero for identical inputs; \c max(|a|, |b|) for
 *         inputs that share no characters in common positions.
 *
 * @pre None.
 * @post The result is in \c [abs(|a| - |b|), max(|a|, |b|)] and is symmetric in
 *       \p a and \p b.
 *
 * @complexity \c O(|a| * |b|) time and \c O(min(|a|, |b|)) auxiliary space.
 */
[[nodiscard]] constexpr auto levenshtein(std::string_view a, std::string_view b) -> std::size_t {
  // Force the shorter string onto the row axis so the rows stay at
  // O(min(|a|, |b|)).
  if (b.size() < a.size()) {
    std::swap(a, b);
  }
  auto const m{a.size()};
  auto const n{b.size()};
  if (m == 0) {
    return n;
  }

  auto prev{std::vector<std::size_t>(m + 1)};
  auto curr{std::vector<std::size_t>(m + 1)};
  for (auto i{std::size_t{0}}; i <= m; ++i) {
    prev[i] = i;
  }

  for (auto j{std::size_t{1}}; j <= n; ++j) {
    curr[0] = j;
    for (auto i{std::size_t{1}}; i <= m; ++i) {
      auto const cost{a[i - 1] == b[j - 1] ? std::size_t{0} : std::size_t{1}};
      curr[i] = std::min({
        curr[i - 1] + 1,     // insertion
        prev[i] + 1,         // deletion
        prev[i - 1] + cost,  // substitution or match
      });
    }
    std::swap(prev, curr);
  }
  return prev[m];
}

}  // namespace nexenne::algorithm
