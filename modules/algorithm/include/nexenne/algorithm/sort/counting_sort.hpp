#pragma once

/**
 * @file
 * @brief Counting sort for small-range unsigned integers.
 *
 * Sorts a contiguous range in place when every element lies in
 * \c [0, max_value], in \c O(N + K) time where \c K is the size of the value
 * range. It wins when \c K is comparable to or smaller than \c N: byte streams,
 * histogram-style data, identifiers in a known bounded space. The sort is
 * stable, uses \c O(K) auxiliary space for the count array, and the caller
 * passes \p max_value so the input need not be scanned twice. For an unknown or
 * wide range, derive the bound from \c std::ranges::max beforehand or use
 * \c radix_sort instead.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace nexenne::algorithm {

/**
 * @brief Sorts a span of small-range unsigned integers in place.
 *
 * Tallies each value into a count array of size \c max_value + 1, then rewrites
 * the span in ascending order by expanding each bucket to its tally. Stable by
 * construction. A range of size zero or one returns immediately.
 *
 * @tparam T Unsigned-integer element type.
 * @param range Span of elements to sort in place.
 * @param max_value Inclusive upper bound on element values.
 *
 * @pre Every element of \p range is in \c [0, max_value]; a larger value
 *      indexes outside the count array and is undefined behaviour.
 * @post \p range is sorted in non-decreasing order and is a permutation of its
 *       original contents; equal elements keep their relative order.
 *
 * @complexity \c O(N + K) time and \c O(K) auxiliary space, where \c N is the
 *             span size and \c K is \c max_value + 1.
 */
template <std::unsigned_integral T>
constexpr auto counting_sort(std::span<T> const range, T const max_value) -> void {
  if (range.size() <= 1) {
    return;
  }
  // A bucket count of max_value + 1 overflows std::size_t only when max_value is
  // its maximum; such a key range cannot be counting-sorted (the bucket array
  // would be unrepresentable), so fall back to a comparison sort rather than wrap
  // to an empty vector and write out of bounds.
  if (static_cast<std::size_t>(max_value) == std::numeric_limits<std::size_t>::max()) {
    std::ranges::sort(range);
    return;
  }
  auto const buckets{static_cast<std::size_t>(max_value) + 1};
  auto counts{std::vector<std::size_t>(buckets, 0)};

  for (auto const v : range) {
    ++counts[static_cast<std::size_t>(v)];
  }

  auto out{std::size_t{0}};
  for (auto v{std::size_t{0}}; v < buckets; ++v) {
    for (auto i{std::size_t{0}}; i < counts[v]; ++i) {
      range[out] = static_cast<T>(v);
      ++out;
    }
  }
}

/**
 * @brief Iterator-pair overload of \c counting_sort for contiguous ranges.
 *
 * Wraps \c [first, last) in a \c std::span and forwards to the span overload.
 *
 * @tparam It Contiguous iterator with an unsigned-integer value type.
 * @param first Iterator to the first element to sort.
 * @param last Iterator one past the last element to sort.
 * @param max_value Inclusive upper bound on element values.
 *
 * @pre \c [first, last) is a valid contiguous range and every element is in
 *      \c [0, max_value].
 * @post The range is sorted in non-decreasing order and is a stable permutation
 *       of its original contents.
 *
 * @complexity \c O(N + K) time and \c O(K) auxiliary space, where \c N is
 *             \c last - first and \c K is \c max_value + 1.
 */
template <std::contiguous_iterator It>
  requires std::unsigned_integral<std::iter_value_t<It>>
constexpr auto
counting_sort(It const first, It const last, std::iter_value_t<It> const max_value) -> void {
  counting_sort(
    std::span{std::to_address(first), static_cast<std::size_t>(last - first)}, max_value
  );
}

}  // namespace nexenne::algorithm
