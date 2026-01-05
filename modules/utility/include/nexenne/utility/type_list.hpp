#pragma once

/**
 * @file
 * @brief Compile-time list of types and the metafunctions that operate on it.
 */

#include <cstddef>
#include <type_traits>

namespace nexenne::utility {

/**
 * @brief A compile-time list of types.
 *
 * Empty aggregate that carries a pack of types through template
 * metaprogramming without instantiating any of them. It is a type-level tag
 * operated on by the \c tl_ metafunctions (\c tl_size, \c tl_at,
 * \c tl_contains, \c tl_index_of, \c tl_push_back, \c tl_push_front,
 * \c tl_concat, \c tl_transform, \c tl_filter, \c tl_unique). Every operation
 * is evaluated at compile time and produces no runtime code.
 *
 * @tparam Ts The types held by the list.
 *
 * @pre None.
 * @post None.
 *
 * @par Example
 * \code
 * using list = nexenne::utility::type_list<int, float, double>;
 * static_assert(nexenne::utility::tl_size_v<list> == 3);
 * static_assert(std::same_as<nexenne::utility::tl_at_t<list, 1>, float>);
 * \endcode
 */
template <typename... Ts>
struct type_list {};

/**
 * @brief Element count of a \c type_list.
 *
 * @tparam L A \c type_list.
 *
 * @pre \p L is a \c type_list specialisation.
 * @post None.
 */
template <typename L>
struct tl_size;

template <typename... Ts>
struct tl_size<type_list<Ts...>> : std::integral_constant<std::size_t, sizeof...(Ts)> {};

/// @brief Convenience variable template for \c tl_size<L>::value.
template <typename L>
inline constexpr auto tl_size_v{tl_size<L>::value};

namespace detail {

template <std::size_t N, typename... Ts>
struct tl_at_impl;

template <typename Head, typename... Tail>
struct tl_at_impl<0, Head, Tail...> {
  using type = Head;
};

template <std::size_t N, typename Head, typename... Tail>
struct tl_at_impl<N, Head, Tail...> : tl_at_impl<N - 1, Tail...> {};

}  // namespace detail

/**
 * @brief The type at index \p N of a \c type_list.
 *
 * Exposes \c type, the \p N-th element. A \c static_assert fires when \p N is
 * out of range.
 *
 * @tparam L A \c type_list.
 * @tparam N Zero-based index into the list.
 *
 * @pre \p L is a \c type_list and \p N is less than its size.
 * @post None.
 */
template <typename L, std::size_t N>
struct tl_at;

template <std::size_t N, typename... Ts>
struct tl_at<type_list<Ts...>, N> : detail::tl_at_impl<N, Ts...> {
  static_assert(N < sizeof...(Ts), "tl_at: index out of range");
};

/// @brief Convenience alias for \c tl_at<L, N>::type.
template <typename L, std::size_t N>
using tl_at_t = typename tl_at<L, N>::type;

/**
 * @brief Whether a \c type_list contains type \p T.
 *
 * Exposes \c value, \c true when \p T appears at least once.
 *
 * @tparam L A \c type_list.
 * @tparam T Type to search for.
 *
 * @pre \p L is a \c type_list specialisation.
 * @post None.
 */
template <typename L, typename T>
struct tl_contains;

template <typename T, typename... Ts>
struct tl_contains<type_list<Ts...>, T> : std::bool_constant<(std::is_same_v<T, Ts> || ...)> {};

/// @brief Convenience variable template for \c tl_contains<L, T>::value.
template <typename L, typename T>
inline constexpr auto tl_contains_v{tl_contains<L, T>::value};

namespace detail {

template <std::size_t I, typename T, typename... Ts>
struct tl_index_of_impl;

template <std::size_t I, typename T, typename Head, typename... Tail>
struct tl_index_of_impl<I, T, Head, Tail...>
    : std::conditional_t<
        std::is_same_v<T, Head>,
        std::integral_constant<std::size_t, I>,
        tl_index_of_impl<I + 1, T, Tail...>> {};

template <std::size_t I, typename T>
struct tl_index_of_impl<I, T> {
  static_assert(false, "tl_index_of: type not in list");
};

}  // namespace detail

/**
 * @brief Index of the first occurrence of type \p T in a \c type_list.
 *
 * Exposes \c value, the zero-based index of \p T. A \c static_assert fires
 * when \p T is not in the list.
 *
 * @tparam L A \c type_list.
 * @tparam T Type to locate.
 *
 * @pre \p L is a \c type_list and contains \p T.
 * @post None.
 */
template <typename L, typename T>
struct tl_index_of;

template <typename T, typename... Ts>
struct tl_index_of<type_list<Ts...>, T> : detail::tl_index_of_impl<0, T, Ts...> {};

/// @brief Convenience variable template for \c tl_index_of<L, T>::value.
template <typename L, typename T>
inline constexpr auto tl_index_of_v{tl_index_of<L, T>::value};

/**
 * @brief Appends type \p T to the end of a \c type_list.
 *
 * @tparam L A \c type_list.
 * @tparam T Type to append.
 *
 * @pre \p L is a \c type_list specialisation.
 * @post None.
 */
template <typename L, typename T>
struct tl_push_back;

template <typename T, typename... Ts>
struct tl_push_back<type_list<Ts...>, T> {
  using type = type_list<Ts..., T>;
};

/// @brief Convenience alias for \c tl_push_back<L, T>::type.
template <typename L, typename T>
using tl_push_back_t = typename tl_push_back<L, T>::type;

/**
 * @brief Prepends type \p T to the front of a \c type_list.
 *
 * @tparam L A \c type_list.
 * @tparam T Type to prepend.
 *
 * @pre \p L is a \c type_list specialisation.
 * @post None.
 */
template <typename L, typename T>
struct tl_push_front;

template <typename T, typename... Ts>
struct tl_push_front<type_list<Ts...>, T> {
  using type = type_list<T, Ts...>;
};

/// @brief Convenience alias for \c tl_push_front<L, T>::type.
template <typename L, typename T>
using tl_push_front_t = typename tl_push_front<L, T>::type;

/**
 * @brief Concatenates two \c type_list lists.
 *
 * Exposes \c type, the elements of \p L1 followed by those of \p L2.
 *
 * @tparam L1 First \c type_list.
 * @tparam L2 Second \c type_list.
 *
 * @pre \p L1 and \p L2 are both \c type_list specialisations.
 * @post None.
 */
template <typename L1, typename L2>
struct tl_concat;

template <typename... As, typename... Bs>
struct tl_concat<type_list<As...>, type_list<Bs...>> {
  using type = type_list<As..., Bs...>;
};

/// @brief Convenience alias for \c tl_concat<L1, L2>::type.
template <typename L1, typename L2>
using tl_concat_t = typename tl_concat<L1, L2>::type;

/**
 * @brief Applies a unary type trait to every element of a \c type_list.
 *
 * Exposes \c type, the list whose i-th element is \c F<element_i>::type.
 *
 * @tparam L A \c type_list.
 * @tparam F Unary class template exposing a nested \c type for each element.
 *
 * @pre \p L is a \c type_list and \c F<T>::type is valid for every element.
 * @post None.
 */
template <typename L, template <typename> class F>
struct tl_transform;

template <template <typename> class F, typename... Ts>
struct tl_transform<type_list<Ts...>, F> {
  using type = type_list<typename F<Ts>::type...>;
};

/// @brief Convenience alias for \c tl_transform<L, F>::type.
template <typename L, template <typename> class F>
using tl_transform_t = typename tl_transform<L, F>::type;

namespace detail {

template <template <typename> class Pred, typename Acc, typename... Ts>
struct tl_filter_impl;

template <template <typename> class Pred, typename Acc>
struct tl_filter_impl<Pred, Acc> {
  using type = Acc;
};

template <template <typename> class Pred, typename... Acc, typename Head, typename... Tail>
struct tl_filter_impl<Pred, type_list<Acc...>, Head, Tail...>
    : tl_filter_impl<
        Pred,
        std::conditional_t<Pred<Head>::value, type_list<Acc..., Head>, type_list<Acc...>>,
        Tail...> {};

}  // namespace detail

/**
 * @brief Keeps only the elements of a \c type_list satisfying a predicate.
 *
 * Exposes \c type, the elements \c T for which \c Pred<T>::value is \c true,
 * in their original order.
 *
 * @tparam L A \c type_list.
 * @tparam Pred Unary class template exposing a \c bool \c value per element.
 *
 * @pre \p L is a \c type_list and \c Pred<T>::value is valid for every element.
 * @post None.
 */
template <typename L, template <typename> class Pred>
struct tl_filter;

template <template <typename> class Pred, typename... Ts>
struct tl_filter<type_list<Ts...>, Pred> : detail::tl_filter_impl<Pred, type_list<>, Ts...> {};

/// @brief Convenience alias for \c tl_filter<L, Pred>::type.
template <typename L, template <typename> class Pred>
using tl_filter_t = typename tl_filter<L, Pred>::type;

namespace detail {

template <typename Acc, typename... Ts>
struct tl_unique_impl;

template <typename Acc>
struct tl_unique_impl<Acc> {
  using type = Acc;
};

template <typename... Acc, typename Head, typename... Tail>
struct tl_unique_impl<type_list<Acc...>, Head, Tail...>
    : tl_unique_impl<
        std::conditional_t<
          (std::is_same_v<Head, Acc> || ...),
          type_list<Acc...>,
          type_list<Acc..., Head>>,
        Tail...> {};

}  // namespace detail

/**
 * @brief Removes duplicate types from a \c type_list, keeping the first.
 *
 * Exposes \c type, the list with later duplicates dropped, preserving the
 * order of first occurrences.
 *
 * @tparam L A \c type_list.
 *
 * @pre \p L is a \c type_list specialisation.
 * @post None.
 */
template <typename L>
struct tl_unique;

template <typename... Ts>
struct tl_unique<type_list<Ts...>> : detail::tl_unique_impl<type_list<>, Ts...> {};

/// @brief Convenience alias for \c tl_unique<L>::type.
template <typename L>
using tl_unique_t = typename tl_unique<L>::type;

}  // namespace nexenne::utility
