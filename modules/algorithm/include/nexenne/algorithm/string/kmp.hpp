#pragma once

/**
 * @file
 * @brief Knuth-Morris-Pratt single-pattern search.
 *
 * Precomputes a failure table over the needle so the scan over the haystack
 * never backtracks, giving worst-case-linear \c O(|haystack| + |needle|) work.
 * Prefer it over Boyer-Moore when a linear worst case is required (Boyer-Moore
 * is usually faster but degrades on small alphabets and pathological inputs),
 * or when you want a streaming find-every-occurrence via callback. An empty
 * needle matches at position 0, consistent with \c std::string::find.
 */

#include <cstddef>
#include <string_view>
#include <type_traits>
#include <vector>

namespace nexenne::algorithm {

namespace detail {

// KMP failure function: table[i] is the length of the longest proper prefix of
// needle[0..i] that is also a suffix of it.
[[nodiscard]] constexpr auto kmp_failure(std::string_view const needle
) -> std::vector<std::size_t> {
  auto table{std::vector<std::size_t>(needle.size(), 0)};
  auto k{std::size_t{0}};
  for (auto i{std::size_t{1}}; i < needle.size(); ++i) {
    while (k > 0 && needle[k] != needle[i]) {
      k = table[k - 1];
    }
    if (needle[k] == needle[i]) {
      ++k;
    }
    table[i] = k;
  }
  return table;
}

}  // namespace detail

/**
 * @brief Index of the first occurrence of \p needle in \p haystack.
 *
 * Builds the KMP failure table over \p needle and scans \p haystack once
 * without backtracking. An empty \p needle matches at position 0.
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
 * @complexity \c O(H + N) time and \c O(N) auxiliary space, where \c H and \c N
 *             are the sizes of \p haystack and \p needle.
 */
[[nodiscard]] constexpr auto
kmp_find(std::string_view const haystack, std::string_view const needle) -> std::size_t {
  if (needle.empty()) {
    return 0;
  }
  if (needle.size() > haystack.size()) {
    return std::string_view::npos;
  }

  auto const fail{detail::kmp_failure(needle)};
  auto k{std::size_t{0}};
  for (auto i{std::size_t{0}}; i < haystack.size(); ++i) {
    while (k > 0 && needle[k] != haystack[i]) {
      k = fail[k - 1];
    }
    if (needle[k] == haystack[i]) {
      ++k;
    }
    if (k == needle.size()) {
      return i - k + 1;
    }
  }
  return std::string_view::npos;
}

/**
 * @brief Reports every occurrence of \p needle in \p haystack via callback.
 *
 * Scans \p haystack with the KMP failure table and invokes \p visit with the
 * start index of each match, including overlapping matches. When \p visit
 * returns \c bool, returning \c false stops the scan early; any other return
 * type is ignored. An empty \p needle, or one longer than \p haystack, reports
 * nothing.
 *
 * @tparam Visitor Callable invocable with a \c std::size_t index; may return
 *         \c bool to request early termination.
 * @param haystack Text to search.
 * @param needle Pattern to find.
 * @param visit Callback invoked once per match with its start index.
 *
 * @pre \p visit is callable with a single \c std::size_t argument.
 * @post Every index passed to \p visit is a valid start of an occurrence of
 *       \p needle, reported in ascending order.
 *
 * @complexity \c O(H + N) time and \c O(N) auxiliary space, where \c H and \c N
 *             are the sizes of \p haystack and \p needle.
 */
template <typename Visitor>
constexpr auto kmp_find_all(
  std::string_view const haystack, std::string_view const needle, Visitor&& visit
) -> void {
  if (needle.empty() || needle.size() > haystack.size()) {
    return;
  }

  auto const fail{detail::kmp_failure(needle)};
  auto k{std::size_t{0}};
  for (auto i{std::size_t{0}}; i < haystack.size(); ++i) {
    while (k > 0 && needle[k] != haystack[i]) {
      k = fail[k - 1];
    }
    if (needle[k] == haystack[i]) {
      ++k;
    }
    if (k == needle.size()) {
      auto const pos{i - k + 1};
      if constexpr (std::is_same_v<decltype(visit(pos)), bool>) {
        if (!visit(pos)) {
          return;
        }
      } else {
        visit(pos);
      }
      k = fail[k - 1];
    }
  }
}

}  // namespace nexenne::algorithm
