#pragma once

/**
 * @file
 * @brief RAII scope-exit guard whose cleanup can be dismissed or re-armed.
 */

#include <concepts>
#include <type_traits>
#include <utility>

namespace nexenne::utility {

/**
 * @brief RAII scope-exit guard that runs a callable on destruction unless dismissed.
 *
 * Holds a no-argument callable and an active flag. On destruction the callable
 * runs only while the guard is active, so cleanup can be cancelled with
 * \c dismiss or re-armed with \c engage. The callable is stored by type, not
 * type-erased, so captured lambdas inline without indirection. Use \c defer
 * when the cleanup always runs. Non-copyable and non-movable, and
 * \c [[nodiscard]] so a discarded temporary (which would run the cleanup
 * immediately) is a compile-time warning.
 *
 * @tparam Fn Callable invocable as an lvalue with no arguments.
 *
 * @pre None.
 * @post A freshly constructed guard is active.
 *
 * @par Example
 * \code
 * auto* const handle{acquire()};
 * auto guard{nexenne::utility::scope_guard{[&] { release(handle); }}};
 * // ... work that might return early or throw ...
 * guard.dismiss(); // cancel cleanup once the resource is handed off
 * \endcode
 */
template <typename Fn>
  requires std::invocable<Fn&>
class [[nodiscard]] scope_guard final {
public:
  using function_type = Fn;  ///< Cleanup callable run at scope exit unless dismissed.

private:
  function_type m_fn;
  bool m_active{true};

  /**
   * @brief Initialises the stored callable from the caller's argument, P0052 style.
   *
   * Follows P0052 \c scope_exit: the forwarded argument is used when that
   * cannot throw; otherwise the member is copied from the caller's object,
   * which a failure therefore leaves intact, and that intact object runs
   * before the exception propagates, so the freshly armed cleanup is never
   * lost or run on a half-moved callable. A move-only callable whose move can
   * throw has no copy to fall back on, so it is moved, and on failure the
   * caller's object runs in whatever state its throwing move left it. The
   * result is elided straight into \c m_fn.
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
   * @brief Constructs an active guard from the cleanup callable \p fn.
   *
   * Matches P0052 \c scope_exit: \p fn is moved in only when that cannot
   * throw and copied otherwise, so if initialising the guard throws, the
   * caller's intact \p fn runs (the freshly armed cleanup is never silently
   * lost) before the exception propagates and no guard is constructed. That
   * holds whether \p fn is an lvalue, a moved-from name, or a temporary.
   *
   * @tparam G Type of the callable argument, forwarded.
   * @param fn Callable to run at scope exit while the guard is active.
   *
   * @pre None.
   * @post The guard is active and holds the callable.
   *
   * @throws Anything initialising the stored callable throws, after \p fn has
   *         been invoked.
   */
  template <typename G>
    requires(!std::same_as<std::remove_cvref_t<G>, scope_guard>)
            && std::constructible_from<function_type, G>
            && std::invocable<std::remove_reference_t<G>&>
  explicit scope_guard(G&& fn) noexcept(
    std::is_nothrow_constructible_v<function_type, G>
    || std::is_nothrow_constructible_v<function_type, G&>
  )
      : m_fn{guarded_init<G>(fn)} {}

  scope_guard(scope_guard const&) = delete;
  auto operator=(scope_guard const&) -> scope_guard& = delete;

  /**
   * @brief Runs the stored callable when the guard is still active.
   *
   * @pre None.
   * @post The callable has run exactly once if the guard was active, and not
   *       at all if it was dismissed.
   *
   * @warning The destructor is conditionally \c noexcept: on a normal scope
   *          exit a throwing callable propagates its exception to the caller,
   *          but if it throws while the destructor runs during stack
   *          unwinding, the program terminates, per the usual
   *          destructor-throws rule.
   */
  ~scope_guard() noexcept(noexcept(m_fn())) {
    if (m_active) {
      m_fn();
    }
  }

  /**
   * @brief Cancels the guard so the callable will not run at destruction.
   *
   * @pre None.
   * @post \c is_active() returns \c false.
   */
  auto dismiss() noexcept -> void {
    m_active = false;
  }

  /**
   * @brief Re-arms a previously dismissed guard.
   *
   * @pre None.
   * @post \c is_active() returns \c true.
   */
  auto engage() noexcept -> void {
    m_active = true;
  }

  /**
   * @brief Reports whether the guard will run its callable at destruction.
   *
   * @return \c true while the guard is armed, \c false after \c dismiss.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_active() const noexcept -> bool {
    return m_active;
  }
};

/**
 * @brief Deduces \c scope_guard's \c Fn from its constructor argument.
 *
 * @tparam Fn Callable type of the constructor argument.
 *
 * @pre None.
 * @post \c scope_guard{fn} deduces \c scope_guard<Fn>.
 */
template <typename Fn>
scope_guard(Fn) -> scope_guard<Fn>;

}  // namespace nexenne::utility
