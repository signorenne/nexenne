#pragma once

/**
 * @file
 * @brief RAII timer that fires a callback on destruction with the
 *        elapsed duration since construction.
 *
 * Sprinkle one of these in a scope to measure how long that scope
 * took:
 *
 * \code
 * {
 *     auto t{scope_timer{[](auto dur) {
 *         LOG_INFO("scope took {} us",
 *                  std::chrono::duration_cast<std::chrono::microseconds>(dur).count());
 *     }}};
 *     do_work();
 * }   // callback fires here
 * \endcode
 *
 * The callback is templated, so a captured lambda has no
 * \c std::function indirection cost. The timer is non-copyable
 * and non-movable (it is meant to live in exactly one scope).
 *
 * If the callback throws, the destructor will propagate (callback
 * itself is invoked unguarded). Wrap your own callback if you need
 * exception swallowing.
 *
 * \c elapsed() reports the duration so far without firing the
 * callback, useful for assertions inside the scope.
 *
 * @tparam Callback Callable taking \c (Clock::duration).
 * @tparam Clock Steady clock to measure with.
 */

#include <chrono>
#include <concepts>
#include <type_traits>
#include <utility>

#include <nexenne/chrono/concepts.hpp>

namespace nexenne::chrono {

/**
 * @brief RAII timer that invokes a callback with the scope's elapsed time.
 *
 * Captures \c Clock::now() at construction and, at destruction, calls the
 * stored callback with the duration since then. The callback type is a
 * template parameter, so a captured lambda incurs no \c std::function
 * indirection. The type is non-copyable and non-movable: it is meant to live
 * in exactly one scope.
 *
 * @tparam Callback Callable taking a \c Clock::duration.
 * @tparam Clock Steady clock to measure with.
 *
 * @pre None.
 * @post None.
 *
 * @warning If the callback throws, the throwing destructor propagates the
 *          exception; wrap the callback if you need exception swallowing.
 */
template <typename Callback, steady_clock_like Clock = std::chrono::steady_clock>
  requires std::invocable<Callback&, typename Clock::duration>
class scope_timer {
public:
  using clock_type = Clock;
  using duration = typename Clock::duration;
  using time_point = typename Clock::time_point;

private:
  time_point m_start{};
  // No default init: Callback (e.g. a capturing lambda) need not be
  // default-constructible; the constructor always sets it.
  Callback m_cb;

public:
  /**
   * @brief Start timing and store the callback.
   *
   * @param cb Callback invoked with the elapsed duration at destruction.
   *
   * @pre None.
   * @post Timing has started from \c Clock::now() and \p cb is stored.
   */
  explicit scope_timer(Callback cb) noexcept(std::is_nothrow_move_constructible_v<Callback>)
      : m_start{Clock::now()}, m_cb{std::move(cb)} {}

  scope_timer(scope_timer const&) = delete;
  auto operator=(scope_timer const&) -> scope_timer& = delete;
  scope_timer(scope_timer&&) = delete;
  auto operator=(scope_timer&&) -> scope_timer& = delete;

  /**
   * @brief Fire the callback with the scope's total elapsed time.
   *
   * @pre None.
   * @post The stored callback has been invoked once with the elapsed time.
   * @throws Whatever the stored callback throws; it is invoked unguarded.
   */
  ~scope_timer() noexcept(noexcept(m_cb(duration{}))) {
    m_cb(elapsed());
  }

  /**
   * @brief Elapsed time since construction, without firing the callback.
   *
   * @return The duration since the timer started.
   *
   * @pre None.
   * @post The result is greater than or equal to \c duration::zero().
   */
  [[nodiscard]] auto elapsed() const noexcept -> duration {
    return Clock::now() - m_start;
  }

  /**
   * @brief Elapsed time since construction in the units \p D.
   *
   * @tparam D Duration type the result is cast to.
   *
   * @return The elapsed time, in units \p D.
   *
   * @pre None.
   * @post The result is greater than or equal to \c D::zero().
   */
  template <chrono_duration D>
  [[nodiscard]] auto elapsed() const noexcept -> D {
    return std::chrono::duration_cast<D>(elapsed());
  }
};

/**
 * @brief Deduction guide that infers \c scope_timer from its callback alone.
 *
 * @tparam Callback Callable taking a \c Clock::duration.
 *
 * @pre None.
 * @post None.
 */
template <typename Callback>
scope_timer(Callback) -> scope_timer<Callback>;

}  // namespace nexenne::chrono
