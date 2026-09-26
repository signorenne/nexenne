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

#include <array>
#include <cerrno>
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
#include <nexenne/utility/ignore.hpp>

#ifdef __linux__

#include <nexenne/container/static_vector.hpp>
#include <nexenne/utility/unique_resource.hpp>

// The kernel request structures appear only behind pointers in the private
// interface below, so a forward declaration is enough and <linux/gpio.h> stays
// out of every consumer's include chain. The definitions live in
// src/io/chardev_chip.cpp, which includes the real kernel header.
struct gpio_v2_line_config;
struct gpio_v2_line_values;

namespace nexenne::gpio {

namespace detail {

/// @cond INTERNAL

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
  auto operator()(int fd) const noexcept -> void;
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

/**
 * @brief Widens the kernel's 32-bit event sequence onto the 64-bit count.
 *
 * The kernel numbers a request's events with a 32-bit counter that wraps,
 * while \c event_sequence is 64-bit so a drop count survives the wrap. The
 * result keeps \p seqno as its low half and takes the high half from
 * \p previous, plus one when \p seqno fell below the low half of
 * \p previous (the counter wrapped).
 *
 * @param previous Widened sequence of the request's previous event, or zero
 *        before the first.
 * @param seqno Kernel sequence number of the new event.
 *
 * @return The widened sequence of the new event.
 *
 * @pre Fewer than \c 2^32 events separate the two, so at most one wrap lies
 *      between them.
 * @post The low 32 bits of the result equal \p seqno, and the result is not
 *       below \p previous.
 */
[[nodiscard]] constexpr auto
widen_sequence(std::uint64_t const previous, std::uint32_t const seqno) noexcept -> std::uint64_t {
  constexpr std::uint64_t wrap{std::uint64_t{1} << 32U};
  auto const widened{(previous & ~(wrap - 1)) | seqno};
  return widened < previous ? widened + wrap : widened;
}

/**
 * @brief The kernel debounce period for \p period, in whole microseconds.
 *
 * The kernel attribute counts microseconds. Rounding up keeps a
 * sub-microsecond period enabled, and periods that round alike share one of
 * the ten attribute slots.
 *
 * @param period Requested debounce period.
 *
 * @return \p period rounded up to whole microseconds.
 *
 * @pre None.
 * @post The result is not shorter than \p period.
 */
[[nodiscard]] constexpr auto debounce_microseconds(std::chrono::nanoseconds const period) noexcept
  -> std::chrono::microseconds {
  return std::chrono::ceil<std::chrono::microseconds>(period);
}

/// @endcond

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
  /// @brief The pollable request file descriptor type.
  using native_handle_type = int;

  /// @brief Largest number of lines one request can hold (a kernel limit).
  // Spelled literally so the kernel header stays out of this file;
  // src/io/chardev_chip.cpp static_asserts it against GPIO_V2_LINES_MAX.
  static constexpr std::size_t max_lines{64};

private:
  using fd_handle = utility::unique_resource<int, detail::fd_closer>;

  chip_id m_chip{0};
  // Owned, not borrowed. A string_view member dangles whenever the label is
  // built at the call site, and the ioctl reads it much later, at open().
  // Sized like the kernel's consumer field, which src/io/chardev_chip.cpp
  // static_asserts against GPIO_MAX_NAME_SIZE. The requested length is kept
  // whole so an over-long label is still refused rather than truncated.
  std::array<char, 32> m_consumer{};
  std::size_t m_consumer_size{0};
  std::uint32_t m_event_buffer_size{0};
  fd_handle m_request{};
  // Widened sequence of the request's last event; the kernel restarts its
  // 32-bit counter with each request.
  std::uint64_t m_last_sequence{0};
  container::static_vector<line_offset, max_lines> m_offsets{};

  /**
   * @brief Position of \p offset in the open request.
   *
   * @param offset Line offset to look up.
   *
   * @return The line's index in the request, or \c std::nullopt when
   *         \p offset was not requested.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto index_of(line_offset const offset) const noexcept
    -> std::optional<std::size_t>;

  /**
   * @brief Renders one line's spec and config as kernel flag bits.
   *
   * \c ACTIVE_LOW is never set: polarity belongs to the wrapper layer, so the
   * kernel's "active" always means the physical high level.
   *
   * @param spec The line's spec.
   * @param config The line's open-time config.
   *
   * @return The kernel line flags for the line.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] static auto kernel_flags(line_spec const& spec, line_config const& config) noexcept
    -> std::uint64_t;

  /**
   * @brief Renders the parallel tables into a kernel line config.
   *
   * The first line's flag word becomes the base, deviating flag words are
   * grouped into one attribute each, and initial output levels and distinct
   * debounce periods become further attributes, within the kernel's
   * ten-attribute cap.
   *
   * @param specs Specs of the request, one per line.
   * @param configs Per-line config, parallel to \p specs.
   * @param config Kernel line config to fill.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         request needs more than ten attributes or a debounce period
   *         beyond the kernel's microsecond range.
   *
   * @pre \p config points to a zero-initialised kernel line config and
   *      \p specs and \p configs have the same length.
   * @post On success \p config describes every line of the request.
   */
  [[nodiscard]] static auto build_line_config(
    std::span<line_spec const> const specs,
    std::span<line_config const> const configs,
    ::gpio_v2_line_config* const config
  ) -> result<void>;

  /**
   * @brief Issues a line-values ioctl on the open request.
   *
   * @param request The ioctl request code, getting or setting values.
   * @param values Kernel values record the ioctl reads or fills.
   *
   * @return Nothing on success; \c gpio_error::not_open when closed,
   *         otherwise the errno-mapped error.
   *
   * @pre \p values points to a record whose mask names requested lines.
   * @post On success the kernel has read or filled \p values.
   */
  [[nodiscard]] auto values_ioctl(unsigned long const request, ::gpio_v2_line_values* values) const
    -> result<void>;

public:
  /**
   * @brief Constructs a closed backend for one chip.
   *
   * Nothing is opened until \c open is called.
   *
   * @param chip Index of the chip; selects \c /dev/gpiochipN.
   * @param consumer Consumer label reported to the kernel; shown by tools
   *                 like \c gpioinfo. Copied into the backend, so a temporary
   *                 is safe. Must be shorter than 32 bytes; a longer label is
   *                 kept whole enough for \c open to refuse it.
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
  ) noexcept;

  /**
   * @brief The chip index this backend targets.
   *
   * @return The stored chip identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto chip() const noexcept -> chip_id;

  /**
   * @brief Whether a line request is currently held.
   *
   * @return \c true between a successful \c open and the next \c close.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto is_open() const noexcept -> bool;

  /**
   * @brief Releases the line request.
   *
   * Safe to call when already closed.
   *
   * @pre None.
   * @post \c is_open() is \c false and the request descriptor is closed.
   */
  auto close() noexcept -> void;

  /**
   * @brief Opens the chip and requests all lines in one atomic ioctl.
   *
   * Renders each spec and config into kernel flag words. The first line's
   * behaviour becomes the request's base flags; lines that differ are
   * grouped into per-group attribute overrides, and initial output levels
   * and per-line debounce periods (rounded up to whole microseconds) become
   * further attributes. The kernel caps a request at ten attributes, shared
   * by the distinct line behaviours beyond the base, one slot for the initial
   * output levels when any line is an output, and one per distinct debounce
   * period.
   *
   * @param specs Specs to request, one per line, all on this chip.
   * @param configs Per-line open-time config, parallel to \p specs.
   *
   * @return Nothing on success; \c gpio_error::invalid_argument when the
   *         tables are malformed, name an offset twice or a different chip, exceed
   *         \c max_lines, need more than ten attribute groups, or carry a
   *         debounce period beyond the kernel's microsecond range;
   *         otherwise the errno-mapped kernel error (notably
   *         \c gpio_error::busy when another consumer holds a line and
   *         \c gpio_error::permission_denied on device permissions).
   *
   * @pre \p specs and \p configs describe the same lines element by element.
   * @post On success \c is_open() is \c true. A request refused by the
   *       checks above leaves the backend as it was; any later failure leaves
   *       it closed, the previous request released.
   */
  [[nodiscard]] auto
  open(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void>;

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
  [[nodiscard]] auto
  reconfigure(std::span<line_spec const> const specs, std::span<line_config const> const configs)
    -> result<void>;

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
  [[nodiscard]] auto read(line_offset const offset) const -> result<bool>;

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
  [[nodiscard]] auto write(line_offset const offset, bool const physical) -> result<void>;

  /**
   * @brief Reads several requested lines in one kernel call.
   *
   * @param offsets Line offsets to read.
   * @param levels_out Caller buffer filled with physical levels, parallel to
   *                   \p offsets.
   *
   * @return Nothing on success, and at once for empty spans, which make no
   *         call; \c gpio_error::invalid_argument when the spans differ in
   *         length or hold more than \c max_lines offsets,
   *         \c gpio_error::not_found when any offset is not in the request
   *         set, otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success \p levels_out holds the level of each offset, observed
   *       atomically by one ioctl.
   */
  [[nodiscard]] auto
  read_lines(std::span<line_offset const> const offsets, std::span<bool> const levels_out) const
    -> result<void>;

  /**
   * @brief Drives several requested output lines in one kernel call.
   *
   * @param offsets Line offsets to write.
   * @param levels_in Physical levels to drive, parallel to \p offsets.
   *
   * @return Nothing on success, and at once for empty spans, which make no
   *         call; \c gpio_error::invalid_argument when the spans differ in
   *         length, \c gpio_error::not_found when any offset is not in the
   *         request set, otherwise the errno-mapped error.
   *
   * @pre None.
   * @post On success every named line is driven, atomically by one ioctl.
   */
  [[nodiscard]] auto
  write_lines(std::span<line_offset const> const offsets, std::span<bool const> const levels_in)
    -> result<void>;

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
  [[nodiscard]] auto wait_event(std::chrono::nanoseconds const timeout)
    -> result<std::optional<line_event>>;

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
  [[nodiscard]] auto native_handle() const noexcept -> native_handle_type;
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
  /// @brief The pollable handle type; always \c -1 here.
  using native_handle_type = int;

  /// @brief Mirrors the Linux kernel per-request line limit.
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
    [[maybe_unused]] std::string_view const consumer = "nexenne-gpio",
    [[maybe_unused]] std::uint32_t const event_buffer_size = 0
  ) noexcept
      : m_chip{chip} {}

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
  [[nodiscard]] auto open(
    [[maybe_unused]] std::span<line_spec const> const specs,
    [[maybe_unused]] std::span<line_config const> const configs
  ) -> result<void> {
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
  [[nodiscard]] auto read([[maybe_unused]] line_offset const offset) const -> result<bool> {
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
  [[nodiscard]] auto
  write([[maybe_unused]] line_offset const offset, [[maybe_unused]] bool const physical)
    -> result<void> {
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
  [[nodiscard]] auto read_lines(
    [[maybe_unused]] std::span<line_offset const> const offsets,
    [[maybe_unused]] std::span<bool> const levels_out
  ) const -> result<void> {
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
  [[nodiscard]] auto write_lines(
    [[maybe_unused]] std::span<line_offset const> const offsets,
    [[maybe_unused]] std::span<bool const> const levels_in
  ) -> result<void> {
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
  [[nodiscard]] auto reconfigure(
    [[maybe_unused]] std::span<line_spec const> const specs,
    [[maybe_unused]] std::span<line_config const> const configs
  ) -> result<void> {
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
  [[nodiscard]] auto wait_event([[maybe_unused]] std::chrono::nanoseconds const timeout)
    -> result<std::optional<line_event>> {
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
