#pragma once

/**
 * @file
 * @brief Binary-search variants that extend \c std::ranges.
 *
 * \c std::ranges already ships \c lower_bound, \c upper_bound, \c equal_range,
 * \c binary_search, and \c partition_point with projection support. This header
 * adds the variants it does not:
 *
 *   - \c find_sorted: returns a \c found_index (an optional zero-based index)
 *     rather than an iterator, for direct array indexing.
 *   - \c exponential_search: galloping search, faster than a plain binary
 *     search when the target sits near the front of a huge range.
 *   - \c interpolation_search: expected \c O(log log N) on uniformly
 *     distributed numeric data.
 *
 * All take a sorted random-access range and order with \c < by default.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <functional>
#include <iterator>
#include <optional>
#include <ranges>
#include <type_traits>

namespace nexenne::algorithm {

/// @brief A search result: the zero-based index of a match, or empty on a miss.
using found_index = std::optional<std::size_t>;

/**
 * @brief Index of \p value in a sorted range, or empty when absent.
 *
 * Locates \p value with \c std::ranges::lower_bound and confirms an exact match
 * with a two-way \c < comparison, then returns the zero-based offset of the
 * element from the start of the range. When several elements compare equal to
 * \p value, the index of the first such element is returned.
 *
 * @tparam R Sorted random-access range type.
 * @tparam T Type comparable to the range value type via \c <.
 * @param range Range sorted in ascending order with respect to \c <.
 * @param value Value to locate.
 *
 * @return The zero-based index of the first element equal to \p value, or empty
 *         when no element equals \p value.
 *
 * @pre \p range is sorted in ascending order with respect to \c <.
 * @post A returned index is a valid offset into \p range and the element at it
 *       compares equal to \p value.
 *
 * @complexity \c O(log N) comparisons in the size \c N of \p range.
 */
template <std::ranges::random_access_range R, typename T>
  requires std::strict_weak_order<std::ranges::less, std::ranges::range_value_t<R> const&, T const&>
[[nodiscard]] constexpr auto find_sorted(R&& range, T const& value) noexcept -> found_index {
  auto const first{std::ranges::begin(range)};
  auto const last{std::ranges::end(range)};
  auto const it{std::ranges::lower_bound(range, value)};
  if (it == last) {
    return std::nullopt;
  }
  if (*it < value || value < *it) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::ranges::distance(first, it));
}

/**
 * @brief Exponential (galloping) search for \p value in a sorted range.
 *
 * Doubles a probe offset until it brackets \p value, then binary-searches the
 * final bracket. This beats a plain binary search when the target sits near the
 * front of a very large range, because the bracket searched is proportional to
 * the distance of \p value from the front rather than to the whole range size.
 * The worst case matches a binary search.
 *
 * @tparam R Sorted random-access range type.
 * @tparam T Type comparable to the range value type via \c <.
 * @param range Range sorted in ascending order with respect to \c <.
 * @param value Value to locate.
 *
 * @return The zero-based index of an element equal to \p value, or empty when
 *         no element equals \p value.
 *
 * @pre \p range is sorted in ascending order with respect to \c <.
 * @post A returned index is a valid offset into \p range and the element at it
 *       compares equal to \p value.
 *
 * @complexity \c O(log P) comparisons, where \c P is the index of the first
 *             element not less than \p value; \c O(log N) in the worst case.
 */
template <std::ranges::random_access_range R, typename T>
  requires std::strict_weak_order<std::ranges::less, std::ranges::range_value_t<R> const&, T const&>
[[nodiscard]] constexpr auto exponential_search(R&& range, T const& value) noexcept -> found_index {
  auto const first{std::ranges::begin(range)};
  auto const last{std::ranges::end(range)};
  auto const n{std::ranges::distance(first, last)};
  if (n == 0) {
    return std::nullopt;
  }
  // Two-way \c < rather than \c ==: the same comparator the range is ordered by.
  if (!(*first < value) && !(value < *first)) {
    return std::size_t{0};
  }

  auto i{std::ranges::range_difference_t<R>{1}};
  while (i < n && *(first + i) < value) {
    i *= 2;
  }

  auto const lo{first + (i / 2)};
  auto const hi{i < n ? first + i + 1 : last};
  auto const it{std::ranges::lower_bound(lo, hi, value)};
  if (it == last) {
    return std::nullopt;
  }
  if (*it < value || value < *it) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::ranges::distance(first, it));
}

/**
 * @brief Interpolation search for \p value in a sorted numeric range.
 *
 * Predicts the probe position of \p value from the linear fraction
 * \c (value-low)/(high-low) rather than always splitting at the midpoint. This
 * is only meaningful for integral or floating-point ranges, which the
 * constraint enforces. For uniformly distributed data the expected cost is far
 * below a binary search; an adversarial distribution degrades it to a linear
 * scan.
 *
 * @tparam R Sorted random-access range of an arithmetic value type.
 * @tparam T Arithmetic type comparable to the range value type.
 * @param range Range sorted in ascending order with respect to \c <.
 * @param value Value to locate.
 *
 * @return The zero-based index of an element equal to \p value, or empty when
 *         no element equals \p value.
 *
 * @pre \p range is sorted in ascending order with respect to \c <.
 * @post A returned index is a valid offset into \p range and the element at it
 *       compares equal to \p value.
 *
 * @complexity \c O(log log N) expected on uniformly distributed data, \c O(N)
 *             in the worst case.
 */
template <std::ranges::random_access_range R, typename T>
  requires std::is_arithmetic_v<std::ranges::range_value_t<R>> && std::is_arithmetic_v<T>
[[nodiscard]] constexpr auto
interpolation_search(R&& range, T const& value) noexcept -> found_index {
  using diff_type = std::ranges::range_difference_t<R>;

  auto const first{std::ranges::begin(range)};
  auto lo{diff_type{0}};
  auto hi{std::ranges::distance(first, std::ranges::end(range)) - 1};

  while (lo <= hi && value >= *(first + lo) && value <= *(first + hi)) {
    if (lo == hi) {
      if (*(first + lo) == value) {
        return static_cast<std::size_t>(lo);
      }
      return std::nullopt;
    }
    auto const lo_val{*(first + lo)};
    auto const hi_val{*(first + hi)};
    if (hi_val == lo_val) {  // a flat span: avoid dividing by zero
      return *(first + lo) == value ? found_index{static_cast<std::size_t>(lo)} : std::nullopt;
    }
    // Subtract in double, not in the element type: an integer span from a large
    // negative to a large positive value would overflow (signed UB) otherwise.
    auto const span{static_cast<double>(hi_val) - static_cast<double>(lo_val)};
    auto const offset{static_cast<double>(value) - static_cast<double>(lo_val)};
    auto const pos{lo + static_cast<diff_type>(offset * static_cast<double>(hi - lo) / span)};
    if (pos < lo || pos > hi) {
      return std::nullopt;
    }
    auto const probe{*(first + pos)};
    if (probe == value) {
      return static_cast<std::size_t>(pos);
    }
    if (probe < value) {
      lo = pos + 1;
    } else {
      hi = pos - 1;
    }
  }
  return std::nullopt;
}

}  // namespace nexenne::algorithm
