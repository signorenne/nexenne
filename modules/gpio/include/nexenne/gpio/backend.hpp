#pragma once

/**
 * @file
 * @brief The backend abstraction: the gpio_backend concept ladder.
 *
 * A GPIO backend is anything that can open a set of lines, read and write
 * levels by offset, and close the request again. Rather than a virtual
 * interface, the abstraction is a C++ concept, so a backend is a concrete
 * type resolved at compile time: the calls inline, there is no vtable, and
 * the read and write path stays allocation-free. The in-memory
 * \c io/mock_chip.hpp and, on Linux, the character-device backend both
 * satisfy \c gpio_backend, and an embedded target supplies its own type over
 * the vendor HAL; portable code written against the concept runs unchanged
 * on all of them.
 *
 * Capabilities stack as refinements, so a minimal backend stays legal:
 * - \c gpio_backend : open, close, and synchronous per-line read and write.
 * - \c bulk_gpio_backend : adds atomic multi-line reads and writes.
 * - \c edge_source : adds timestamped edge events and a pollable native
 *   handle for event-loop integration (epoll, Qt, ASIO all drive an fd).
 *
 * Backends exchange raw PHYSICAL levels only. Polarity is applied above, by
 * the handles and the decode step, which is what keeps every backend
 * drop-in interchangeable.
 */

#include <chrono>
#include <concepts>
#include <optional>
#include <span>

#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief A backend that opens a set of lines and reads and writes them by offset.
 *
 * A type models \c gpio_backend when it offers, with these exact return
 * types: \c open(std::span<line_spec const>, std::span<line_config const>)
 * returning \c result<void>, requesting all lines in one call because many
 * kernels and HALs configure a request set atomically; a \c noexcept
 * \c close(); a const \c noexcept \c is_open() returning \c bool; a const
 * \c read(line_offset) returning \c result<bool> with the raw physical
 * level; and \c write(line_offset, bool) taking a physical level and
 * returning \c result<void>.
 *
 * @tparam B Candidate backend type.
 */
template <typename B>
concept gpio_backend = requires(
  B backend,
  B const const_backend,
  std::span<line_spec const> const specs,
  std::span<line_config const> const configs,
  line_offset const offset,
  bool const level
) {
  { backend.open(specs, configs) } -> std::same_as<result<void>>;
  { backend.close() } noexcept -> std::same_as<void>;
  { const_backend.is_open() } noexcept -> std::same_as<bool>;
  { const_backend.read(offset) } -> std::same_as<result<bool>>;
  { backend.write(offset, level) } -> std::same_as<result<void>>;
};

/**
 * @brief A backend that also reads and writes several lines in one operation.
 *
 * Adds \c read_lines and \c write_lines over parallel spans of offsets and
 * physical levels. The point of the bulk tier is atomicity where the
 * hardware offers it (one register access, one kernel call); a backend whose
 * transport has no bulk primitive simply does not model this tier, and
 * callers fall back to per-line operations.
 *
 * @tparam B Candidate backend type.
 */
template <typename B>
concept bulk_gpio_backend = gpio_backend<B>
                            && requires(
                              B backend,
                              B const const_backend,
                              std::span<line_offset const> const offsets,
                              std::span<bool> const levels_out,
                              std::span<bool const> const levels_in
                            ) {
                                 {
                                   const_backend.read_lines(offsets, levels_out)
                                 } -> std::same_as<result<void>>;
                                 {
                                   backend.write_lines(offsets, levels_in)
                                 } -> std::same_as<result<void>>;
                               };

/**
 * @brief A backend that can change line configuration without reopening.
 *
 * Adds \c reconfigure over the SAME lines as the active request: the spec
 * and config tables must address the request's lines element by element,
 * and only the behaviour changes (direction, edges, debounce, bias, drive,
 * output levels). The point of the tier is that the request is never
 * released: exclusivity is not lost to a competing consumer and output
 * lines never glitch through an unconfigured state, which a close-and-
 * reopen cannot guarantee.
 *
 * @tparam B Candidate backend type.
 */
template <typename B>
concept reconfigurable_gpio_backend =
  gpio_backend<B>
  && requires(
    B backend, std::span<line_spec const> const specs, std::span<line_config const> const configs
  ) {
       { backend.reconfigure(specs, configs) } -> std::same_as<result<void>>;
     };

/**
 * @brief A backend that also delivers timestamped edge events.
 *
 * Adds \c wait_event(timeout) returning \c result<std::optional<line_event>>,
 * where \c std::nullopt means the timeout elapsed with no event (a clean
 * miss, not an error) and a zero timeout is a non-blocking poll. The
 * \c native_handle() accessor exposes the backend's pollable handle (a file
 * descriptor on Linux) so an external event loop, epoll, a Qt socket
 * notifier, or ASIO, can wait for readiness and then drain with a zero
 * timeout; the module never owns the loop.
 *
 * @tparam B Candidate backend type; must expose a \c native_handle_type.
 */
template <typename B>
concept edge_source =
  gpio_backend<B>
  && requires(B backend, B const const_backend, std::chrono::nanoseconds const timeout) {
       typename B::native_handle_type;
       { backend.wait_event(timeout) } -> std::same_as<result<std::optional<line_event>>>;
       { const_backend.native_handle() } noexcept -> std::same_as<typename B::native_handle_type>;
     };

}  // namespace nexenne::gpio
