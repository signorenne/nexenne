#pragma once

/**
 * @file
 * @brief Concepts shared by the nexenne::chrono module.
 *
 *   - \c chrono_duration : any \c std::chrono::duration specialisation.
 *   - \c clock_like      : exposes \c now() returning a \c time_point.
 *   - \c steady_clock_like : \c clock_like plus a compile-time
 *                            \c is_steady = true.
 *   - \c tick_backend    : a low-level tick source suitable for
 *                          wrapping in \c tick_clock (rep / period /
 *                          \c is_steady / \c ticks()).
 *
 * These keep templates honest about what they need, without leaking
 * implementation details into call sites.
 */

#include <chrono>
#include <concepts>
#include <ratio>
#include <type_traits>

namespace nexenne::chrono {

/**
 * @brief Satisfied by any \c std::chrono::duration specialisation.
 *
 * Checks that \p T (after removing cv and reference qualifiers) exposes
 * \c rep and \c period member types and is exactly the
 * \c std::chrono::duration built from them.
 *
 * @tparam T Type under test.
 *
 * @pre None.
 * @post None.
 */
template <typename T>
concept chrono_duration =
  requires {
    typename std::remove_cvref_t<T>::rep;
    typename std::remove_cvref_t<T>::period;
  }
  && std::same_as<
    std::remove_cvref_t<T>,
    std::chrono::
      duration<typename std::remove_cvref_t<T>::rep, typename std::remove_cvref_t<T>::period>>;

/**
 * @brief Satisfied by a Chrono-style clock type.
 *
 * Requires the nested \c rep, \c period, \c duration, and \c time_point
 * types and a static \c now() returning that \c time_point.
 *
 * @tparam C Type under test.
 *
 * @pre None.
 * @post None.
 */
template <typename C>
concept clock_like = requires {
  typename C::rep;
  typename C::period;
  typename C::duration;
  typename C::time_point;
  { C::now() } -> std::same_as<typename C::time_point>;
};

/**
 * @brief Satisfied by a \c clock_like type that is monotonic.
 *
 * Refines \c clock_like by additionally requiring a compile-time
 * \c is_steady that evaluates to \c true, the contract that the clock
 * never runs backward.
 *
 * @tparam C Type under test.
 *
 * @pre None.
 * @post None.
 */
template <typename C>
concept steady_clock_like = clock_like<C> && requires {
  { C::is_steady } -> std::convertible_to<bool>;
} && static_cast<bool>(C::is_steady);

namespace detail {

template <class T>
struct is_positive_ratio : std::false_type {};

template <std::intmax_t N, std::intmax_t D>
struct is_positive_ratio<std::ratio<N, D>> : std::bool_constant<(N > 0) && (D > 0)> {};

template <class P>
concept chrono_period = is_positive_ratio<std::remove_cvref_t<P>>::value;

}  // namespace detail

/**
 * @brief Backend contract consumed by \c tick_clock.
 *
 * A \p B satisfies this concept when it exposes a signed-integral \c rep, a
 * positive \c std::ratio \c period giving seconds per tick, a compile-time
 * \c is_steady convertible to \c bool, and a static \c ticks() that is
 * \c noexcept and returns \c rep.
 *
 * @tparam B Type under test.
 *
 * @pre None.
 * @post None.
 */
template <class B>
concept tick_backend =
  requires {
    typename B::rep;
    typename B::period;
  } && detail::chrono_period<typename B::period> && std::signed_integral<typename B::rep>
  && requires {
       { B::is_steady } -> std::convertible_to<bool>;
     } && requires {
       { B::ticks() } noexcept -> std::same_as<typename B::rep>;
     };

}  // namespace nexenne::chrono
