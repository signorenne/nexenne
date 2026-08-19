#pragma once

/**
 * @file
 * @brief A Linux SocketCAN backend that satisfies the can_bus concept.
 *
 * SocketCAN exposes a CAN interface as a network socket: a raw \c CAN_RAW socket
 * bound to an interface (\c can0, \c vcan0) carries one frame per datagram. This
 * backend wraps that socket as a \c bus.hpp \c can_bus, so code written
 * against the in-memory \c loopback_bus.hpp runs unchanged against real
 * hardware. A frame converts to and from the kernel \c can_frame and
 * \c canfd_frame by copying fields, because \c id.hpp already stores the
 * identifier in the kernel's bit layout; filters become the kernel's hardware
 * filter array; and error frames are decoded into a \c bus.hpp
 * \c bus_state through \c error_frame.hpp.
 *
 * This is Linux-only. On other platforms the type still exists and satisfies the
 * concept, but every operation returns \c can_error::unsupported, so generic code
 * compiles everywhere and fails cleanly off Linux.
 *
 * Reference: the Linux kernel SocketCAN documentation,
 * \c Documentation/networking/can.rst.
 */

#include <cassert>
#include <optional>
#include <span>
#include <string_view>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/error.hpp>
#include <nexenne/can/error_frame.hpp>
#include <nexenne/can/filter.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/socket_options.hpp>
#include <nexenne/utility/discard.hpp>

#ifdef __linux__

#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <ctime>
#include <utility>

#include <fcntl.h>
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <nexenne/container/static_vector.hpp>
#include <nexenne/utility/unique_resource.hpp>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nexenne::can {

namespace detail {

/**
 * @brief Closes a file descriptor; the deleter for the socket handle.
 */
struct socket_closer {
  /**
   * @brief Closes the descriptor \p fd.
   *
   * @param fd Descriptor to close.
   *
   * @pre None.
   * @post \p fd is closed.
   */
  auto operator()(int fd) const noexcept -> void;
};

}  // namespace detail

/**
 * @brief Converts a frame to a Classic CAN kernel struct.
 *
 * @param f Frame to convert; only the first 8 bytes are used.
 *
 * @return A \c can_frame holding the identifier, length, and payload of \p f,
 *         truncated to the 8 bytes the Classic struct holds. Use
 *         \c socketcan_bus::send to have an over-long frame refused rather
 *         than truncated.
 *
 * @post The result's \c can_id equals \c f.id().raw(), and its \c can_dlc is
 *       the number of bytes actually copied.
 */
[[nodiscard]] auto to_can_frame(frame const& f) noexcept -> ::can_frame;

/**
 * @brief Converts a frame to a CAN FD kernel struct.
 *
 * The on-wire length is padded up to the next discrete CAN FD size; the padding
 * bytes are zero.
 *
 * @param f Frame to convert.
 *
 * @return A \c canfd_frame holding the identifier, padded length, FD flags, and
 *         payload of \p f, truncated to the 64 bytes the FD struct holds.
 *
 * @post The result's \c can_id equals \c f.id().raw(), and its \c len is the
 *       padded length of the bytes actually copied.
 */
[[nodiscard]] auto to_canfd_frame(frame const& f) noexcept -> ::canfd_frame;

/**
 * @brief Converts a Classic CAN kernel struct to a frame.
 *
 * @param cf Kernel frame to convert.
 *
 * @return The equivalent \c frame, or \c can_error::invalid_dlc when the kernel
 *         length exceeds 8.
 *
 * @pre None.
 * @post On success \c is_fd() of the result is \c false.
 */
[[nodiscard]] auto from_can_frame(::can_frame const& cf) -> result<frame>;

/**
 * @brief Converts a CAN FD kernel struct to a frame.
 *
 * @param cf Kernel FD frame to convert.
 *
 * @return The equivalent \c frame, or \c can_error::invalid_dlc when the kernel
 *         length exceeds 64.
 *
 * @pre None.
 * @post On success \c is_fd() of the result is \c true.
 */
[[nodiscard]] auto from_canfd_frame(::canfd_frame const& cf) -> result<frame>;

/**
 * @brief A CAN bus backed by a Linux SocketCAN raw socket.
 *
 * Open one with \c open, then use it like any \c can_bus. The type owns the
 * socket and is move-only; it satisfies the \c can_bus concept.
 */
class socketcan_bus {
public:
  using value_type = frame;

  /// @brief Largest number of hardware filters installed in one call.
  static constexpr std::size_t max_filters{64};

private:
  using socket_handle = utility::unique_resource<int, detail::socket_closer>;

  socket_handle m_socket{};
  bool m_fd_enabled{false};
  bus_state m_state{bus_state::error_active};

  socketcan_bus(socket_handle&& socket, bool const fd_enabled) noexcept;

public:
  /**
   * @brief Opens a SocketCAN socket bound to a CAN interface.
   *
   * @param interface Interface name, such as \c "can0" or \c "vcan0".
   * @param options Socket options (CAN FD, receive own messages, non-blocking).
   *
   * @return The opened bus, or \c can_error::io_error when any socket call fails.
   *
   * @pre \p interface names an existing CAN interface.
   * @post On success the socket is bound and ready to send and receive.
   */
  [[nodiscard]] static auto
  open(std::string_view const interface, socket_options const& options = {})
    -> result<socketcan_bus>;

  /**
   * @brief Sends a frame on the bus.
   *
   * @param f Frame to send; an FD frame is written as a \c canfd_frame.
   *
   * @return Empty on success, \c can_error::unsupported when \p f is a CAN FD
   *         frame but the socket was not opened with \c fd_enabled, or
   *         \c can_error::io_error when the write fails.
   *
   * @pre The bus is open.
   * @post On success the frame has been handed to the kernel.
   */
  auto send(frame const& f) -> result<void>;

  /**
   * @brief Receives the next frame, if one is ready.
   *
   * Updates the cached controller state when an error frame arrives.
   *
   * @return The next frame, \c std::nullopt when none is ready on a non-blocking
   *         socket, or \c can_error::io_error on a read failure.
   *
   * @pre The bus is open.
   * @post A received error frame has updated \c state().
   */
  auto receive() -> result<std::optional<frame>>;

  /**
   * @brief Installs the kernel hardware filters.
   *
   * @param filters Filters to install; an empty span accepts every frame. At
   *                most \c max_filters are applied.
   *
   * @return Empty on success, \c can_error::buffer_full when more than
   *         \c max_filters are given, or \c can_error::io_error on failure.
   *
   * @pre The bus is open.
   * @post The kernel drops frames matching none of \p filters before they reach
   *       this socket.
   */
  auto set_filters(std::span<filter const> const filters) -> result<void>;

  /**
   * @brief The controller fault-confinement state.
   *
   * @return The state from the most recent error frame, or \c error_active.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto state() const noexcept -> bus_state;

  /**
   * @brief The underlying socket descriptor, for advanced use.
   *
   * @return The raw file descriptor the bus owns.
   *
   * @pre The bus is open.
   * @post None.
   */
  [[nodiscard]] auto descriptor() const noexcept -> int;
};

static_assert(can_bus<socketcan_bus>, "socketcan_bus must satisfy the can_bus concept");

}  // namespace nexenne::can

#else  // not Linux: a stub that satisfies the concept and reports unsupported.

namespace nexenne::can {

/**
 * @brief Non-Linux stub for the SocketCAN backend.
 *
 * Satisfies the \c can_bus concept so generic code compiles on every platform,
 * but every operation returns \c can_error::unsupported because SocketCAN is a
 * Linux facility.
 */
class socketcan_bus {
public:
  using value_type = frame;

  /**
   * @brief Reports that SocketCAN is unavailable on this platform.
   *
   * @param interface Ignored.
   * @param options Ignored.
   *
   * @return Always \c can_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto
  open(std::string_view const interface, socket_options const& options = {})
    -> result<socketcan_bus> {
    nexenne::utility::discard(interface);
    nexenne::utility::discard(options);
    return std::unexpected{can_error::unsupported};
  }

  /**
   * @brief Reports that sending is unsupported off Linux.
   *
   * @param f Ignored.
   *
   * @return Always \c can_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto send(frame const& f) -> result<void> {
    nexenne::utility::discard(f);
    return std::unexpected{can_error::unsupported};
  }

  /**
   * @brief Reports that receiving is unsupported off Linux.
   *
   * @return Always \c can_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto receive() -> result<std::optional<frame>> {
    return std::unexpected{can_error::unsupported};
  }

  /**
   * @brief Reports that filtering is unsupported off Linux.
   *
   * @param filters Ignored.
   *
   * @return Always \c can_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto set_filters(std::span<filter const> const filters) -> result<void> {
    nexenne::utility::discard(filters);
    return std::unexpected{can_error::unsupported};
  }

  /**
   * @brief The controller state; always bus-off on an unavailable backend.
   *
   * @return \c bus_state::bus_off.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto state() const noexcept -> bus_state {
    return bus_state::bus_off;
  }
};

static_assert(can_bus<socketcan_bus>, "socketcan_bus must satisfy the can_bus concept");

}  // namespace nexenne::can

#endif  // __linux__
