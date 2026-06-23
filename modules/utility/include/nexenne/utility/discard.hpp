#pragma once

/**
 * @file
 * @brief Explicitly evaluate and discard expression results.
 */

namespace nexenne::utility {

/**
 * @brief Evaluates its arguments and discards the results.
 *
 * Replaces the C-style \c (void)expr and \c static_cast<void>(expr) idioms used
 * to silence unused-variable or \c [[nodiscard]] warnings, which are banned by
 * the style guide. Each argument is evaluated in turn (so side effects and
 * exceptions still occur at the call site) and then ignored. Use
 * \c [[maybe_unused]] on a declaration where the entity is named; use \c discard
 * to consume an unnamed result or to mark an existing variable as deliberately
 * touched.
 *
 * @tparam Ts Argument types, deduced.
 * @param args Values to evaluate and discard.
 *
 * @pre None.
 * @post None.
 *
 * @par Example
 * \code
 * auto const guard{make_scope_guard()};
 * nexenne::utility::discard(guard);                 // kept alive on purpose
 * nexenne::utility::discard(resource.release());    // drop a [[nodiscard]]
 * \endcode
 */
template <typename... Ts>
constexpr auto discard([[maybe_unused]] Ts&&... args) noexcept -> void {}

}  // namespace nexenne::utility
