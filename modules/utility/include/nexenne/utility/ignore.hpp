#pragma once

/**
 * @file
 * @brief Explicitly evaluate and ignore expression results.
 */

namespace nexenne::utility {

/**
 * @brief Evaluates its arguments and ignores the results.
 *
 * Replaces the C-style \c (void)expr and \c static_cast<void>(expr) idioms used
 * to silence a \c [[nodiscard]] warning, which are banned by the style guide.
 * Each argument is evaluated in turn, so side effects and exceptions still
 * occur at the call site, and the results are then dropped.
 *
 * This is for an unnamed result. A named entity that is deliberately not read,
 * such as a scope guard held only for its destructor or a parameter an
 * unsupported-platform stub never touches, takes \c [[maybe_unused]] on its own
 * declaration instead: the attribute is the standard tool, it sits where the
 * reader already looks, and it does not claim the value is being thrown away.
 *
 * @tparam Ts Argument types, deduced.
 * @param args Values to evaluate and ignore.
 *
 * @par Example
 * \code
 * nexenne::utility::ignore(resource.release());   // drop a [[nodiscard]]
 *
 * [[maybe_unused]] auto const guard{make_scope_guard()};  // named: attribute
 * \endcode
 */
template <typename... Ts>
constexpr auto ignore([[maybe_unused]] Ts&&... args) noexcept -> void {}

}  // namespace nexenne::utility
