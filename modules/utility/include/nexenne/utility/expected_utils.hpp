#pragma once

/**
 * @file
 * @brief Helpers for \c std::expected that the standard library omits.
 *
 * C++23's \c std::expected already provides the monadic chain primitives
 * (\c and_then, \c or_else, \c transform, \c transform_error, \c value_or,
 * \c error_or). This header adds the small ergonomic pieces that come up in
 * real code but the standard left out: \c into_optional (drop the error),
 * \c try_or (call a fallback with the error), \c flatten (collapse a nested
 * \c expected), and \c first_error (fold N results to the first error). All
 * operations are header-only and \c noexcept where the callables are.
 */

#include <concepts>
#include <expected>
#include <optional>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief Drops the error channel: turns \c expected<T, E> into \c optional<T>.
 *
 * @tparam T Value type.
 * @tparam E Error type.
 * @param e Expected to convert.
 *
 * @return The contained value in a \c std::optional on success, or
 *         \c std::nullopt on error.
 *
 * @pre None.
 * @post The result has a value if and only if \p e had a value.
 */
template <typename T, typename E>
  requires(!std::is_void_v<T>)
[[nodiscard]] constexpr auto into_optional(std::expected<T, E> const& e) -> std::optional<T> {
  if (e) {
    return *e;
  }
  return std::nullopt;
}

/**
 * @brief Rvalue overload of \c into_optional that moves the value out.
 *
 * @tparam T Value type.
 * @tparam E Error type.
 * @param e Expected to convert; its value is moved on success.
 *
 * @return The moved value in a \c std::optional on success, or \c std::nullopt
 *         on error.
 *
 * @pre None.
 * @post The result has a value if and only if \p e had a value.
 */
template <typename T, typename E>
  requires(!std::is_void_v<T>)
[[nodiscard]] constexpr auto into_optional(std::expected<T, E>&& e) -> std::optional<T> {
  if (e) {
    return std::optional<T>{std::move(*e)};
  }
  return std::nullopt;
}

/**
 * @brief Overload for \c expected<void, E>: reports success as a \c bool.
 *
 * @tparam E Error type.
 * @param e Expected to inspect.
 *
 * @return \c true when \p e holds a value, \c false when it holds an error.
 *
 * @pre None.
 * @post None.
 */
template <typename E>
[[nodiscard]] constexpr auto into_optional(std::expected<void, E> const& e) noexcept -> bool {
  return e.has_value();
}

/**
 * @brief Collapses \c expected<expected<T, E>, E> into \c expected<T, E>.
 *
 * The outer error passes through unchanged; otherwise the inner \c expected
 * becomes the result.
 *
 * @tparam T Value type.
 * @tparam E Error type, shared by both nesting levels.
 * @param e Nested expected to flatten.
 *
 * @return The inner \c expected when the outer holds a value, otherwise the
 *         outer error.
 *
 * @pre None.
 * @post The result holds an error if and only if either nesting level did.
 */
template <typename T, typename E>
[[nodiscard]] constexpr auto flatten(std::expected<std::expected<T, E>, E> const& e
) -> std::expected<T, E> {
  if (!e) {
    return std::unexpected{e.error()};
  }
  return *e;
}

/**
 * @brief Rvalue overload of \c flatten that moves the inner value or error out.
 *
 * @tparam T Value type.
 * @tparam E Error type, shared by both nesting levels.
 * @param e Nested expected to flatten; its contents are moved.
 *
 * @return The moved inner \c expected when the outer holds a value, otherwise
 *         the moved outer error.
 *
 * @pre None.
 * @post The result holds an error if and only if either nesting level did.
 */
template <typename T, typename E>
[[nodiscard]] constexpr auto flatten(std::expected<std::expected<T, E>, E>&& e
) -> std::expected<T, E> {
  if (!e) {
    return std::unexpected{std::move(e).error()};
  }
  return std::move(*e);
}

/**
 * @brief Folds a pack of \c expected<void, E> values to the first error.
 *
 * Scans the arguments left to right and returns the first that holds an error;
 * if every argument succeeded, returns a value-holding \c expected<void, E>.
 * The arguments themselves are already evaluated at the call, so this does not
 * short-circuit their evaluation.
 *
 * @tparam E Error type.
 * @tparam Args Remaining \c expected<void, E> argument types.
 * @param first First result to inspect.
 * @param rest Further results, inspected in order.
 *
 * @return The first error encountered, or a value-holding \c expected<void, E>
 *         when all succeeded.
 *
 * @pre None.
 * @post The result holds an error if and only if at least one argument did.
 *
 * @par Example
 * \code
 * if (auto r{first_error(open_bus(), configure_clock(), reset_chip())}; !r) {
 *   log_error(r.error());
 *   return;
 * }
 * \endcode
 */
template <typename E, typename... Args>
  requires(std::same_as<std::remove_cvref_t<Args>, std::expected<void, E>> && ...)
[[nodiscard]] constexpr auto
first_error(std::expected<void, E> const& first, Args const&... rest) -> std::expected<void, E> {
  if (!first) {
    return std::unexpected{first.error()};
  }
  if constexpr (sizeof...(rest) > 0) {
    return first_error<E>(rest...);
  } else {
    return {};
  }
}

/**
 * @brief Like \c value_or, but the fallback is a callable taking the error.
 *
 * Returns the contained value on success; on error, invokes \p fn with the
 * error and returns its result. \p fn is only called on the error path.
 *
 * @tparam T Value type.
 * @tparam E Error type.
 * @tparam Fn Callable invocable as \c T(E const&).
 * @param e Expected to inspect.
 * @param fn Fallback callable invoked with the error on failure.
 *
 * @return The contained value on success, otherwise \c fn(e.error()).
 *
 * @pre \p fn is invocable as \c T(E const&).
 * @post None.
 *
 * @throws Anything \p fn throws when invoked on the error path.
 *
 * @par Example
 * \code
 * auto const v{try_or(read_sensor(), [](auto err) {
 *   log_warning(to_string_view(err));
 *   return last_known_good_value;
 * })};
 * \endcode
 */
template <typename T, typename E, typename Fn>
  requires std::is_invocable_r_v<T, Fn, E const&>
[[nodiscard]] constexpr auto try_or(std::expected<T, E> const& e, Fn&& fn) -> T {
  if (e) {
    return *e;
  }
  return std::forward<Fn>(fn)(e.error());
}

/**
 * @brief Rvalue overload of \c try_or that moves the value or error out.
 *
 * @tparam T Value type.
 * @tparam E Error type.
 * @tparam Fn Callable invocable as \c T(E&&).
 * @param e Expected to inspect; its value or error is moved.
 * @param fn Fallback callable invoked with the moved error on failure.
 *
 * @return The moved value on success, otherwise \c fn(std::move(e).error()).
 *
 * @pre \p fn is invocable as \c T(E&&).
 * @post None.
 *
 * @throws Anything \p fn throws when invoked on the error path.
 */
template <typename T, typename E, typename Fn>
  requires std::is_invocable_r_v<T, Fn, E&&>
[[nodiscard]] constexpr auto try_or(std::expected<T, E>&& e, Fn&& fn) -> T {
  if (e) {
    return std::move(*e);
  }
  return std::forward<Fn>(fn)(std::move(e).error());
}

}  // namespace nexenne::utility
