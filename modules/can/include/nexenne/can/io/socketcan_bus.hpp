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

#  include <array>
#  include <cerrno>
#  include <cstddef>
#  include <cstring>
#  include <ctime>
#  include <utility>

#  include <fcntl.h>
#  include <linux/can.h>
#  include <linux/can/raw.h>
#  include <net/if.h>
#  include <nexenne/container/static_vector.hpp>
#  include <nexenne/utility/unique_resource.hpp>
#  include <sys/ioctl.h>
#  include <sys/socket.h>
#  include <unistd.h>

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
  auto operator()(int const fd) const noexcept -> void {
    ::close(fd);
  }
};

}  // namespace detail

/**
 * @brief Converts a frame to a Classic CAN kernel struct.
 *
 * @param f Frame to convert; only the first 8 bytes are used.
 *
 * @return A \c can_frame holding the identifier, length, and payload of \p f.
 *
 * @pre \c f.length() is at most 8.
 * @post The result's \c can_id equals \c f.id().raw().
 */
[[nodiscard]] inline auto to_can_frame(frame const& f) noexcept -> ::can_frame {
  assert(f.length() <= max_classic_length && "to_can_frame: length exceeds the 8-byte Classic buffer");
  ::can_frame out{};
  out.can_id = f.id().raw();
  out.can_dlc = f.length();
  auto const payload{f.data()};
  std::memcpy(static_cast<void*>(out.data), payload.data(), payload.size());
  return out;
}

/**
 * @brief Converts a frame to a CAN FD kernel struct.
 *
 * The on-wire length is padded up to the next discrete CAN FD size; the padding
 * bytes are zero.
 *
 * @param f Frame to convert.
 *
 * @return A \c canfd_frame holding the identifier, padded length, FD flags, and
 *         payload of \p f.
 *
 * @pre \c f.length() is at most 64.
 * @post The result's \c can_id equals \c f.id().raw().
 */
[[nodiscard]] inline auto to_canfd_frame(frame const& f) noexcept -> ::canfd_frame {
  assert(f.length() <= max_fd_length && "to_canfd_frame: length exceeds the 64-byte FD buffer");
  ::canfd_frame out{};
  out.can_id = f.id().raw();
  out.len = fd_padded_length(f.length());
  if (f.flags().has(fd_flag::brs)) {
    out.flags |= CANFD_BRS;
  }
  if (f.flags().has(fd_flag::esi)) {
    out.flags |= CANFD_ESI;
  }
  auto const payload{f.data()};
  std::memcpy(static_cast<void*>(out.data), payload.data(), payload.size());
  return out;
}

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
[[nodiscard]] inline auto from_can_frame(::can_frame const& cf) -> result<frame> {
  std::array<std::byte, max_classic_length> bytes{};
  // A Classic CAN DLC of 9..15 means 8 data bytes, per the standard; clamp it
  // rather than rejecting it.
  auto const length{static_cast<std::size_t>(classic_dlc_to_length(cf.can_dlc & 0x0FU))};
  std::memcpy(bytes.data(), static_cast<void const*>(cf.data), length);
  return frame::classic(can_id::from_raw(cf.can_id), std::span{bytes}.first(length));
}

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
[[nodiscard]] inline auto from_canfd_frame(::canfd_frame const& cf) -> result<frame> {
  std::array<std::byte, max_fd_length> bytes{};
  auto const length{static_cast<std::size_t>(cf.len)};
  if (length > max_fd_length) {
    return std::unexpected{can_error::invalid_dlc};
  }
  std::memcpy(bytes.data(), static_cast<void const*>(cf.data), length);
  utility::flags<fd_flag> flags;
  if ((cf.flags & CANFD_BRS) != 0) {
    flags.set(fd_flag::brs);
  }
  if ((cf.flags & CANFD_ESI) != 0) {
    flags.set(fd_flag::esi);
  }
  return frame::fd(can_id::from_raw(cf.can_id), std::span{bytes}.first(length), flags);
}

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

  socketcan_bus(socket_handle&& socket, bool const fd_enabled) noexcept
      : m_socket{std::move(socket)}, m_fd_enabled{fd_enabled} {}

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
  [[nodiscard]] static auto open(
    std::string_view const interface, socket_options const& options = {}
  ) -> result<socketcan_bus> {
    int const raw{::socket(PF_CAN, SOCK_RAW, CAN_RAW)};
    if (raw < 0) {
      return std::unexpected{can_error::io_error};
    }
    socket_handle handle{raw, detail::socket_closer{}};

    if (options.fd_enabled) {
      int const on{1};
      if (::setsockopt(raw, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &on, sizeof(on)) < 0) {
        return std::unexpected{can_error::io_error};
      }
    }
    {
      int const own{options.receive_own_messages ? 1 : 0};
      if (::setsockopt(raw, SOL_CAN_RAW, CAN_RAW_RECV_OWN_MSGS, &own, sizeof(own)) < 0) {
        return std::unexpected{can_error::io_error};
      }
    }
    {
      // Best-effort receive timestamps; a backend without them just leaves the
      // frame timestamp at zero.
      int const stamp{1};
      utility::discard(::setsockopt(raw, SOL_SOCKET, SO_TIMESTAMPNS, &stamp, sizeof(stamp)));
    }
    {
      // Subscribe to error frames; without this filter the kernel never delivers
      // one, so the receive-side error decode and state() reporting would be dead.
      can_err_mask_t const error_mask{CAN_ERR_MASK};
      if (::setsockopt(raw, SOL_CAN_RAW, CAN_RAW_ERR_FILTER, &error_mask, sizeof(error_mask)) < 0) {
        return std::unexpected{can_error::io_error};
      }
    }

    ifreq ifr{};
    // A string_view is not null-terminated, so copy exactly its bytes (bounded by
    // the field) rather than strncpy, which would read past the view.
    if (interface.size() >= sizeof(ifr.ifr_name)) {
      return std::unexpected{can_error::io_error};
    }
    std::memcpy(static_cast<void*>(ifr.ifr_name), interface.data(), interface.size());
    if (::ioctl(raw, SIOCGIFINDEX, &ifr) < 0) {
      return std::unexpected{can_error::io_error};
    }

    sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    if (::bind(raw, reinterpret_cast<sockaddr const*>(&addr), sizeof(addr)) < 0) {
      return std::unexpected{can_error::io_error};
    }

    if (options.nonblocking) {
      int const previous{::fcntl(raw, F_GETFL, 0)};
      if (previous < 0 || ::fcntl(raw, F_SETFL, previous | O_NONBLOCK) < 0) {
        return std::unexpected{can_error::io_error};
      }
    } else if (options.read_timeout_ms != 0) {
      // A blocking read otherwise waits forever; honour the requested timeout.
      ::timeval timeout{};
      timeout.tv_sec = options.read_timeout_ms / 1000;
      timeout.tv_usec = static_cast<::suseconds_t>((options.read_timeout_ms % 1000) * 1000);
      if (::setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0) {
        return std::unexpected{can_error::io_error};
      }
    }

    return socketcan_bus{std::move(handle), options.fd_enabled};
  }

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
  auto send(frame const& f) -> result<void> {
    if (f.is_fd()) {
      if (!m_fd_enabled) {
        return std::unexpected{can_error::unsupported};
      }
      ::canfd_frame const out{to_canfd_frame(f)};
      if (::write(m_socket.get(), &out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
        return std::unexpected{can_error::io_error};
      }
    } else {
      ::can_frame const out{to_can_frame(f)};
      if (::write(m_socket.get(), &out, sizeof(out)) != static_cast<ssize_t>(sizeof(out))) {
        return std::unexpected{can_error::io_error};
      }
    }
    return {};
  }

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
  auto receive() -> result<std::optional<frame>> {
    ::canfd_frame buffer{};
    iovec io{};
    io.iov_base = &buffer;
    io.iov_len = sizeof(buffer);
    alignas(::cmsghdr) std::array<unsigned char, CMSG_SPACE(sizeof(::timespec))> control{};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control.data();
    message.msg_controllen = control.size();

    ssize_t const got{::recvmsg(m_socket.get(), &message, 0)};
    if (got < 0) {
      // EAGAIN and EWOULDBLOCK are the same value on Linux; the guard avoids a
      // "logical or of equal expressions" warning while staying portable.
      auto const code{errno};
      if (code == EAGAIN
#  if EWOULDBLOCK != EAGAIN
          || code == EWOULDBLOCK
#  endif
      ) {
        return std::optional<frame>{};
      }
      return std::unexpected{can_error::io_error};
    }

    result<frame> decoded{std::unexpected{can_error::io_error}};
    if (got == static_cast<ssize_t>(sizeof(::canfd_frame))) {
      decoded = from_canfd_frame(buffer);
    } else if (got == static_cast<ssize_t>(sizeof(::can_frame))) {
      ::can_frame classic{};
      std::memcpy(&classic, &buffer, sizeof(classic));
      decoded = from_can_frame(classic);
    } else {
      return std::unexpected{can_error::io_error};
    }
    if (!decoded) {
      return std::unexpected{decoded.error()};
    }

    // Extract the kernel receive timestamp from the control message, if present.
    for (::cmsghdr* header{CMSG_FIRSTHDR(&message)}; header != nullptr;
         header = CMSG_NXTHDR(&message, header)) {
      if (header->cmsg_level == SOL_SOCKET && header->cmsg_type == SO_TIMESTAMPNS) {
        ::timespec stamp{};
        std::memcpy(&stamp, CMSG_DATA(header), sizeof(stamp));
        decoded->timestamp_ns() = static_cast<std::uint64_t>(stamp.tv_sec) * 1'000'000'000ULL
                                  + static_cast<std::uint64_t>(stamp.tv_nsec);
      }
    }

    if (auto const report{decode_error_frame(*decoded)}) {
      m_state = report->state;
    }
    return std::optional<frame>{*decoded};
  }

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
  auto set_filters(std::span<filter const> const filters) -> result<void> {
    if (filters.size() > max_filters) {
      return std::unexpected{can_error::buffer_full};
    }
    container::static_vector<::can_filter, max_filters> kernel_filters;
    for (filter const f : filters) {
      nexenne::utility::discard(kernel_filters.push_back(::can_filter{f.id(), f.mask()}));
    }
    auto const length{static_cast<socklen_t>(kernel_filters.size() * sizeof(::can_filter))};
    if (::setsockopt(m_socket.get(), SOL_CAN_RAW, CAN_RAW_FILTER, kernel_filters.data(), length)
        < 0) {
      return std::unexpected{can_error::io_error};
    }
    return {};
  }

  /**
   * @brief The controller fault-confinement state.
   *
   * @return The state from the most recent error frame, or \c error_active.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto state() const noexcept -> bus_state {
    return m_state;
  }

  /**
   * @brief The underlying socket descriptor, for advanced use.
   *
   * @return The raw file descriptor the bus owns.
   *
   * @pre The bus is open.
   * @post None.
   */
  [[nodiscard]] auto descriptor() const noexcept -> int {
    return m_socket.get();
  }
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
  [[nodiscard]] static auto open(
    std::string_view const interface, socket_options const& options = {}
  ) -> result<socketcan_bus> {
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
