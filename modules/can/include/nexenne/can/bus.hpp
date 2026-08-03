#pragma once

/**
 * @file
 * @brief The bus backend abstraction: the \c can_bus concept and the bus state types.
 *
 * A CAN backend is anything that can send a frame, receive a frame, set hardware
 * or software filters, and report its error state. Rather than a virtual
 * interface, the abstraction is a C++ concept, so a backend is a concrete type
 * resolved at compile time: the calls inline, there is no vtable, and the send
 * and receive path stays allocation-free. The in-memory \c loopback_bus.hpp
 * and, on Linux, the SocketCAN backend both satisfy \c can_bus. A caller that
 * needs runtime-swappable backends can wrap one in a small adapter, but the
 * default surface is zero-overhead.
 */

#include <concepts>
#include <cstdint>
#include <optional>
#include <span>

#include <nexenne/can/error.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>

namespace nexenne::can {

/**
 * @brief The error confinement state of a CAN controller.
 *
 * A controller starts error-active, drops to error-passive as its error counters
 * rise, and goes bus-off when they exceed the limit, at which point it stops
 * taking part until it is reset. See ISO 11898-1 fault confinement.
 */
enum class bus_state : std::uint8_t {
  error_active,   ///< Normal operation; the controller signals active errors.
  error_passive,  ///< Elevated error counters; the controller signals passive errors.
  bus_off,        ///< The controller has left the bus after too many errors.
};

/**
 * @brief The transmit and receive error counters of a CAN controller.
 *
 * The counters rise on transmit and receive errors and fall on success; they
 * drive the transition between the \c bus_state levels.
 */
struct error_counters {
  std::uint8_t transmit{0};  ///< Transmit error counter.
  std::uint8_t receive{0};   ///< Receive error counter.

  /**
   * @brief Equality over both counters.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both counters are equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(error_counters const& lhs, error_counters const& rhs) noexcept -> bool = default;
};

/**
 * @brief A CAN bus backend that sends, receives, sets filters, and reports its state.
 *
 * A type models \c can_bus when it offers, with these exact return types:
 * \c send(frame const&) returning \c result<void>; \c receive() returning
 * \c result<std::optional<frame>>, where \c std::nullopt means no frame is ready
 * rather than an error; \c set_filters(std::span<filter const>) returning
 * \c result<void>; and a const \c state() returning \c bus_state.
 *
 * @tparam B Candidate backend type.
 */
template <typename B>
concept can_bus =
  requires(B bus, B const const_bus, frame const& f, std::span<filter const> const filters) {
    { bus.send(f) } -> std::same_as<result<void>>;
    { bus.receive() } -> std::same_as<result<std::optional<frame>>>;
    { bus.set_filters(filters) } -> std::same_as<result<void>>;
    { const_bus.state() } -> std::same_as<bus_state>;
  };

}  // namespace nexenne::can
