#pragma once

/**
 * @file
 * @brief The Linux GPIO backend over the character-device v2 uAPI.
 *
 * Linux exposes every GPIO chip as \c /dev/gpiochipN, driven through the v2
 * ioctl interface in the kernel header \c linux/gpio.h (stable since kernel
 * 5.10; it is the same interface libgpiod wraps). This backend talks to it
 * directly, so the module carries no external library dependency: one
 * \c GPIO_V2_GET_LINE_IOCTL requests all lines atomically and yields an
 * anonymous request file descriptor; values move through
 * \c GPIO_V2_LINE_GET_VALUES_IOCTL and \c SET_VALUES over 64-bit bitmaps;
 * edge events arrive by reading \c gpio_v2_line_event records from the
 * request descriptor, one per read, so there is no staging buffer to go
 * stale across a close and reopen.
 *
 * The type satisfies \c gpio_backend, \c bulk_gpio_backend, and
 * \c edge_source. Polarity is NOT applied here, and the kernel's own
 * \c ACTIVE_LOW flag is deliberately never set: backends exchange raw
 * physical levels, and the wrapper layer owns the logical domain, which
 * keeps this backend drop-in interchangeable with the mock and any embedded
 * backend. Kernel edge events therefore report physical directions, and the
 * event's level is implied by its direction (the v2 interface only ever
 * reports rising or falling).
 *
 * Event-loop integration: \c native_handle() returns the request
 * descriptor. Register it with epoll, a Qt \c QSocketNotifier, ASIO, or any
 * other readiness loop, and drain with \c wait_event(0ns) when it signals
 * readable; the module never owns the loop.
 *
 * This header is Linux-only in behaviour but portable in form: off Linux
 * the same type exists, satisfies the same concepts, and every operation
 * returns \c gpio_error::unsupported, so generic code compiles everywhere
 * and fails cleanly.
 *
 * Reference: the Linux kernel GPIO character device documentation,
 * \c Documentation/userspace-api/gpio/chardev.rst.
 *
 * @thread_safety A chardev_chip instance is not safe for concurrent calls;
 * distinct instances are independent.
 */

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <nexenne/gpio/backend.hpp>
#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line_config.hpp>
#include <nexenne/gpio/line_event.hpp>
#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/utility/discard.hpp>

#ifdef __linux__

#  include <algorithm>
#  include <array>
#  include <cerrno>
#  include <charconv>
#  include <cstring>
#  include <ctime>
#  include <limits>

#  include <fcntl.h>
#  include <linux/gpio.h>
#  include <nexenne/container/static_vector.hpp>
#  include <nexenne/utility/unique_resource.hpp>
#  include <poll.h>
#  include <sys/ioctl.h>
#  include <unistd.h>

namespace nexenne::gpio {

namespace detail {

/**
 * @brief Closes a file descriptor; the deleter for the descriptor handles.
 */
struct fd_closer {
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

/**
 * @brief Maps an errno value onto the module's neutral error categories.
 *
 * @param error The errno value reported by a failed call.
 *
 * @return The matching \c gpio_error; unrecognised values map to
 *         \c gpio_error::io_error.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto errno_to_gpio_error(int const error) noexcept -> gpio_error {
  switch (error) {
    case EINVAL:
      return gpio_error::invalid_argument;
    case ENOENT:
    case ENODEV:
    case ENXIO:
      return gpio_error::not_found;
    case EACCES:
    case EPERM:
      return gpio_error::permission_denied;
    case EBUSY:
      return gpio_error::busy;
    case ETIMEDOUT:
      return gpio_error::timeout;
    case ENOTSUP:
      return gpio_error::unsupported;
    default:
      return gpio_error::io_error;
  }
}

}  // namespace detail

/**
 * @brief A GPIO chip backed by the Linux character device.
 *
 * Construct one per \c /dev/gpiochipN, then \c open it with the spec and
 * config tables; multi-chip systems hold one instance per chip. The type
 * owns the request descriptor and is move-only.
 */
class chardev_chip {
public:
  using value_type = bool;
  /// The pollable request file descriptor type.
  using native_handle_type = int;

  /// Largest number of lines one request can hold (a kernel limit).
  static constexpr std::size_t max_lines{GPIO_V2_LINES_MAX};

private:
  using fd_handle = utility::unique_resource<int, detail::fd_closer>;

  chip_id m_chip{0};
  std::string_view m_consumer{"nexenne-gpio"};
  std::uint32_t m_event_buffer_size{0};
  fd_handle m_request{};
  container::static_vector<line_offset, max_lines> m_offsets{};

  [[nodiscard]] auto index_of(line_offset const offset
  ) const noexcept -> std::optional<std::size_t> {
    for (std::size_t i{0}; i < m_offsets.size(); ++i) {
      if (m_offsets[i] == offset) {
        return i;
      }
    }
    return std::nullopt;
  }

  // The neutral vocabulary rendered as kernel flag bits. ACTIVE_LOW is never
  // set: polarity belongs to the wrapper layer, so the kernel's "active"
  // always means the physical high level.
  [[nodiscard]] static auto
  kernel_flags(line_spec const& spec, line_config const& config) noexcept -> std::uint64_t {
    std::uint64_t flags{0};
    if (spec.direction() == line_direction::output) {
      flags |= GPIO_V2_LINE_FLAG_OUTPUT;
      switch (spec.drive()) {
        case line_drive::open_drain:
          flags |= GPIO_V2_LINE_FLAG_OPEN_DRAIN;
          break;
        case line_drive::open_source:
          flags |= GPIO_V2_LINE_FLAG_OPEN_SOURCE;
          break;
        case line_drive::push_pull:
          break;
      }
    } else {
      flags |= GPIO_V2_LINE_FLAG_INPUT;
      switch (config.edges()) {
        case edge_detection::rising:
          flags |= GPIO_V2_LINE_FLAG_EDGE_RISING;
          break;
        case edge_detection::falling:
          flags |= GPIO_V2_LINE_FLAG_EDGE_FALLING;
          break;
        case edge_detection::both:
          flags |= GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING;
          break;
        case edge_detection::none:
          break;
      }
    }
    switch (spec.bias()) {
      case line_bias::pull_up:
        flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_UP;
        break;
      case line_bias::pull_down:
        flags |= GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN;
        break;
      case line_bias::disabled:
        flags |= GPIO_V2_LINE_FLAG_BIAS_DISABLED;
        break;
      case line_bias::as_is:
        break;
    }
    switch (config.clock()) {
      case line_clock::realtime:
        flags |= GPIO_V2_LINE_FLAG_EVENT_CLOCK_REALTIME;
        break;
      case line_clock::hte:
        // The kernel rejects this on hardware without a timestamp engine,
        // which surfaces as gpio_error::unsupported from the open.
        flags |= GPIO_V2_LINE_FLAG_EVENT_CLOCK_HTE;
        break;
      case line_clock::monotonic:
        break;
    }
    return flags;
  }

  // Renders the parallel tables into a kernel line config: the first line's
  // flag word becomes the base, deviating flag words are grouped into one
  // attribute each, and initial output levels and distinct debounce periods
  // become further attributes, within the kernel's ten-attribute cap.
  [[nodiscard]] static auto build_line_config(
    std::span<line_spec const> const specs,
    std::span<line_config const> const configs,
    ::gpio_v2_line_config* const config
  ) -> result<void> {
    std::array<std::uint64_t, max_lines> flags{};
    for (std::size_t i{0}; i < specs.size(); ++i) {
      flags[i] = kernel_flags(specs[i], configs[i]);
    }
    config->flags = flags[0];

    auto& attrs{config->attrs};
    std::uint32_t attr_count{0};
    auto const add_attr{
      [&](::gpio_v2_line_attribute const& attribute, std::uint64_t const mask) noexcept -> bool {
        if (attr_count >= GPIO_V2_LINE_NUM_ATTRS_MAX) {
          return false;
        }
        attrs[attr_count].attr = attribute;
        attrs[attr_count].mask = mask;
        attr_count += 1;
        return true;
      }
    };

    // Group lines whose flags differ from the base into one attribute per
    // distinct flag word (first occurrence wins, so scan forward).
    for (std::size_t i{1}; i < specs.size(); ++i) {
      if (flags[i] == config->flags) {
        continue;
      }
      bool grouped{false};
      for (std::size_t j{1}; j < i; ++j) {
        if (flags[j] == flags[i]) {
          grouped = true;
          break;
        }
      }
      if (grouped) {
        continue;
      }
      std::uint64_t mask{0};
      for (std::size_t j{i}; j < specs.size(); ++j) {
        if (flags[j] == flags[i]) {
          mask |= std::uint64_t{1} << j;
        }
      }
      ::gpio_v2_line_attribute attribute{};
      attribute.id = GPIO_V2_LINE_ATTR_ID_FLAGS;
      // The kernel attribute is a tagged union; id selects the member.
      attribute.flags = flags[i];  // NOLINT(cppcoreguidelines-pro-type-union-access)
      if (!add_attr(attribute, mask)) {
        return std::unexpected{gpio_error::invalid_argument};
      }
    }

    // Initial output levels ride in one OUTPUT_VALUES attribute.
    {
      std::uint64_t output_mask{0};
      std::uint64_t output_values{0};
      for (std::size_t i{0}; i < specs.size(); ++i) {
        if (specs[i].direction() != line_direction::output) {
          continue;
        }
        output_mask |= std::uint64_t{1} << i;
        if (configs[i].initial_value()) {
          output_values |= std::uint64_t{1} << i;
        }
      }
      if (output_mask != 0) {
        ::gpio_v2_line_attribute attribute{};
        attribute.id = GPIO_V2_LINE_ATTR_ID_OUTPUT_VALUES;
        // The kernel attribute is a tagged union; id selects the member.
        attribute.values = output_values;  // NOLINT(cppcoreguidelines-pro-type-union-access)
        if (!add_attr(attribute, output_mask)) {
          return std::unexpected{gpio_error::invalid_argument};
        }
      }
    }

    // One DEBOUNCE attribute per distinct non-zero period.
    for (std::size_t i{0}; i < specs.size(); ++i) {
      auto const period{configs[i].debounce_period()};
      if (period.count() <= 0) {
        continue;
      }
      bool grouped{false};
      for (std::size_t j{0}; j < i; ++j) {
        if (configs[j].debounce_period() == period) {
          grouped = true;
          break;
        }
      }
      if (grouped) {
        continue;
      }
      auto const microseconds{std::chrono::duration_cast<std::chrono::microseconds>(period).count()
      };
      if (microseconds > std::int64_t{std::numeric_limits<std::uint32_t>::max()}) {
        return std::unexpected{gpio_error::invalid_argument};
      }
      std::uint64_t mask{0};
      for (std::size_t j{i}; j < specs.size(); ++j) {
        if (configs[j].debounce_period() == period) {
          mask |= std::uint64_t{1} << j;
        }
      }
      ::gpio_v2_line_attribute attribute{};
      attribute.id = GPIO_V2_LINE_ATTR_ID_DEBOUNCE;
      // The kernel attribute is a tagged union; id selects the member.
      attribute.debounce_period_us =  // NOLINT(cppcoreguidelines-pro-type-union-access)
        static_cast<std::uint32_t>(microseconds);
      if (!add_attr(attribute, mask)) {
        return std::unexpected{gpio_error::invalid_argument};
      }
    }
    config->num_attrs = attr_count;
    return {};
  }

  [[nodiscard]] auto
  values_ioctl(unsigned long const request, ::gpio_v2_line_values* values) const -> result<void> {
    if (!m_request.owns()) {
      return std::unexpected{gpio_error::not_open};
    }
    if (::ioctl(m_request.get(), request, values)
        < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }
    return {};
  }

public:
  /**
   * @brief Constructs a closed backend for one chip.
   *
   * Nothing is opened until \c open is called.
   *
   * @param chip Index of the chip; selects \c /dev/gpiochipN.
   * @param consumer Consumer label reported to the kernel; shown by tools
   *                 like \c gpioinfo. Must be shorter than 32 bytes and must
   *                 outlive the backend.
   * @param event_buffer_size Suggested kernel event buffer depth; zero keeps
   *                          the kernel default of 16 events per line.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  explicit chardev_chip(
    chip_id const chip = chip_id{0},
    std::string_view const consumer = "nexenne-gpio",
    std::uint32_t const event_buffer_size = 0
  ) noexcept
      : m_chip{chip}, m_consumer{consumer}, m_event_buffer_size{event_buffer_size} {}

  /**
   * @brief The chip index this backend targets.
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
   * @brief Whether a line request is currently held.
   *
   * @return \c true between a successful \c open and the next \c close.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool {
    return m_request.owns();
  }

  /**
   * @brief Releases the line request.
   *
   * Safe to call when already closed.
   *
   * @pre None.
   * @post \c is_open() is \c false and the request descriptor is closed.
   */
  auto close() noexcept -> void {
    m_request.reset();
    m_offsets.clear();
  }

  /**
   * @brief Opens the chip and requests all lines in one atomic ioctl.
   *
   * Renders each spec and config into kernel flag words. The most common
   * behaviour becomes the request's base flags; lines that differ are
   * grouped into per-group attribute overrides, and initial output levels
   * and per-line debounce periods become further attributes. The kernel
   * caps a request at ten attributes, so a request needs at most ten
   * distinct line behaviours beyond the base.
   *
   * @param specs Specs to request, one per line, all on this chip.
   * @param configs Per-line open-time config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         tables are malformed, name a different chip, exceed
   *         \c max_lines, need more than ten attribute groups, or carry a
   *         debounce period beyond the kernel's microsecond range;
   *         otherwise the errno-mapped kernel error (notably
   *         \c gpio_error::busy when another consumer holds a line and
   *         \c gpio_error::permission_denied on device permissions).
   *
   * @pre \p specs and \p configs describe the same lines element by element.
   * @post On success \c is_open() is \c true; on failure the backend is
   *       closed.
   */
  auto open(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void> {
    if (specs.size() != configs.size() || specs.empty() || specs.size() > max_lines) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    if (m_consumer.size() >= GPIO_MAX_NAME_SIZE) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    for (auto const& spec : specs) {
      if (spec.chip() != m_chip) {
        return std::unexpected{gpio_error::invalid_argument};
      }
    }

    close();

    // Rendered with to_chars rather than a vararg call: type-safe, and the
    // buffer is wide enough for the prefix plus any 16-bit index.
    std::array<char, 32> path{};
    constexpr std::string_view prefix{"/dev/gpiochip"};
    std::ranges::copy(prefix, path.begin());
    auto const rendered{
      std::to_chars(path.data() + prefix.size(), path.data() + path.size() - 1, m_chip.get())
    };
    *rendered.ptr = '\0';
    fd_handle const chip_fd{utility::make_unique_resource_checked(
      ::open(path.data(), O_RDWR | O_CLOEXEC), -1, detail::fd_closer{}
    )};
    if (!chip_fd.owns()) {
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }

    ::gpio_v2_line_request request{};
    request.num_lines = static_cast<std::uint32_t>(specs.size());
    request.event_buffer_size = m_event_buffer_size;
    std::memcpy(static_cast<void*>(request.consumer), m_consumer.data(), m_consumer.size());
    for (std::size_t i{0}; i < specs.size(); ++i) {
      request.offsets[i] = static_cast<std::uint32_t>(specs[i].offset().get());
    }
    if (auto const built{build_line_config(specs, configs, &request.config)}; !built.has_value()) {
      return std::unexpected{built.error()};
    }

    if (::ioctl(chip_fd.get(), GPIO_V2_GET_LINE_IOCTL, &request)
        < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }
    m_request.reset(request.fd);
    for (auto const& spec : specs) {
      utility::discard(m_offsets.push_back(spec.offset()));
    }
    return {};
  }

  /**
   * @brief Changes the configuration of the held lines without reopening.
   *
   * Renders the tables like \c open and applies them to the live request
   * through one \c GPIO_V2_LINE_SET_CONFIG_IOCTL. The request is never
   * released, so exclusivity is not lost to a competing consumer and
   * outputs never glitch through an unconfigured state. The tables must
   * address the request's lines element by element, in the order they were
   * opened; only the behaviour (direction, edges, debounce, bias, drive,
   * output levels) may change.
   *
   * @param specs New specs, parallel to the open request's lines.
   * @param configs New per-line config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         \c gpio_error::invalid_argument when the tables are malformed
   *         or do not match the request's lines, otherwise the errno-mapped
   *         kernel error.
   *
   * @pre \p specs and \p configs describe the same lines element by element.
   * @post On success the new behaviour is live; on failure the previous
   *       configuration is untouched.
   */
  auto reconfigure(
    std::span<line_spec const> const specs, std::span<line_config const> const configs
  ) -> result<void> {
    if (!m_request.owns()) {
      return std::unexpected{gpio_error::not_open};
    }
    if (specs.size() != configs.size() || specs.size() != m_offsets.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    for (std::size_t i{0}; i < specs.size(); ++i) {
      if (specs[i].chip() != m_chip || specs[i].offset() != m_offsets[i]) {
        return std::unexpected{gpio_error::invalid_argument};
      }
    }
    ::gpio_v2_line_config config{};
    if (auto const built{build_line_config(specs, configs, &config)}; !built.has_value()) {
      return std::unexpected{built.error()};
    }
    if (::ioctl(m_request.get(), GPIO_V2_LINE_SET_CONFIG_IOCTL, &config)
        < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }
    return {};
  }

  /**
   * @brief Reads the physical level of one requested line.
   *
   * @param offset Line offset within the chip.
   *
   * @return The physical level; \c gpio_error::not_open when closed,
   *         \c gpio_error::not_found when \p offset is not in the request
   *         set, otherwise the errno-mapped kernel error.
   *
   * @pre None.
   * @post No state is changed.
   */
  [[nodiscard]] auto read(line_offset const offset) const -> result<bool> {
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return m_request.owns() ? std::unexpected{gpio_error::not_found}
                              : std::unexpected{gpio_error::not_open};
    }
    ::gpio_v2_line_values values{};
    values.mask = std::uint64_t{1} << *index;
    auto const done{values_ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values)};
    if (!done.has_value()) {
      return std::unexpected{done.error()};
    }
    return (values.bits & (std::uint64_t{1} << *index)) != 0;
  }

  /**
   * @brief Drives the physical level of one requested output line.
   *
   * @param offset Line offset within the chip.
   * @param physical Physical level to drive.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         \c gpio_error::not_found when \p offset is not in the request
   *         set, \c gpio_error::permission_denied when the kernel rejects
   *         writing an input line, otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success the line is driven to \p physical.
   */
  auto write(line_offset const offset, bool const physical) -> result<void> {
    auto const index{index_of(offset)};
    if (!index.has_value()) {
      return m_request.owns() ? std::unexpected{gpio_error::not_found}
                              : std::unexpected{gpio_error::not_open};
    }
    ::gpio_v2_line_values values{};
    values.mask = std::uint64_t{1} << *index;
    values.bits = physical ? values.mask : 0;
    return values_ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
  }

  /**
   * @brief Reads several requested lines in one kernel call.
   *
   * @param offsets Line offsets to read.
   * @param levels_out Caller buffer filled with physical levels, parallel to
   *                   \p offsets.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         spans differ in length, \c gpio_error::not_found when any offset
   *         is not in the request set, otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success \p levels_out holds the level of each offset, observed
   *       atomically by one ioctl.
   */
  auto read_lines(std::span<line_offset const> const offsets, std::span<bool> const levels_out)
    const -> result<void> {
    if (offsets.size() != levels_out.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    ::gpio_v2_line_values values{};
    std::array<std::size_t, max_lines> indices{};
    for (std::size_t i{0}; i < offsets.size(); ++i) {
      auto const index{index_of(offsets[i])};
      if (!index.has_value()) {
        return m_request.owns() ? std::unexpected{gpio_error::not_found}
                                : std::unexpected{gpio_error::not_open};
      }
      indices[i] = *index;
      values.mask |= std::uint64_t{1} << *index;
    }
    auto const done{values_ioctl(GPIO_V2_LINE_GET_VALUES_IOCTL, &values)};
    if (!done.has_value()) {
      return std::unexpected{done.error()};
    }
    for (std::size_t i{0}; i < offsets.size(); ++i) {
      levels_out[i] = (values.bits & (std::uint64_t{1} << indices[i])) != 0;
    }
    return {};
  }

  /**
   * @brief Drives several requested output lines in one kernel call.
   *
   * @param offsets Line offsets to write.
   * @param levels_in Physical levels to drive, parallel to \p offsets.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         spans differ in length, \c gpio_error::not_found when any offset
   *         is not in the request set, otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success every named line is driven, atomically by one ioctl.
   */
  auto write_lines(
    std::span<line_offset const> const offsets, std::span<bool const> const levels_in
  ) -> result<void> {
    if (offsets.size() != levels_in.size()) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    ::gpio_v2_line_values values{};
    for (std::size_t i{0}; i < offsets.size(); ++i) {
      auto const index{index_of(offsets[i])};
      if (!index.has_value()) {
        return m_request.owns() ? std::unexpected{gpio_error::not_found}
                                : std::unexpected{gpio_error::not_open};
      }
      values.mask |= std::uint64_t{1} << *index;
      if (levels_in[i]) {
        values.bits |= std::uint64_t{1} << *index;
      }
    }
    return values_ioctl(GPIO_V2_LINE_SET_VALUES_IOCTL, &values);
  }

  /**
   * @brief Waits for and reads one edge event.
   *
   * Waits on the request descriptor with \c ppoll, then reads exactly one
   * kernel event record. A zero timeout is a non-blocking poll and a
   * negative timeout blocks until an event arrives; an interrupted wait
   * (\c EINTR) reports a clean miss so callers simply loop. The level is
   * implied by the direction the kernel reports: the v2 interface only
   * delivers rising or falling events, so a rising event means the line is
   * now physically high.
   *
   * @param timeout Longest time to wait; zero polls, negative blocks.
   *
   * @return The next event, or \c std::nullopt when the timeout (or a
   *         signal) ended the wait first; \c gpio_error::not_open when
   *         closed, otherwise the errno-mapped error.
   *
   * @pre Edge detection was requested for at least one line.
   * @post On a value result one kernel event record was consumed.
   */
  auto wait_event(std::chrono::nanoseconds const timeout) -> result<std::optional<line_event>> {
    if (!m_request.owns()) {
      return std::unexpected{gpio_error::not_open};
    }

    ::pollfd poll_target{};
    poll_target.fd = m_request.get();
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
        return std::optional<line_event>{};
      }
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }
    if (ready == 0) {
      return std::optional<line_event>{};
    }

    ::gpio_v2_line_event kernel_event{};
    ssize_t const got{::read(m_request.get(), &kernel_event, sizeof(kernel_event))};
    if (got < 0) {
      if (errno == EAGAIN || errno == EINTR) {
        return std::optional<line_event>{};
      }
      return std::unexpected{detail::errno_to_gpio_error(errno)};
    }
    if (got != static_cast<ssize_t>(sizeof(kernel_event))) {
      return std::unexpected{gpio_error::io_error};
    }

    line_event event{};
    event.chip = m_chip;
    event.offset = line_offset{kernel_event.offset};
    event.sequence = event_sequence{kernel_event.seqno};
    event.timestamp =
      event_time{std::chrono::nanoseconds{static_cast<std::int64_t>(kernel_event.timestamp_ns)}};
    event.edge =
      kernel_event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? edge_kind::rising : edge_kind::falling;
    event.physical = event.edge == edge_kind::rising;
    return std::optional<line_event>{event};
  }

  /**
   * @brief The pollable request descriptor for event-loop integration.
   *
   * Register it readable with epoll, a Qt socket notifier, or ASIO, then
   * drain ready events with \c wait_event and a zero timeout.
   *
   * @return The request descriptor, or \c -1 when closed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto native_handle() const noexcept -> native_handle_type {
    return m_request.owns() ? m_request.get() : -1;
  }
};

static_assert(gpio_backend<chardev_chip>, "chardev_chip must satisfy gpio_backend");
static_assert(bulk_gpio_backend<chardev_chip>, "chardev_chip must satisfy bulk_gpio_backend");
static_assert(edge_source<chardev_chip>, "chardev_chip must satisfy edge_source");
static_assert(
  reconfigurable_gpio_backend<chardev_chip>, "chardev_chip must satisfy reconfigurable_gpio_backend"
);

}  // namespace nexenne::gpio

#else  // not Linux: a stub that satisfies the concepts and reports unsupported.

namespace nexenne::gpio {

/**
 * @brief Non-Linux stub for the character-device backend.
 *
 * Satisfies the backend concepts so generic code compiles on every
 * platform, but every operation returns \c gpio_error::unsupported because
 * the GPIO character device is a Linux facility.
 */
class chardev_chip {
public:
  using value_type = bool;
  /// The pollable handle type; always \c -1 here.
  using native_handle_type = int;

  /// Mirrors the Linux kernel per-request line limit.
  static constexpr std::size_t max_lines{64};

private:
  chip_id m_chip{0};

public:
  /**
   * @brief Constructs the stub; nothing can be opened.
   *
   * @param chip Stored for \c chip(); otherwise unused.
   * @param consumer Ignored.
   * @param event_buffer_size Ignored.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  explicit chardev_chip(
    chip_id const chip = chip_id{0},
    std::string_view const consumer = "nexenne-gpio",
    std::uint32_t const event_buffer_size = 0
  ) noexcept
      : m_chip{chip} {
    utility::discard(consumer, event_buffer_size);
  }

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
   * @brief Reports that the character device is unavailable here.
   *
   * @param specs Ignored.
   * @param configs Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto open(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void> {
    utility::discard(specs, configs);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Does nothing; there is never anything to close.
   *
   * @pre None.
   * @post \c is_open() is \c false.
   */
  auto close() noexcept -> void {}

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
   * @brief Reports that reading is unsupported off Linux.
   *
   * @param offset Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto read(line_offset const offset) const -> result<bool> {
    utility::discard(offset);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that writing is unsupported off Linux.
   *
   * @param offset Ignored.
   * @param physical Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto write(line_offset const offset, bool const physical) -> result<void> {
    utility::discard(offset, physical);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that bulk reading is unsupported off Linux.
   *
   * @param offsets Ignored.
   * @param levels_out Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto read_lines(std::span<line_offset const> const offsets, std::span<bool> const levels_out)
    const -> result<void> {
    utility::discard(offsets, levels_out);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that bulk writing is unsupported off Linux.
   *
   * @param offsets Ignored.
   * @param levels_in Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto write_lines(
    std::span<line_offset const> const offsets, std::span<bool const> const levels_in
  ) -> result<void> {
    utility::discard(offsets, levels_in);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that reconfiguration is unsupported off Linux.
   *
   * @param specs Ignored.
   * @param configs Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto reconfigure(
    std::span<line_spec const> const specs, std::span<line_config const> const configs
  ) -> result<void> {
    utility::discard(specs, configs);
    return std::unexpected{gpio_error::unsupported};
  }

  /**
   * @brief Reports that edge events are unsupported off Linux.
   *
   * @param timeout Ignored.
   *
   * @return Always \c gpio_error::unsupported.
   *
   * @pre None.
   * @post None.
   */
  auto wait_event(std::chrono::nanoseconds const timeout) -> result<std::optional<line_event>> {
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

static_assert(gpio_backend<chardev_chip>, "chardev_chip must satisfy gpio_backend");
static_assert(bulk_gpio_backend<chardev_chip>, "chardev_chip must satisfy bulk_gpio_backend");
static_assert(edge_source<chardev_chip>, "chardev_chip must satisfy edge_source");
static_assert(
  reconfigurable_gpio_backend<chardev_chip>, "chardev_chip must satisfy reconfigurable_gpio_backend"
);

}  // namespace nexenne::gpio

#endif  // __linux__
