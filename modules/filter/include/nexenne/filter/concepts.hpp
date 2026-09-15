#pragma once

/**
 * @file
 * @brief Concept for discrete-time sample filters.
 */

#include <concepts>

namespace nexenne::filter {

/**
 * @brief Concept for discrete-time sample filters.
 *
 * A \c filter_like type holds internal state and processes one
 * sample at a time via \c push(sample) -> filtered_value. The
 * common API surface is:
 *
 * - \c value_type : the sample type (e.g. \c double).
 * - \c push(T) : feed one sample, get the filtered output.
 * - \c value() : read the last output without advancing.
 * - \c reset() : return the filter to its initial state.
 *
 * Most filters in this module satisfy \c filter_like, so generic code can
 * accept "any filter" without naming a concrete type. The exceptions have a
 * different surface and are used directly: \c hysteresis (a boolean output from
 * a numeric input), \c lms (two inputs), and \c timed_debounce (no
 * \c value_type).
 *
 * @tparam F Candidate filter type. Must expose a \c value_type and the
 * \c push / \c value / \c reset surface described above.
 *
 * @pre None.
 * @post None.
 */
template <typename F>
concept filter_like = requires(F f, typename F::value_type sample) {
  typename F::value_type;
  { f.push(sample) } -> std::same_as<typename F::value_type>;
  { f.value() } -> std::same_as<typename F::value_type>;
  { f.reset() };
};

}  // namespace nexenne::filter
