#pragma once

/**
 * @file
 * @brief Small compile-time metaprogramming helpers (always_false_v, type_name,
 *        function_traits).
 */

#include <cstddef>
#include <string_view>
#include <type_traits>

#include <nexenne/utility/type_list.hpp>

namespace nexenne::utility {

/**
 * @brief A value-dependent \c false, always \c false regardless of \p Ts.
 *
 * Use it as the condition of a \c static_assert in the unreachable \c else of
 * an \c if \c constexpr chain, so the assert fires only when that branch is
 * selected. Since C++23 a bare \c static_assert(false) is also permitted in an
 * uninstantiated branch; \c always_false_v remains as an explicit marker.
 *
 * @tparam Ts Any pack of types; the value never depends on them.
 *
 * @pre None.
 * @post None.
 *
 * @par Example
 * \code
 * template <typename T>
 * constexpr auto describe() noexcept -> std::string_view {
 *   if constexpr (std::is_integral_v<T>) {
 *     return "integral";
 *   } else {
 *     static_assert(nexenne::utility::always_false_v<T>, "unsupported type");
 *   }
 * }
 * \endcode
 */
template <typename...>
inline constexpr bool always_false_v{false};

/**
 * @brief Compile-time spelled name of a type \p T.
 *
 * Parses the compiler's pretty-function signature and returns the spelling of
 * \p T at compile time, with no runtime cost. The result is robust to type
 * names containing spaces, \c :: qualifiers, and commas inside template
 * argument lists. The exact spelling is compiler-dependent (it may carry an
 * elaborated \c struct / \c class keyword, full qualification, or different
 * whitespace), so treat it as a diagnostic label, not a stable identifier.
 *
 * @tparam T Any type whose name to recover.
 *
 * @return The compiler's spelling of \p T, or an empty view when the compiler
 *         is unsupported or the marker cannot be located.
 *
 * @pre None.
 * @post None.
 *
 * @warning Requires GCC or Clang (\c __PRETTY_FUNCTION__); other compilers
 *          return an empty view.
 */
template <typename T>
[[nodiscard]] constexpr auto type_name() noexcept -> std::string_view {
#if defined(__GNUC__) || defined(__clang__)
  auto const fn{std::string_view{__PRETTY_FUNCTION__}};
  // Clang renders the parameter as "[T = X]" and GCC as "[with T = X; ...]";
  // both share the "T = " marker.
  auto const marker{fn.find("T = ")};
  if (marker == std::string_view::npos) {
    return {};
  }
  auto const start{marker + 4};
  // The last ']' bounds the spelling and survives array types like int[5] and
  // nested templates whose own ']' precede it. GCC packs any extra signature
  // entries after a "; ", which, when present before that bracket, is the
  // tighter, correct end.
  auto end{fn.rfind(']')};
  if (auto const semi{fn.find("; ", start)}; semi != std::string_view::npos && semi < end) {
    end = semi;
  }
  if (end == std::string_view::npos || end <= start) {
    return {};
  }
  return fn.substr(start, end - start);
#else
  return {};
#endif
}

namespace detail {

/**
 * @brief Shared payload of every \c function_traits specialisation.
 *
 * @tparam R Return type of the callable.
 * @tparam Noexcept Whether the callable's type is \c noexcept.
 * @tparam Args Parameter types of the callable.
 *
 * @pre None.
 * @post None.
 */
template <typename R, bool Noexcept, typename... Args>
struct function_traits_base {
  using return_type = R;
  using arguments = type_list<Args...>;
  static constexpr std::size_t arity{sizeof...(Args)};
  template <std::size_t I>
  using arg_t = tl_at_t<arguments, I>;
  static constexpr bool is_noexcept{Noexcept};
};

}  // namespace detail

/// @cond INTERNAL

/**
 * @brief Extracts the return type, arity, argument types, and \c noexcept-ness
 *        of a callable type \p F.
 *
 * The primary template handles functors and lambdas by delegating to the
 * traits of their \c operator(). Specialisations cover plain function types,
 * function pointers, and every cv- / ref- / \c noexcept-qualified member
 * function pointer. Each exposes, via \c detail::function_traits_base: a
 * \c return_type alias, an \c arguments alias (a \c type_list), an \c arity
 * constant, an indexed \c arg_t alias, and an \c is_noexcept constant. Prefer the
 * \c function_* aliases below over this trait directly.
 *
 * @tparam F A callable type: a function type, a (member) function pointer, or a
 *           class with a single concrete (non-overloaded, non-template)
 *           \c operator().
 *
 * @pre \p F is a supported callable type.
 * @post None.
 *
 * @warning A generic lambda or any class with an overloaded or templated
 *          \c operator() has no single signature, so only concrete callables
 *          are supported.
 */
template <typename F>
struct function_traits : function_traits<decltype(&std::remove_reference_t<F>::operator())> {};

template <typename R, typename... Args>
struct function_traits<R(Args...)> : detail::function_traits_base<R, false, Args...> {};

template <typename R, typename... Args>
struct function_traits<R(Args...) noexcept> : detail::function_traits_base<R, true, Args...> {};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : detail::function_traits_base<R, false, Args...> {};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...) noexcept> : detail::function_traits_base<R, true, Args...> {};

#define NEXENNE_UTILITY_MEMFN_TRAITS(CV, REF)                                                      \
  template <typename R, typename C, typename... Args>                                              \
  struct function_traits<R (C::*)(Args...) CV REF>                                                 \
      : detail::function_traits_base<R, false, Args...> {};                                        \
  template <typename R, typename C, typename... Args>                                              \
  struct function_traits<R (C::*)(Args...) CV REF noexcept>                                        \
      : detail::function_traits_base<R, true, Args...> {};

NEXENNE_UTILITY_MEMFN_TRAITS(, )
NEXENNE_UTILITY_MEMFN_TRAITS(, &)
NEXENNE_UTILITY_MEMFN_TRAITS(, &&)
NEXENNE_UTILITY_MEMFN_TRAITS(const, )
NEXENNE_UTILITY_MEMFN_TRAITS(const, &)
NEXENNE_UTILITY_MEMFN_TRAITS(const, &&)
NEXENNE_UTILITY_MEMFN_TRAITS(volatile, )
NEXENNE_UTILITY_MEMFN_TRAITS(volatile, &)
NEXENNE_UTILITY_MEMFN_TRAITS(volatile, &&)
NEXENNE_UTILITY_MEMFN_TRAITS(const volatile, )
NEXENNE_UTILITY_MEMFN_TRAITS(const volatile, &)
NEXENNE_UTILITY_MEMFN_TRAITS(const volatile, &&)

#undef NEXENNE_UTILITY_MEMFN_TRAITS

/// @endcond

/**
 * @brief The return type of callable \p F (reference stripped first).
 *
 * @tparam F A callable type supported by \c function_traits.
 *
 * @pre \p F is a supported callable type.
 * @post None.
 */
template <typename F>
using function_return_t = typename function_traits<std::remove_reference_t<F>>::return_type;

/**
 * @brief The parameter types of callable \p F as a \c type_list.
 *
 * @tparam F A callable type supported by \c function_traits.
 *
 * @pre \p F is a supported callable type.
 * @post None.
 */
template <typename F>
using function_args_t = typename function_traits<std::remove_reference_t<F>>::arguments;

/**
 * @brief The number of parameters of callable \p F.
 *
 * @tparam F A callable type supported by \c function_traits.
 *
 * @pre \p F is a supported callable type.
 * @post None.
 */
template <typename F>
inline constexpr std::size_t function_arity_v{function_traits<std::remove_reference_t<F>>::arity};

/**
 * @brief The type of the \p I-th parameter of callable \p F.
 *
 * A \c static_assert from \c tl_at fires when \p I is out of range.
 *
 * @tparam F A callable type supported by \c function_traits.
 * @tparam I Zero-based index into \p F's parameter list.
 *
 * @pre \p F is a supported callable type and \p I is less than
 *      \c function_arity_v<F>.
 * @post None.
 */
template <typename F, std::size_t I>
using function_arg_t = typename function_traits<std::remove_reference_t<F>>::template arg_t<I>;

/**
 * @brief Whether the type of callable \p F is \c noexcept.
 *
 * @tparam F A callable type supported by \c function_traits.
 *
 * @pre \p F is a supported callable type.
 * @post None.
 */
template <typename F>
inline constexpr bool function_is_noexcept_v{
  function_traits<std::remove_reference_t<F>>::is_noexcept
};

}  // namespace nexenne::utility
