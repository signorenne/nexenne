#pragma once

/**
 * @file
 * @brief Error codes and the result alias for fallible container operations.
 *
 * Module-wide error policy:
 * - A function that runs no element or caller code is \c noexcept. One that
 *   does (an element's copy, move, construction, assignment or comparison; a
 *   comparator, hasher, key equality, key range or arena) is \c noexcept
 *   exactly when that code is, so a throw from it propagates instead of
 *   terminating. Destructors are always \c noexcept.
 * - The standard function objects count as nothrow when what they apply is:
 *   \c std::less, \c std::greater and \c std::equal_to (plain or transparent)
 *   when their operator on the arguments is \c noexcept, and \c std::hash
 *   when its hash is. A caller's own callable must declare its call operator
 *   \c noexcept.
 * - An operation that mutates state and can fail at a documented boundary
 *   (popping an empty queue, pushing onto a full fixed-capacity buffer) returns
 *   \c result<T>, i.e. \c std::expected<T, container_error>; there is no
 *   precondition-based "fast path" variant.
 * - An operation whose only failure mode is \c std::bad_alloc from the
 *   underlying allocator (for example \c slot_map::insert or \c small_vector
 *   growth past its inline capacity) does not report it: allocation failure
 *   calls \c std::terminate, matching the rest of nexenne. Callers needing
 *   recovery should pre-allocate with \c reserve.
 */

#include <expected>
#include <functional>
#include <string_view>
#include <type_traits>
#include <utility>

namespace nexenne::container {

/**
 * @brief Recoverable error reported by a fallible container operation.
 */
enum class container_error {
  full,              ///< The container is full (push to a full ring buffer).
  empty,             ///< The container is empty (pop from an empty queue).
  out_of_range,      ///< An index was outside the container's logical size.
  not_found,         ///< A looked-up key or handle is not present.
  invalid_argument,  ///< An argument that no call can accept.
};

/**
 * @brief The result of a fallible container operation: a value or an error.
 *
 * An alias for \c std::expected<T, container_error>, the module's single
 * fallible-return type.
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, container_error>;

/**
 * @brief Human-readable name of a \c container_error.
 *
 * @param err Error to describe.
 *
 * @return A static string view naming the error.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(container_error const err) noexcept -> std::string_view {
  switch (err) {
    case container_error::full:
      return "full";
    case container_error::empty:
      return "empty";
    case container_error::out_of_range:
      return "out_of_range";
    case container_error::not_found:
      return "not_found";
    case container_error::invalid_argument:
      return "invalid_argument";
  }
  return "unknown";
}

namespace detail {

/// @cond INTERNAL

/**
 * @brief Whether an \p A argument binds to a \c T \c const& without throwing.
 *
 * That reference is the parameter a standard function object over \p T takes.
 *
 * @tparam A Argument type.
 * @tparam T Parameter type of the function object.
 *
 * @pre None.
 * @post None.
 */
template <typename A, typename T>
struct nothrow_binds : std::is_nothrow_convertible<A, T const&> {};

/**
 * @brief Whether \c a < \c b, taken as \c bool, is \c noexcept on \p A and \p B.
 *
 * @tparam A Left operand type.
 * @tparam B Right operand type.
 *
 * @pre None.
 * @post None.
 */
template <typename A, typename B>
struct nothrow_less_op
    : std::bool_constant<noexcept(static_cast<bool>(std::declval<A>() < std::declval<B>()))> {};

/**
 * @brief Whether \c a > \c b, taken as \c bool, is \c noexcept on \p A and \p B.
 *
 * @tparam A Left operand type.
 * @tparam B Right operand type.
 *
 * @pre None.
 * @post None.
 */
template <typename A, typename B>
struct nothrow_greater_op
    : std::bool_constant<noexcept(static_cast<bool>(std::declval<A>() > std::declval<B>()))> {};

/**
 * @brief Whether \c a == \c b, taken as \c bool, is \c noexcept on \p A and \p B.
 *
 * @tparam A Left operand type.
 * @tparam B Right operand type.
 *
 * @pre None.
 * @post None.
 */
template <typename A, typename B>
struct nothrow_equal_op
    : std::bool_constant<noexcept(static_cast<bool>(std::declval<A>() == std::declval<B>()))> {};

/**
 * @brief Whether \c std::hash over \p T hashes a \c T \c const& without throwing.
 *
 * @tparam T Hashed type.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
struct nothrow_hash_op : std::bool_constant<noexcept(std::hash<T>{}(std::declval<T const&>()))> {};

/**
 * @brief Whether invoking \p F (a cv-ref qualified \p Fn) with \p Args is nothrow.
 *
 * Any callable follows \c std::is_nothrow_invocable; a standard comparison
 * object or \c std::hash, whose call operator libstdc++ does not declare
 * \c noexcept, counts as nothrow when the operator or hash it applies is.
 * \c std::conjunction stops at the first false trait, so an operator is only
 * probed on arguments \p F accepts.
 *
 * @tparam Fn Unqualified callable type, the specialisation key.
 * @tparam F Callable type as invoked, with its cv-ref qualifiers.
 * @tparam Args Argument types.
 *
 * @pre None.
 * @post None.
 */
template <typename Fn, typename F, typename... Args>
struct nothrow_call : std::is_nothrow_invocable<F, Args...> {};

template <typename T, typename F, typename A, typename B>
  requires(!std::is_void_v<T>)
struct nothrow_call<std::less<T>, F, A, B>
    : std::conjunction<
        std::is_invocable<F, A, B>,
        nothrow_binds<A, T>,
        nothrow_binds<B, T>,
        nothrow_less_op<T const&, T const&>> {};

template <typename F, typename A, typename B>
struct nothrow_call<std::less<>, F, A, B>
    : std::conjunction<std::is_invocable<F, A, B>, nothrow_less_op<A, B>> {};

template <typename T, typename F, typename A, typename B>
  requires(!std::is_void_v<T>)
struct nothrow_call<std::greater<T>, F, A, B>
    : std::conjunction<
        std::is_invocable<F, A, B>,
        nothrow_binds<A, T>,
        nothrow_binds<B, T>,
        nothrow_greater_op<T const&, T const&>> {};

template <typename F, typename A, typename B>
struct nothrow_call<std::greater<>, F, A, B>
    : std::conjunction<std::is_invocable<F, A, B>, nothrow_greater_op<A, B>> {};

template <typename T, typename F, typename A, typename B>
  requires(!std::is_void_v<T>)
struct nothrow_call<std::equal_to<T>, F, A, B>
    : std::conjunction<
        std::is_invocable<F, A, B>,
        nothrow_binds<A, T>,
        nothrow_binds<B, T>,
        nothrow_equal_op<T const&, T const&>> {};

template <typename F, typename A, typename B>
struct nothrow_call<std::equal_to<>, F, A, B>
    : std::conjunction<std::is_invocable<F, A, B>, nothrow_equal_op<A, B>> {};

template <typename T, typename F, typename A>
struct nothrow_call<std::hash<T>, F, A>
    : std::conjunction<std::is_invocable<F, A>, nothrow_binds<A, T>, nothrow_hash_op<T>> {};

/**
 * @brief \c std::is_nothrow_invocable_v, trusting the standard function objects.
 *
 * See \c nothrow_call. Every conditional \c noexcept on a callable in this
 * module uses it.
 *
 * @tparam F Callable type as invoked.
 * @tparam Args Argument types.
 */
template <typename F, typename... Args>
// A caller's comparator type (say std::greater<int>) passes through here as a
// template argument, which modernize-use-transparent-functors misreads as a
// use.
// NOLINTNEXTLINE(modernize-use-transparent-functors)
inline constexpr bool nothrow_invocable_v{nothrow_call<std::remove_cvref_t<F>, F, Args...>::value};

/// @endcond

}  // namespace detail

}  // namespace nexenne::container
