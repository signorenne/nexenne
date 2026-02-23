#pragma once

/**
 * @file
 * @brief LSD radix sort for unsigned-integer ranges.
 *
 * Sorts a contiguous range of unsigned integers in place with 8-bit-radix
 * least-significant-digit passes, in \c O(N * sizeof(T)) time, which is \c O(N)
 * for any fixed integer width and beats the comparison-sort lower bound on large
 * inputs. The sort is stable. It needs one auxiliary buffer the size of the
 * input, ping-ponged across the digit passes; the allocating overload manages
 * that buffer itself, and a caller-supplied-scratch overload is provided for
 * tight memory budgets. Signed types are not supported: use \c std::sort, or
 * bias the input into an unsigned domain first.
 */

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <memory>
#include <span>
#include <utility>
#include <vector>

namespace nexenne::algorithm {

namespace detail {

template <std::unsigned_integral T>
constexpr auto radix_sort_pass(
  std::span<T const> const input, std::span<T> const output, std::size_t const byte_index
) noexcept -> void {
  constexpr std::size_t buckets{256};
  auto counts{std::array<std::size_t, buckets>{}};

  // Count occurrences of each byte value at this digit.
  for (auto const v : input) {
    auto const b{static_cast<std::uint8_t>(v >> (byte_index * 8))};
    ++counts[b];
  }

  // Convert counts to starting offsets via a prefix sum.
  auto running{std::size_t{0}};
  for (auto& c : counts) {
    auto const old{c};
    c = running;
    running += old;
  }

  // Scatter each element into its bucketed position; stable because input is
  // walked front to back.
  for (auto const v : input) {
    auto const b{static_cast<std::uint8_t>(v >> (byte_index * 8))};
    output[counts[b]] = v;
    ++counts[b];
  }
}

template <std::unsigned_integral T>
constexpr auto
radix_sort_into(std::span<T> const range, std::span<T> const scratch) noexcept -> void {
  auto a{range};
  auto b{scratch.subspan(0, range.size())};

  constexpr std::size_t bytes{sizeof(T)};
  for (auto i{std::size_t{0}}; i < bytes; ++i) {
    radix_sort_pass(std::span<T const>{a}, b, i);
    std::swap(a, b);
  }
  // After an even number of passes the sorted data sits in \c range; after an
  // odd number it sits in \c scratch and is copied back.
  if constexpr (bytes % 2 == 1) {
    for (auto i{std::size_t{0}}; i < range.size(); ++i) {
      range[i] = scratch[i];
    }
  }
}

}  // namespace detail

/**
 * @brief Sorts a span of unsigned integers in place using caller scratch.
 *
 * Runs the radix passes between \p range and \p scratch with no allocation, so
 * it suits a heap-free budget. A range of size zero or one returns immediately.
 *
 * @tparam T Unsigned-integer element type.
 * @param range Span of elements to sort in place.
 * @param scratch Auxiliary span, at least as large as \p range.
 *
 * @pre \c scratch.size() is at least \c range.size(); \p range and \p scratch
 *      do not overlap.
 * @post \p range is sorted in non-decreasing order and is a permutation of its
 *       original contents; equal elements keep their relative order. The
 *       contents of \p scratch are unspecified.
 *
 * @complexity \c O(N * sizeof(T)) time and no allocation.
 */
template <std::unsigned_integral T>
constexpr auto radix_sort(std::span<T> const range, std::span<T> const scratch) noexcept -> void {
  if (range.size() <= 1) {
    return;
  }
  detail::radix_sort_into(range, scratch);
}

/**
 * @brief Sorts a span of unsigned integers in place by LSD radix sort.
 *
 * Allocates a scratch buffer the size of \p range and forwards to the
 * scratch overload. A range of size zero or one returns immediately.
 *
 * @tparam T Unsigned-integer element type.
 * @param range Span of elements to sort in place.
 *
 * @pre None. Every value of an unsigned-integer \c T is sortable.
 * @post \p range is sorted in non-decreasing order and is a permutation of its
 *       original contents; equal elements keep their relative order.
 *
 * @complexity \c O(N * sizeof(T)) time and \c O(N) auxiliary space.
 */
template <std::unsigned_integral T>
constexpr auto radix_sort(std::span<T> const range) -> void {
  if (range.size() <= 1) {
    return;
  }
  auto scratch{std::vector<T>(range.size())};
  detail::radix_sort_into(range, std::span<T>{scratch});
}

/**
 * @brief Iterator-pair overload of \c radix_sort for contiguous ranges.
 *
 * Wraps \c [first, last) in a \c std::span and forwards to the allocating span
 * overload.
 *
 * @tparam It Contiguous iterator with an unsigned-integer value type.
 * @param first Iterator to the first element to sort.
 * @param last Iterator one past the last element to sort.
 *
 * @pre \c [first, last) is a valid contiguous range.
 * @post The range is sorted in non-decreasing order and is a stable permutation
 *       of its original contents.
 *
 * @complexity \c O(N * sizeof(T)) time and \c O(N) auxiliary space.
 */
template <std::contiguous_iterator It>
  requires std::unsigned_integral<std::iter_value_t<It>>
constexpr auto radix_sort(It const first, It const last) -> void {
  radix_sort(std::span{std::to_address(first), static_cast<std::size_t>(last - first)});
}

}  // namespace nexenne::algorithm
