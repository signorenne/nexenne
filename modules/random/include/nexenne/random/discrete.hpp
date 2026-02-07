#pragma once

/**
 * @file
 * @brief Discrete distribution with arbitrary per-outcome weights.
 *
 * Precomputes the cumulative weight array once at construction
 * so that each \c sample() is O(log N) via binary search. The
 * unweighted version of this, uniform-int over a range, is
 * already provided by \c uniform_int.
 *
 * Use for:
 *   - Loot-table sampling in games (e.g. 5% legendary, 25%
 *     rare, 70% common).
 *   - Markov-chain transitions weighted by edge probability.
 *   - Bootstrap resampling with non-uniform weights.
 *   - Monte Carlo importance sampling.
 *
 * Negative weights are clamped to zero. If all weights are
 * zero (or the input is empty), \c sample() returns
 * \c weights.size() as a sentinel "no outcome possible"
 * indicator, never indexes past the end.
 *
 * \code
 * auto rng{rnd::pcg32{42, 1}};
 * auto dist{rnd::discrete_distribution<double>{{1.0, 1.0, 100.0}}};
 *
 * for (auto i{0}; i < 1000; ++i) {
 *     auto const idx{dist.sample(rng)};
 *     ++hits[idx];
 * }
 * // hits[2] >> hits[0] + hits[1]
 * \endcode
 *
 * Why not \c std::discrete_distribution? Same reason as
 * \c normal_distribution: its output is implementation-defined
 * across \c libstdc++ / \c libc++ / \c MSVC, breaking
 * reproducibility across toolchains.
 *
 * @tparam T Floating-point weight type. Default \c double.
 */

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <initializer_list>
#include <iterator>
#include <ranges>
#include <span>
#include <vector>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

template <std::floating_point T = double>
class discrete_distribution {
public:
  using value_type = T;
  using size_type = std::size_t;

private:
  std::vector<T> m_cumulative{};
  T m_total{T{0}};

public:
  /**
   * @brief Constructs the distribution from a range of weights.
   *
   * Builds the cumulative-weight table once. Negative weights are
   * clamped to zero before accumulation.
   *
   * @tparam R Input range of weights convertible to \c T.
   * @param weights Per-outcome weights, in outcome order.
   *
   * @pre None. Negative weights are clamped to zero.
   * @post \c size() equals the number of weights and \c total_weight()
   *       equals the sum of the clamped weights.
   *
   * @complexity \c O(size()).
   */
  template <std::ranges::input_range R>
  explicit discrete_distribution(R const& weights) {
    if constexpr (std::ranges::sized_range<R>) {
      m_cumulative.reserve(std::ranges::size(weights));
    }
    auto running{T{0}};
    for (auto const w : weights) {
      running += w > T{0} ? w : T{0};
      m_cumulative.push_back(running);
    }
    m_total = running;
  }

  /**
   * @brief Constructs the distribution from a braced weight list.
   *
   * @param weights Per-outcome weights, in outcome order.
   *
   * @pre None. Negative weights are clamped to zero.
   * @post \c size() equals the number of weights and \c total_weight()
   *       equals the sum of the clamped weights.
   *
   * @complexity \c O(size()).
   */
  explicit discrete_distribution(std::initializer_list<T> const weights)
      : discrete_distribution{std::span<T const>{weights.begin(), weights.size()}} {}

  /**
   * @brief Returns the number of outcomes.
   *
   * @return The count of weights supplied at construction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto size() const noexcept -> size_type {
    return m_cumulative.size();
  }

  /**
   * @brief Returns the sum of all clamped weights.
   *
   * @return The total weight; zero when the distribution is empty or
   *         every weight was non-positive.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto total_weight() const noexcept -> T {
    return m_total;
  }

  /**
   * @brief Draws one outcome index proportional to its weight.
   *
   * Scales a uniform draw by the total weight and binary-searches the
   * cumulative table.
   *
   * @tparam G Engine type satisfying \c rng_engine.
   * @param g Engine to draw a uniform from.
   *
   * @return The chosen outcome index, or \c size() as a sentinel when
   *         no outcome is possible (empty or all weights zero).
   *
   * @pre None.
   * @post The result is a valid index in \c [0, size()) or the
   *       sentinel \c size(); \p g has advanced.
   *
   * @complexity \c O(log size()).
   */
  template <rng_engine G>
  [[nodiscard]] auto sample(G& g) const noexcept -> size_type {
    if (m_cumulative.empty() || m_total <= T{0}) {
      return m_cumulative.size();
    }
    auto const target{static_cast<T>(uniform_real(g)) * m_total};
    // upper_bound (first cumulative strictly greater than target), not
    // lower_bound: with a half-open draw the index whose interval is
    // (cumulative[i-1], cumulative[i]] owns the boundary, so a zero-weight
    // outcome (an equal cumulative run) is never selected, including the
    // target == 0 case that uniform_real can produce.
    auto const it{std::ranges::upper_bound(m_cumulative, target)};
    if (it == m_cumulative.end()) {
      return m_cumulative.size() - 1;  // floating-point slack guard
    }
    return static_cast<size_type>(std::distance(m_cumulative.begin(), it));
  }

  /**
   * @brief Returns the probability of a given outcome.
   *
   * Recovers the outcome's clamped weight from the cumulative table
   * and divides by the total weight.
   *
   * @param i Outcome index.
   *
   * @return The probability of outcome \p i, or zero when \p i is out
   *         of range or the total weight is non-positive.
   *
   * @pre None.
   * @post None.
   *
   * @complexity \c O(1).
   */
  [[nodiscard]] auto probability(size_type const i) const noexcept -> T {
    if (i >= m_cumulative.size() || m_total <= T{0}) {
      return T{0};
    }
    auto const w{i == 0 ? m_cumulative[0] : m_cumulative[i] - m_cumulative[i - 1]};
    return w / m_total;
  }
};

/**
 * @brief Deduction guide for constructing from a braced weight list.
 *
 * Lets \c discrete_distribution{{w0, w1, ...}} deduce the weight type
 * from the list elements.
 *
 * @tparam T Weight type.
 *
 * @pre None.
 * @post None.
 */
template <std::floating_point T>
discrete_distribution(std::initializer_list<T>) -> discrete_distribution<T>;

}  // namespace nexenne::random
