#include <nexenne/can/io/socketcan_bus.hpp>

#ifdef __linux__

#include <cerrno>
#include <cstring>
#include <optional>
#include <span>
#include <utility>

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace nexenne::can {

namespace detail {

auto socket_closer::operator()(int const fd) const noexcept -> void {
  ::close(fd);
}

}  // namespace detail

auto to_can_frame(frame const& f) noexcept -> ::can_frame {
  assert(
    f.length() <= max_classic_length && "to_can_frame: length exceeds the 8-byte Classic buffer"
  );
  ::can_frame out{};
  out.can_id = f.id().raw();
  out.can_dlc = f.length();
  auto const payload{f.data()};
  std::memcpy(static_cast<void*>(out.data), payload.data(), payload.size());
  return out;
}

auto to_canfd_frame(frame const& f) noexcept -> ::canfd_frame {
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

auto from_can_frame(::can_frame const& cf) -> result<frame> {
  std::array<std::byte, max_classic_length> bytes{};
  // A Classic CAN DLC of 9..15 means 8 data bytes, per the standard; clamp it
  // rather than rejecting it.
  auto const length{static_cast<std::size_t>(classic_dlc_to_length(cf.can_dlc & 0x0FU))};
  std::memcpy(bytes.data(), static_cast<void const*>(cf.data), length);
  return frame::classic(can_id::from_raw(cf.can_id), std::span{bytes}.first(length));
}

auto from_canfd_frame(::canfd_frame const& cf) -> result<frame> {
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

socketcan_bus::socketcan_bus(socket_handle&& socket, bool const fd_enabled) noexcept
    : m_socket{std::move(socket)}, m_fd_enabled{fd_enabled} {}

auto socketcan_bus::open(std::string_view const interface, socket_options const& options)
  -> result<socketcan_bus> {
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

auto socketcan_bus::send(frame const& f) -> result<void> {
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

auto socketcan_bus::receive() -> result<std::optional<frame>> {
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
    if (
      code == EAGAIN
#if EWOULDBLOCK != EAGAIN
      || code == EWOULDBLOCK
#endif
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

auto socketcan_bus::set_filters(std::span<filter const> const filters) -> result<void> {
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

auto socketcan_bus::state() const noexcept -> bus_state {
  return m_state;
}

auto socketcan_bus::descriptor() const noexcept -> int {
  return m_socket.get();
}

}  // namespace nexenne::can

#endif  // __linux__
