#pragma once

/**
 * @file
 * @brief Suffix array construction (prefix doubling) and the LCP array.
 *
 * The suffix array \c sa[0..N-1] gives the start offsets of the suffixes of a
 * string in lexicographic order; it underpins substring search (binary search
 * within the sorted suffixes), longest-common-substring queries, and the
 * Burrows-Wheeler transform. Construction is by prefix doubling: sort the
 * suffixes by their first character, then by the first 2, 4, 8, ... characters,
 * which is \c O(N log^2 N) and far simpler than the linear SA-IS while staying
 * fast for strings up to a few megabytes. \c build_lcp adds the
 * longest-common-prefix array in \c O(N) by Kasai's algorithm, needed by most
 * suffix-array queries.
 */

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <span>
#include <string_view>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Builds the suffix array of \p text by prefix doubling.
 *
 * Produces \c sa[0..N-1] where \c sa[i] is the start offset of the i-th
 * smallest suffix of \p text. Suffixes are ranked by their first character,
 * then sorted by progressively doubled prefix lengths until all ranks differ.
 * Bytes are compared as \c unsigned \c char.
 *
 * @param text Text whose suffixes are ranked.
 *
 * @return A vector of size \c text.size() holding suffix start offsets in
 *         ascending suffix order; empty for empty \p text.
 *
 * @pre \c text.size() fits in \c std::int32_t.
 * @post The result has size \c text.size() and is a permutation of
 *       \c [0, text.size()) ordering the suffixes lexicographically.
 *
 * @complexity \c O(N log^2 N) time and \c O(N) auxiliary space in the length
 *             \c N of \p text.
 */
[[nodiscard]] inline auto build_suffix_array(std::string_view const text
) -> std::vector<std::int32_t> {
  auto const n{static_cast<std::int32_t>(text.size())};
  auto sa{std::vector<std::int32_t>(static_cast<std::size_t>(n))};
  auto rank{std::vector<std::int32_t>(static_cast<std::size_t>(n))};
  auto tmp{std::vector<std::int32_t>(static_cast<std::size_t>(n))};

  std::iota(sa.begin(), sa.end(), 0);
  for (auto i{std::int32_t{0}}; i < n; ++i) {
    auto const u{static_cast<std::size_t>(i)};
    rank[u] = static_cast<std::int32_t>(static_cast<unsigned char>(text[u]));
  }

  // k is 64-bit so the k *= 2 doubling cannot overflow when n approaches the
  // int32 limit (k would otherwise reach 2^31 as a signed int32).
  for (auto k{std::int64_t{1}}; k < n; k *= 2) {
    auto const cmp{[&](std::int32_t const a, std::int32_t const b) {
      auto const ua{static_cast<std::size_t>(a)};
      auto const ub{static_cast<std::size_t>(b)};
      if (rank[ua] != rank[ub]) {
        return rank[ua] < rank[ub];
      }
      auto const ra{(a + k < n) ? rank[static_cast<std::size_t>(a + k)] : -1};
      auto const rb{(b + k < n) ? rank[static_cast<std::size_t>(b + k)] : -1};
      return ra < rb;
    }};
    std::ranges::sort(sa, cmp);
    tmp[static_cast<std::size_t>(sa[0])] = 0;
    for (auto i{std::int32_t{1}}; i < n; ++i) {
      auto const prev{sa[static_cast<std::size_t>(i - 1)]};
      auto const curr{sa[static_cast<std::size_t>(i)]};
      tmp[static_cast<std::size_t>(curr)] =
        tmp[static_cast<std::size_t>(prev)] + (cmp(prev, curr) ? 1 : 0);
    }
    rank = tmp;
    if (rank[static_cast<std::size_t>(sa[static_cast<std::size_t>(n - 1)])] == n - 1) {
      break;
    }
  }
  return sa;
}

/**
 * @brief Builds the LCP array of \p text from its suffix array \p sa.
 *
 * Uses Kasai's algorithm. Entry \c lcp[i] is the length of the longest common
 * prefix of the suffixes \c text[sa[i-1]..] and \c text[sa[i]..]; \c lcp[0] is
 * 0 by convention.
 *
 * @param text Text the suffix array was built from.
 * @param sa Suffix array of \p text, as returned by \c build_suffix_array.
 *
 * @return A vector of size \c text.size() holding the LCP values; empty for
 *         empty \p text.
 *
 * @pre \p sa is the suffix array of \p text and has the same length;
 *      \c text.size() fits in \c std::int32_t.
 * @post The result has size \c text.size(); \c lcp[0] == 0 and every other
 *       entry is the LCP of adjacent suffixes in \p sa.
 *
 * @complexity \c O(N) time and \c O(N) auxiliary space in the length \c N of
 *             \p text.
 */
[[nodiscard]] inline auto build_lcp(
  std::string_view const text, std::span<std::int32_t const> const sa
) -> std::vector<std::int32_t> {
  auto const n{static_cast<std::int32_t>(text.size())};
  auto rank{std::vector<std::int32_t>(static_cast<std::size_t>(n))};
  for (auto i{std::int32_t{0}}; i < n; ++i) {
    rank[static_cast<std::size_t>(sa[static_cast<std::size_t>(i)])] = i;
  }
  auto lcp{std::vector<std::int32_t>(static_cast<std::size_t>(n), 0)};
  auto h{std::int32_t{0}};
  for (auto i{std::int32_t{0}}; i < n; ++i) {
    if (rank[static_cast<std::size_t>(i)] > 0) {
      auto const j{sa[static_cast<std::size_t>(rank[static_cast<std::size_t>(i)] - 1)]};
      while (i + h < n && j + h < n
             && text[static_cast<std::size_t>(i + h)] == text[static_cast<std::size_t>(j + h)]) {
        ++h;
      }
      lcp[static_cast<std::size_t>(rank[static_cast<std::size_t>(i)])] = h;
      if (h > 0) {
        h -= 1;
      }
    }
  }
  return lcp;
}

}  // namespace nexenne::algorithm
