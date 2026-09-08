#pragma once

/**
 * @file
 * @brief RAII scope-exit guard that always runs a callable on destruction.
 */

#include <concepts>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief RAII scope-exit guard that runs a callable when it leaves scope.
 *
 * Stores a no-argument callable and invokes it exactly once on destruction.
 * The cleanup always runs; there is no dismiss or engage, unlike
 * \c scope_guard. The guard is non-copyable and non-movable, so it is bound to
 * the scope it is declared in, and is \c [[nodiscard]] so a discarded
 * temporary (which would run the cleanup immediately) is a compile-time
 * warning.
 *
 * @tparam Fn Callable invocable as an lvalue with no arguments.
 *
 * @pre None.
 * @post The stored callable runs once, at scope exit.
 *
 * @par Example
 * \code
 * auto* const handle{acquire()};
 * auto const guard{nexenne::utility::defer{[&] { release(handle); }}};
 * // release(handle) runs at scope exit, even on an early return.
 * \endcode
 */
template <typename Fn>
  requires std::invocable<Fn&>
class [[nodiscard]] defer final {
public:
  using function_type = Fn;  ///< Cleanup callable run at scope exit.

private:
  function_type m_fn;

  /**
   * @brief Initialises the stored callable from the caller's argument, P0052 style.
   *
   * Follows P0052 \c scope_exit: the forwarded argument is used when that
   * cannot throw; otherwise the member is copied from the caller's object,
   * which a failure therefore leaves intact, and that intact object runs
   * before the exception propagates, so the cleanup is never lost or run on a
   * half-moved callable. A move-only callable whose move can throw has no
   * copy to fall back on, so it is moved, and on failure the caller's object
   * runs in whatever state its throwing move left it. The result is elided
   * straight into \c m_fn.
   *
   * @tparam G Forwarded argument type.
   * @param fn The caller's callable; invoked if initialising the member throws.
   *
   * @return The callable to store.
   *
   * @pre None.
   * @post On a throw, \p fn has been invoked exactly once.
   *
   * @throws Anything initialising the member throws, after \p fn has run.
   */
  template <typename G>
  [[nodiscard]] static auto guarded_init(G& fn) noexcept(
    std::is_nothrow_constructible_v<function_type, G>
    || std::is_nothrow_constructible_v<function_type, G&>
  ) -> function_type {
    if constexpr (std::is_nothrow_constructible_v<function_type, G>) {
      return static_cast<function_type>(std::forward<G>(fn));
    } else if constexpr (std::is_nothrow_constructible_v<function_type, G&>) {
      return static_cast<function_type>(fn);
    } else if constexpr (std::is_constructible_v<function_type, G&>) {
      try {
        return static_cast<function_type>(fn);
      } catch (...) {
        fn();
        throw;
      }
    } else {
      try {
        return static_cast<function_type>(std::forward<G>(fn));
      } catch (...) {
        fn();
        throw;
      }
    }
  }

public:
  /**
   * @brief Constructs the guard from the cleanup callable \p fn.
   *
   * Matches P0052 \c scope_exit: \p fn is moved in only when that cannot
   * throw and copied otherwise, so if initialising the guard throws, the
   * caller's intact \p fn runs before the exception propagates and no guard is
   * constructed. The cleanup is never silently lost, whether \p fn is an
   * lvalue, a moved-from name, or a temporary.
   *
   * @tparam G Type of the callable argument, forwarded.
   * @param fn Callable to run at scope exit.
   *
   * @pre None.
   * @post The guard holds the callable and will invoke it on destruction.
   *
   * @throws Anything initialising the stored callable throws, after \p fn has
   *         been invoked.
   */
  template <typename G>
    requires(!std::same_as<std::remove_cvref_t<G>, defer>)
            && std::constructible_from<function_type, G>
            && std::invocable<std::remove_reference_t<G>&>
  explicit defer(G&& fn) noexcept(
    std::is_nothrow_constructible_v<function_type, G>
    || std::is_nothrow_constructible_v<function_type, G&>
  )
      : m_fn{guarded_init<G>(fn)} {}

  defer(defer const&) = delete;
  auto operator=(defer const&) -> defer& = delete;

  /**
   * @brief Runs the stored callable.
   *
   * @pre None.
   * @post The stored callable has been invoked exactly once.
   *
   * @warning The callable runs unconditionally. The destructor is
   *          conditionally \c noexcept: on a normal scope exit a throwing
   *          callable propagates its exception to the caller, but if it throws
   *          while the destructor runs during stack unwinding, the program
   *          terminates, per the usual destructor-throws rule.
   */
  ~defer() noexcept(noexcept(m_fn())) {
    m_fn();
  }
};

/**
 * @brief Deduces \c defer's \c Fn from its constructor argument.
 *
 * @tparam Fn Callable type of the constructor argument.
 *
 * @pre None.
 * @post \c defer{fn} deduces \c defer<Fn>.
 */
template <typename Fn>
defer(Fn) -> defer<Fn>;

}  // namespace nexenne::utility
