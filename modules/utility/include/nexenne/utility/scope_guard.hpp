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
  using function_type = Fn;

private:
  function_type m_fn;
  bool m_active{true};

  /**
   * @brief Move-constructs the stored callable, running \p fn if the move throws.
   *
   * P0052 scope_exit semantics: if moving the callable into the member throws,
   * the cleanup must not be silently lost, so \p fn runs immediately (via the
   * still-intact argument) before the exception propagates. The return object is
   * the member itself (guaranteed elision), so the catch sees the real move. The
   * catch path exists only when the move can actually throw, so the \c noexcept
   * instantiation contains no unreachable rethrow.
   *
   * @param fn Callable to move into the guard; invoked if the move throws.
   *
   * @return The callable, move-constructed from \p fn.
   *
   * @pre None.
   * @post The returned callable owns the moved-from state of \p fn.
   *
   * @throws Anything the move of \p fn throws, after \p fn has been invoked.
   */
  [[nodiscard]] static auto
  guarded_move(function_type& fn) noexcept(std::is_nothrow_move_constructible_v<function_type>)
    -> function_type {
    if constexpr (std::is_nothrow_move_constructible_v<function_type>) {
      return std::move(fn);
    } else {
      try {
        return std::move(fn);
      } catch (...) {
        fn();
        throw;
      }
    }
  }

public:
  /**
   * @brief Constructs an active guard, taking ownership of \p fn.
   *
   * Matches P0052 \c scope_exit: if moving \p fn into the guard throws, \p fn
   * is invoked immediately (the freshly armed cleanup is never silently lost)
   * and the exception then propagates; no guard is constructed in that case.
   *
   * @param fn Callable to run at scope exit while active, moved into the guard.
   *
   * @pre None.
   * @post The guard is active and holds \p fn.
   *
   * @throws Anything the move of \p fn throws, after \p fn has been invoked.
   */
  explicit scope_guard(
    function_type fn
  ) noexcept(std::is_nothrow_move_constructible_v<function_type>)
      : m_fn{guarded_move(fn)} {}

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
