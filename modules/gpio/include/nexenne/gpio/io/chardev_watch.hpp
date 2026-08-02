#pragma once

/**
 * @file
 * @brief Watching line ownership and configuration over the character device.
 *
 * A GPIO line can be requested, released, or reconfigured by any process at
 * any time, and a supervisor that only polls its own request never sees it
 * happen: the kernel simply answers \c EBUSY at the next open. The kernel's
 * line-info watch (\c GPIO_V2_GET_LINEINFO_WATCH_IOCTL) closes that blind
 * spot: arm a watch per line and the chip descriptor becomes readable
 * whenever any watched line is requested, released, or reconfigured, with
 * the updated \c line_info and a timestamp in each change record.
 *
 * Like the event path, the watcher owns no loop: \c native_handle() exposes
 * the chip descriptor for epoll, Qt, or ASIO, and \c wait_change with a
 * zero timeout drains a ready change without blocking. Watching is
 * read-only and never claims a line.
 *
 * Off Linux the type exists and reports \c gpio_error::unsupported.
 *
 * @thread_safety A chardev_watcher instance is not safe for concurrent
 * calls; distinct instances are independent.
 */

#include <chrono>
#include <cstddef>
#include <optional>
#include <span>

#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/io/chardev_info.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::gpio {

/**
 * @brief What happened to a watched line.
 */
enum class line_change_kind : std::uint8_t {
  requested,     ///< A consumer requested the line.
  released,      ///< The holding consumer released the line.
  reconfigured,  ///< The holding consumer changed the line's configuration.
};

/**
 * @brief One ownership or configuration change on a watched line.
 */
struct line_change {
  using value_type = line_info;

  line_info info{};                                    ///< The line's state after the change.
  event_time timestamp{};                              ///< When the change occurred.
  line_change_kind kind{line_change_kind::requested};  ///< What kind of change it was.
};

}  // namespace nexenne::gpio

#ifdef __linux__

#  include <cerrno>

#  include <linux/gpio.h>
#  include <nexenne/container/static_vector.hpp>
#  include <nexenne/utility/unique_resource.hpp>
#  include <poll.h>
#  include <sys/ioctl.h>
#  include <unistd.h>

namespace nexenne::gpio {

/**
 * @brief Watches lines on one chip for request, release, and reconfigure.
 *
 * Construct one per \c /dev/gpiochipN, arm it with \c watch, then wait on
 * \c wait_change or integrate \c native_handle() into an event loop. The
 * type owns the chip descriptor and is move-only.
 */
class chardev_watcher {
public:
  using value_type = line_change;
  /// The pollable chip file descriptor type.
  using native_handle_type = int;

  /// Watched-line bookkeeping capacity, mirroring the per-request limit.
  static constexpr std::size_t max_lines{GPIO_V2_LINES_MAX};

private:
  using fd_handle = utility::unique_resource<int, detail::info_fd_closer>;

  chip_id m_chip{0};
  fd_handle m_fd{};
  container::static_vector<line_offset, max_lines> m_offsets{};

public:
  /**
   * @brief Constructs a closed watcher for one chip.
   *
   * @param chip Index of the chip; selects \c /dev/gpiochipN.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  explicit chardev_watcher(chip_id const chip = chip_id{0}) noexcept : m_chip{chip} {}

  /**
   * @brief The chip index this watcher targets.
   *
   * @return The stored chip identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto chip() const noexcept -> chip_id {
    return m_chip;
  }

  /**
   * @brief Whether the watcher holds an armed chip descriptor.
   *
   * @return \c true between a successful \c watch and the next \c close.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_fd.owns();
  }

  /**
   * @brief Stops watching and closes the chip descriptor.
   *
   * Safe to call when already closed; the kernel drops the watches with
   * the descriptor.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  auto close() noexcept -> void {
    m_fd.reset();
    m_offsets.clear();
  }

  /**
   * @brief Opens the chip and arms a watch on each given line.
   *
   * Watching is read-only: it never requests a line and cannot conflict
   * with any consumer. Each line's current info is fetched as a side
   * effect of arming, so a change delivered later is always relative to a
   * state the kernel reported.
   *
   * @param offsets Lines to watch; at most \c max_lines, no duplicates.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when
   *         \p offsets is empty, oversized, or names an offset the chip
   *         does not have, \c gpio_error::busy on a duplicate watch,
   *         otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success \c is_open() is \c true; on failure the watcher is
   *       closed.
   */
  auto watch(std::span<line_offset const> const offsets) -> result<void> {
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
      if (::ioctl(fd.get(), GPIO_V2_GET_LINEINFO_WATCH_IOCTL, &raw) < 0) {
        return std::unexpected{
          errno == EINVAL ? gpio_error::invalid_argument : detail::info_errno()
        };
      }
      utility::discard(m_offsets.push_back(offset));
    }
    m_fd = std::move(fd);
    return {};
  }

  /**
   * @brief Waits for and reads one change record.
   *
   * Waits on the chip descriptor with \c ppoll, then reads exactly one
   * kernel change record. A zero timeout is a non-blocking poll and a
   * negative timeout blocks until a change arrives; an interrupted wait
   * reports a clean miss so callers simply loop.
   *
   * @param timeout Longest time to wait; zero polls, negative blocks.
   *
   * @return The next change, or \c std::nullopt when the timeout (or a
   *         signal) ended the wait first; \c gpio_error::not_open when no
   *         watch is armed, otherwise the errno-mapped error.
   *
   * @pre \c watch succeeded.
   * @post On a value result one kernel change record was consumed.
   */
  auto wait_change(std::chrono::nanoseconds const timeout) -> result<std::optional<line_change>> {
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

  /**
   * @brief The pollable chip descriptor for event-loop integration.
   *
   * Register it readable with epoll, a Qt socket notifier, or ASIO, then
   * drain ready changes with \c wait_change and a zero timeout.
   *
   * @return The chip descriptor, or \c -1 when closed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto native_handle() const noexcept -> native_handle_type {
    return m_fd.owns() ? m_fd.get() : -1;
  }
};

}  // namespace nexenne::gpio

#else  // not Linux: a stub that compiles everywhere and reports unsupported.

namespace nexenne::gpio {

/**
 * @brief Non-Linux stub for the line-info watcher.
 *
 * The surface exists so generic code compiles on every platform, but every
 * operation reports \c gpio_error::unsupported because the line-info watch
 * is a Linux facility.
 */
class chardev_watcher {
public:
  using value_type = line_change;
  /// The pollable handle type; always \c -1 here.
  using native_handle_type = int;

  /// Mirrors the Linux per-request line limit.
  static constexpr std::size_t max_lines{64};

private:
  chip_id m_chip{0};

public:
  /**
   * @brief Constructs the stub; nothing can be watched.
   *
   * @param chip Stored for \c chip(); otherwise unused.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  explicit chardev_watcher(chip_id const chip = chip_id{0}) noexcept : m_chip{chip} {}

  /**
   * @brief The chip index this stub was constructed with.
   *
   * @return The stored chip identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto chip() const noexcept -> chip_id {
    return m_chip;
  }

  /**
   * @brief Reports the permanently closed state.
   *
   * @return Always \c false.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return false;
  }

  /**
   * @brief Does nothing; there is never anything to close.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  auto close() noexcept -> void {}

  /**
   * @brief Reports that watching is unsupported off Linux.
   *
   * @param offsets Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto watch(std::span<line_offset const> const offsets) -> result<void> {
    utility::discard(offsets);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that change waits are unsupported off Linux.
   *
   * @param timeout Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto wait_change(std::chrono::nanoseconds const timeout) -> result<std::optional<line_change>> {
    utility::discard(timeout);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief The pollable handle; the stub has none.
   *
   * @return Always \c -1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto native_handle() const noexcept -> native_handle_type {
    return -1;
  }
};

}  // namespace nexenne::gpio

#endif  // __linux__
