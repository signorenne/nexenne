#pragma once

/**
 * @file
 * @brief The edge transport abstraction: the edge_sink concepts.
 *
 * An edge sink is the seam between whoever produces edge events (a backend
 * poll loop, an interrupt handler on an embedded target, a test rig) and
 * whoever consumes them. Keeping the transport a separate concept means the
 * delivery mechanism, a direct callback, a lock-free ring, or a
 * user-supplied type over an RTOS queue, is chosen independently of the
 * backend that produces the events, and the same producer code targets any
 * of them with no virtual dispatch.
 *
 * \c push must be \c noexcept because on embedded targets it runs in
 * interrupt context. Its \c bool result is the drop-accounting hook: a full
 * transport returns \c false and the producer counts the loss instead of
 * blocking, which is the only correct behaviour in an interrupt.
 */

#include <concepts>
#include <optional>

#include <nexenne/gpio/line_event.hpp>

namespace nexenne::gpio {

/**
 * @brief Anything an edge producer can push a raw event into.
 *
 * A type models \c edge_sink when it offers a \c noexcept
 * \c push(line_event const&) returning \c bool: \c true when the event was
 * accepted, \c false when the transport was full and the event was dropped.
 *
 * @tparam S Candidate sink type.
 */
template <typename S>
concept edge_sink = requires(S sink, line_event const& event) {
  { sink.push(event) } noexcept -> std::same_as<bool>;
};

/**
 * @brief An edge sink a consumer can drain without blocking.
 *
 * Adds a \c noexcept \c try_pop() returning the next buffered event, or
 * \c std::nullopt when none is ready. This is the delivery path that needs
 * no OS: the producer pushes from its context and a consumer polls, so it
 * works identically on Linux and on a bare-metal main loop.
 *
 * @tparam S Candidate sink type.
 */
template <typename S>
concept draining_edge_sink = edge_sink<S> && requires(S sink) {
  { sink.try_pop() } noexcept -> std::same_as<std::optional<line_event>>;
};

}  // namespace nexenne::gpio
