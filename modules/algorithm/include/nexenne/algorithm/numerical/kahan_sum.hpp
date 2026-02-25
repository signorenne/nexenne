#pragma once

/**
 * @file
 * @brief Compensated summation: O(1) memory, far better accuracy than naive.
 *
 * Naive floating-point accumulation (\c std::accumulate, \c fold_left) loses
 * precision proportional to the running sum's magnitude divided by the smallest
 * addend; over millions of iterations the drift makes the answer wrong at the
 * percent level. Compensated summation carries a small error-correction term
 * alongside the accumulator and largely eliminates the drift for one extra
 * subtract per element.
 *
 * \c kahan_sum (Kahan, 1965) tracks the low-order bits lost in each add and
 * handles most workloads. \c neumaier_sum (Neumaier, 1974) refines it for the
 * case where the running sum is sometimes smaller in magnitude than the next
 * addend, at the cost of one extra branch. Both are O(N) time, O(1) space, and
 * accept any input range of a floating-point type.
 */

#include <concepts>
#include <ranges>

namespace nexenne::algorithm {

/**
 * @brief Kahan-compensated sum of \p range.
 *
 * Carries a running compensation term that recovers the low-order bits lost by
 * each addition, far more accurate than naive accumulation at constant extra
 * memory.
 *
 * @tparam Range Input range type.
 * @tparam T Floating-point accumulator type; defaults to the range value type.
 * @param range Range of values to sum.
 * @param init Initial accumulator value; defaults to zero.
 *
 * @return The compensated sum of \p init and every element of \p range.
 *
 * @pre None.
 * @post For an empty range the result equals \p init.
 *
 * @complexity \c O(N) time and \c O(1) auxiliary space in the element count.
 */
template <std::ranges::input_range Range, std::floating_point T = std::ranges::range_value_t<Range>>
[[nodiscard]] constexpr auto kahan_sum(Range&& range, T init = T{0}) noexcept -> T {
  auto sum{init};
  auto comp{T{0}};  // running compensation for lost low-order bits
  for (auto const value : range) {
    auto const y{value - comp};  // partially compensated value
    auto const t{sum + y};       // new sum, may lose low-order bits of y
    comp = (t - sum) - y;        // what was lost, guarded against cancellation
    sum = t;
  }
  return sum;
}

/**
 * @brief Neumaier-compensated sum of \p range.
 *
 * A refinement of \c kahan_sum that is strictly more accurate when the running
 * sum is occasionally smaller in magnitude than the next addend, at the cost of
 * one extra branch per element; the compensation is folded in once at the end.
 *
 * @tparam Range Input range type.
 * @tparam T Floating-point accumulator type; defaults to the range value type.
 * @param range Range of values to sum.
 * @param init Initial accumulator value; defaults to zero.
 *
 * @return The compensated sum of \p init and every element of \p range.
 *
 * @pre None.
 * @post For an empty range the result equals \p init.
 *
 * @complexity \c O(N) time and \c O(1) auxiliary space in the element count.
 */
template <std::ranges::input_range Range, std::floating_point T = std::ranges::range_value_t<Range>>
[[nodiscard]] constexpr auto neumaier_sum(Range&& range, T init = T{0}) noexcept -> T {
  auto sum{init};
  auto comp{T{0}};
  for (auto const value : range) {
    auto const t{sum + value};
    auto const abs_sum{sum < T{0} ? -sum : sum};
    auto const abs_v{value < T{0} ? -value : value};
    if (abs_sum >= abs_v) {
      comp += (sum - t) + value;
    } else {
      comp += (value - t) + sum;
    }
    sum = t;
  }
  return sum + comp;
}

}  // namespace nexenne::algorithm
