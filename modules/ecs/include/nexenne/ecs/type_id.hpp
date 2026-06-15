#pragma once

/**
 * @file
 * @brief Compile-time-stable, runtime-cheap unique IDs per type, with
 *        no RTTI dependency.
 *
 * \c type_id<T>() returns a \c std::size_t that is:
 *
 *   - Unique per type \p T within a single translation-unit graph.
 *   - Dense and contiguous: the first type whose \c type_id<T>() is
 *     evaluated gets \c 0, the second \c 1, and so on. This makes the
 *     IDs usable directly as indices into a flat vector, which is
 *     what \c registry uses to look up a component storage by type.
 *   - \c constexpr-friendly at the call site (the static-local
 *     initialization runs once per program, then every subsequent
 *     call is a single load).
 *
 * The implementation relies on the C++ "one static-local per
 * instantiation" rule. Each \c type_id<T> instantiation has its own
 * static \c id whose initializer increments a shared counter exactly
 * once, on first call. The order of those increments is the order in
 * which the program first asks about each type, so do not depend on
 * IDs being stable across runs that touch the types in different
 * orders, or across separate executables.
 *
 * If you need IDs that are stable across processes (serialization,
 * networking, plugin boundaries), this header is the wrong tool:
 * use a string hash of \c typeid(T).name() or a hand-rolled registry
 * keyed by user-supplied strings.
 */

#include <cstddef>

namespace nexenne::ecs {

namespace detail {

/**
 * @brief Returns the next free dense ID and advances the shared counter.
 *
 * Reads the function-local \c counter, returns its current value, then
 * post-increments it. Called exactly once per distinct \c type_id<T>
 * instantiation, from that instantiation's static-local initializer.
 *
 * @return The pre-increment value of the shared counter: \c 0 on the
 *         first call of the program, \c 1 on the second, and so on.
 *
 * @pre  None.
 * @post The shared counter is one greater than the returned value.
 */
[[nodiscard]] inline auto next_type_id() noexcept -> std::size_t {
  static std::size_t counter{0};
  return counter++;
}

}  // namespace detail

/**
 * @brief Returns the unique, dense ID for type \p T.
 *
 * The first call for a given \p T runs the static-local initializer,
 * which consumes one slot from the shared counter; every subsequent
 * call for the same \p T is a single load of the cached ID. IDs are
 * assigned in first-touch order, so they are dense and contiguous
 * starting at \c 0 but not stable across runs that touch types in a
 * different order.
 *
 * @tparam T  Any type. \c T need not be complete.
 *
 * @return Small dense \c std::size_t ID, suitable as an array index.
 *
 * @pre  None.
 * @post The returned ID is stable for \p T for the remainder of the
 *       program, and is strictly less than the number of distinct
 *       types whose ID has been requested.
 *
 * @warning IDs are per-process and assigned by first-touch order. Do
 *          not persist them or share them across executables.
 */
template <typename T>
[[nodiscard]] inline auto type_id() noexcept -> std::size_t {
  static std::size_t const id{detail::next_type_id()};
  return id;
}

}  // namespace nexenne::ecs
