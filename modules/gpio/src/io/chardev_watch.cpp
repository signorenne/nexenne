#include <nexenne/gpio/io/chardev_watch.hpp>

#ifdef __linux__

#include <cerrno>

#include <linux/gpio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace nexenne::gpio {

// The header spells max_lines as a literal to avoid including <linux/gpio.h>;
// keep the two in lockstep.
static_assert(
  chardev_watcher::max_lines == GPIO_V2_LINES_MAX,
  "chardev_watcher::max_lines must match the kernel per-request line limit"
);

chardev_watcher::chardev_watcher(chip_id const chip) noexcept : m_chip{chip} {}

auto chardev_watcher::chip() const noexcept -> chip_id {
  return m_chip;
}

auto chardev_watcher::is_open() const noexcept -> bool {
  return m_fd.owns();
}

auto chardev_watcher::close() noexcept -> void {
  m_fd.reset();
  m_offsets.clear();
}

auto chardev_watcher::watch(std::span<line_offset const> const offsets) -> result<void> {
  if (offsets.empty() || offsets.size() > max_lines) {
    return std::unexpected{gpio_error::invalid_argument};
  }
  close();
  auto fd{detail::open_chip_readonly(m_chip)};
  if (!fd.owns()) {
    return std::unexpected{detail::info_errno()};
  }
  for (auto const offset : offsets) {
    ::gpio_v2_line_info raw{};
    raw.offset = static_cast<std::uint32_t>(offset.get());
    if (::ioctl(fd.get(), GPIO_V2_GET_LINEINFO_WATCH_IOCTL, &raw)
        < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
      return std::unexpected{errno == EINVAL ? gpio_error::invalid_argument : detail::info_errno()};
    }
    utility::discard(m_offsets.push_back(offset));
  }
  m_fd = std::move(fd);
  return {};
}

auto chardev_watcher::wait_change(std::chrono::nanoseconds const timeout)
  -> result<std::optional<line_change>> {
  if (!m_fd.owns()) {
    return std::unexpected{gpio_error::not_open};
  }

  ::pollfd poll_target{};
  poll_target.fd = m_fd.get();
  poll_target.events = POLLIN;
  ::timespec wait{};
  if (timeout.count() >= 0) {
    auto const seconds{std::chrono::duration_cast<std::chrono::seconds>(timeout)};
    wait.tv_sec = seconds.count();
    wait.tv_nsec = (timeout - seconds).count();
  }
  int const ready{::ppoll(&poll_target, 1, timeout.count() < 0 ? nullptr : &wait, nullptr)};
  if (ready < 0) {
    if (errno == EINTR) {
      return std::optional<line_change>{};
    }
    return std::unexpected{detail::info_errno()};
  }
  if (ready == 0) {
    return std::optional<line_change>{};
  }

  ::gpio_v2_line_info_changed raw{};
  ssize_t const got{::read(m_fd.get(), &raw, sizeof(raw))};
  if (got < 0) {
    if (errno == EAGAIN || errno == EINTR) {
      return std::optional<line_change>{};
    }
    return std::unexpected{detail::info_errno()};
  }
  if (got != static_cast<ssize_t>(sizeof(raw))) {
    return std::unexpected{gpio_error::io_error};
  }

  line_change change{};
  change.info = detail::decode_line_info(raw.info);
  change.timestamp =
    event_time{std::chrono::nanoseconds{static_cast<std::int64_t>(raw.timestamp_ns)}};
  switch (raw.event_type) {
    case GPIO_V2_LINE_CHANGED_REQUESTED:
      change.kind = line_change_kind::requested;
      break;
    case GPIO_V2_LINE_CHANGED_RELEASED:
      change.kind = line_change_kind::released;
      break;
    case GPIO_V2_LINE_CHANGED_CONFIG:
    default:
      change.kind = line_change_kind::reconfigured;
      break;
  }
  return std::optional<line_change>{change};
}

auto chardev_watcher::native_handle() const noexcept -> native_handle_type {
  return m_fd.owns() ? m_fd.get() : -1;
}

}  // namespace nexenne::gpio

#endif  // __linux__
