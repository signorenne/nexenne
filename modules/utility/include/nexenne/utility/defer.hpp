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
 * @tparam Fn Callable invocable with no arguments.
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
template <std::invocable Fn>
class [[nodiscard]] defer final {
public:
  using function_type = Fn;

private:
  function_type m_fn;

public:
  /**
   * @brief Constructs the guard, taking ownership of \p fn.
   *
   * @param fn Callable to run at scope exit, moved into the guard.
   *
   * @pre None.
   * @post The guard holds \p fn and will invoke it on destruction.
   */
  explicit defer(function_type fn) noexcept(std::is_nothrow_move_constructible_v<function_type>)
      : m_fn{std::move(fn)} {}

  defer(defer const&) = delete;
  auto operator=(defer const&) -> defer& = delete;

  /**
   * @brief Runs the stored callable.
   *
   * @pre None.
   * @post The stored callable has been invoked exactly once.
   *
   * @warning The callable runs unconditionally. If it throws while the
   *          destructor runs during stack unwinding, the program terminates,
   *          per the usual destructor-throws rule.
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
