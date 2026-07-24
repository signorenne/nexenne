#pragma once

/**
 * @file
 * @brief The zero-overhead edge transport: deliver straight into a callable.
 *
 * When the producer and the consumer live in the same context (a poll loop
 * that handles each event as it drains it), buffering is pure overhead. A
 * \c callback_sink satisfies \c edge_sink by invoking a caller-supplied
 * callable on every push, so producer code written against the sink concept
 * needs no second code path for the direct case. A stateless callable costs
 * no storage thanks to \c [[no_unique_address]].
 *
 * The callable must be \c noexcept: push runs wherever the producer runs,
 * interrupt context included, and nothing may throw across that boundary.
 */

#include <concepts>
#include <type_traits>
#include <utility>

#include <nexenne/gpio/line_event.hpp>

namespace nexenne::gpio {

/**
 * @brief An edge sink that hands every pushed event to a callable.
 *
 * Satisfies \c edge_sink. The callable's \c bool result is forwarded as the
 * push result, so a handler can itself report a drop (for example when it
 * relays into a further, bounded transport).
 *
 * @tparam Handler Callable taking \c line_event const& and returning
 *                 \c bool, invocable \c noexcept.
 */
template <typename Handler>
  requires requires(Handler& handler, line_event const& event) {
    { handler(event) } noexcept -> std::same_as<bool>;
  }
class callback_sink {
public:
  using value_type = line_event;
  /// The callable type events are delivered to.
  using handler_type = Handler;

private:
  [[no_unique_address]] handler_type m_handler;

public:
  /**
   * @brief Constructs a sink around a handler.
   *
   * @param handler Callable to invoke on every push.
   *
   * @pre None.
   * @post Every \c push invokes a copy of \p handler.
   */
  explicit constexpr callback_sink(handler_type handler) noexcept(
    std::is_nothrow_move_constructible_v<handler_type>
  )
    : m_handler{std::move(handler)} {}

  /**
   * @brief Delivers one event to the handler.
   *
   * @param event Event to deliver.
   *
   * @return The handler's result: \c true when the event was consumed.
   *
   * @pre None.
   * @post The handler was invoked exactly once with \p event.
   *
   * @complexity \c O(1) plus the handler.
   */
  constexpr auto push(line_event const& event) noexcept -> bool {
    return m_handler(event);
  }
};

}  // namespace nexenne::gpio
