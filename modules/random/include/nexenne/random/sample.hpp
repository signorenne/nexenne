#pragma once

/**
 * @file
 * @brief Sampling utilities over random-access ranges.
 *
 *   - \c shuffle: Fisher-Yates in place.
 *   - \c reservoir_sample: Algorithm R - pick \c k items from a
 *     range of unknown length in a single pass with uniform
 *     probability per item.
 *   - \c weighted_choice: pick one index proportional to its
 *     weight via linear scan; cheap to set up, O(n) per draw.
 *     For repeated draws from the same weight vector, the alias
 *     method is faster, that's not in here yet (see TODO.md).
 *
 * All callable on any \c rng_engine.
 */

#include <cstddef>
#include <iterator>
#include <ranges>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

/**
 * @brief Shuffles a range in place with the Fisher-Yates algorithm.
 *
 * Produces a uniformly random permutation using one bounded integer
 * draw per element and swaps in place, allocating nothing.
 *
 * @tparam R Random-access range type.
 * @tparam G Engine type satisfying \c rng_engine.
 * @param range Range to permute; modified in place.
 * @param g Engine to draw indices from.
 *
 * @pre None. Ranges of fewer than two elements are left unchanged.
 * @post \p range holds a uniformly random permutation of its original
 *       elements; \p g has advanced once per swap.
 *
 * @complexity \c O(n) swaps for a range of \c n elements.
 */
template <std::ranges::random_access_range R, rng_engine G>
constexpr auto shuffle(R&& range, G& g) noexcept -> void {
  auto const n{std::ranges::size(range)};
  if (n < 2) {
    return;
  }
  using std::swap;
  auto const first{std::ranges::begin(range)};
  for (auto i{n - 1}; i > 0; --i) {
    auto const j{static_cast<std::size_t>(uniform_int<std::size_t>(g, 0, i))};
    swap(*(first + static_cast<std::ptrdiff_t>(i)), *(first + static_cast<std::ptrdiff_t>(j)));
  }
}

/**
 * @brief Samples \p k elements from a range in a single pass.
 *
 * Implements Algorithm R: each element of \p range is retained with
 * uniform probability, so the result is an unbiased sample even when the
 * range length is unknown in advance.
 *
 * @tparam R Input range type.
 * @tparam G Engine type satisfying \c rng_engine.
 * @param range Range to sample from; traversed once.
 * @param k Number of elements to draw.
 * @param g Engine to draw indices from.
 *
 * @return A vector of up to \p k sampled elements. When \p k exceeds the
 *         range size the entire range is returned.
 *
 * @pre None.
 * @post The result holds \c min(k, range_size) elements; \p g has
 *       advanced once per element beyond the first \p k.
 *
 * @complexity \c O(n) time and \c O(k) space for a range of \c n
 *             elements.
 */
template <std::ranges::input_range R, rng_engine G>
[[nodiscard]] auto
reservoir_sample(R&& range, std::size_t k, G& g) -> std::vector<std::ranges::range_value_t<R>> {
  using value_type = std::ranges::range_value_t<R>;

  auto out{std::vector<value_type>{}};
  out.reserve(k);

  auto i{std::size_t{0}};
  for (auto&& item : range) {
    if (i < k) {
      out.push_back(item);
    } else {
      auto const j{static_cast<std::size_t>(uniform_int<std::size_t>(g, 0, i))};
      if (j < k) {
        out[j] = item;
      }
    }
    ++i;
  }
  return out;
}

/**
 * @brief Picks one index proportional to its weight.
 *
 * Sums the non-negative weights, draws a uniform target, and returns the
 * first index whose cumulative weight exceeds the target. Negative
 * entries are treated as zero. For repeated draws from the same weight
 * vector an alias-method sampler would be faster, but this single linear
 * scan keeps setup cost at zero.
 *
 * @tparam G Engine type satisfying \c rng_engine.
 * @param weights Per-index weights.
 * @param g Engine to draw a uniform from.
 *
 * @return The chosen index, or \c weights.size() as a past-the-end
 *         sentinel when the input is empty or every weight is zero.
 *
 * @pre None. Negative weights are treated as zero.
 * @post The result is a valid index in \c [0, weights.size()) or the
 *       sentinel \c weights.size(); \p g has advanced.
 *
 * @complexity \c O(n) per draw for \c n weights.
 */
template <rng_engine G>
[[nodiscard]] auto weighted_choice(std::span<double const> weights, G& g) noexcept -> std::size_t {
  auto total{0.0};
  for (auto const w : weights) {
    if (w > 0.0) {
      total += w;
    }
  }
  if (total <= 0.0) {
    return weights.size();
  }
  auto const target{uniform_real(g) * total};
  auto acc{0.0};
  for (std::size_t i{0}; i < weights.size(); ++i) {
    auto const w{weights[i] > 0.0 ? weights[i] : 0.0};
    acc += w;
    if (target < acc) {
      return i;
    }
  }
  return weights.size() - 1;  // floating-point slack guard
}

}  // namespace nexenne::random
