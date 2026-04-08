#pragma once

/**
 * @file
 * @brief Apply a callable to every non-null element of a range.
 */

#include <functional>
#include <ranges>

namespace nexenne::utility {

/**
 * @brief Calls \p fn with the pointee of every element of \p range that is not
 *        null.
 *
 * Replaces the ubiquitous "iterate, skip the null ones, dereference the rest"
 * loop over a container of pointer-like handles (raw pointers, \c shared_ptr,
 * \c unique_ptr, \c non_null, ...). Each surviving element is dereferenced once
 * and passed to \p fn by reference, so the callback never sees a null and never
 * has to dereference by hand.
 *
 * @tparam Range Input range whose elements are comparable to \c nullptr and
 *               dereferenceable.
 * @tparam Fn Callable invocable with \c *element.
 * @param range Range of pointer-like elements to scan.
 * @param fn Callable applied to each non-null element's pointee.
 *
 * @pre None.
 * @post \p fn has been invoked once, in order, for each non-null element.
 *
 * @complexity \c O(n) in the size of \p range, plus the cost of \p fn.
 *
 * @par Example
 * \code
 * nexenne::utility::for_each_non_null(m_sinks, [&](auto& sink) { sink.write(r); });
 * \endcode
 */
template <std::ranges::input_range Range, typename Fn>
  requires requires(Fn& fn, std::ranges::range_reference_t<Range> element) { fn(*element); }
constexpr auto for_each_non_null(Range&& range, Fn fn) -> void {
  for (auto&& element : range) {
    if (element != nullptr) {
      std::invoke(fn, *element);
    }
  }
}

}  // namespace nexenne::utility
