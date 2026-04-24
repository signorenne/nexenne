#pragma once

/**
 * @file
 * @brief Configuration a CAN bus backend is opened with.
 *
 * These are the knobs common to the bus backends: whether CAN FD frames are
 * enabled, whether a backend echoes the frames it sends back to its own receive
 * path, and whether receiving is non-blocking. The in-memory loopback bus and
 * the Linux SocketCAN backend both read this struct; a backend ignores an option
 * it does not support.
 */

#include <cstdint>

namespace nexenne::can {

/**
 * @brief Options controlling how a CAN bus backend behaves.
 */
struct socket_options {
  bool fd_enabled{false};            ///< Accept and transmit CAN FD frames.
  bool receive_own_messages{true};   ///< Echo sent frames back to the receive path.
  bool nonblocking{true};            ///< Receiving returns no frame instead of waiting.
  std::uint32_t read_timeout_ms{0};  ///< Blocking read timeout in ms; 0 means none.

  /**
   * @brief Equality over all options.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when every option is equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(socket_options const& lhs, socket_options const& rhs) noexcept -> bool = default;
};

}  // namespace nexenne::can
