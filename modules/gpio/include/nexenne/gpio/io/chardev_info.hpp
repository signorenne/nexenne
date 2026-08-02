#pragma once

/**
 * @file
 * @brief Discovery over the Linux GPIO character device: chips and lines.
 *
 * Hard-coding a line offset is the classic way to toggle the wrong pin on
 * the next board revision. The kernel already knows each line's name (from
 * the device tree or board file), its current consumer, and its
 * configuration, and exposes all of it through info ioctls on
 * \c /dev/gpiochipN. These helpers read that: \c read_chip_info names a
 * chip and counts its lines, \c read_line_info describes one line, and
 * \c find_line resolves a kernel line name to its offset so a \c line_spec
 * can be built from a name instead of a magic number.
 *
 * The info records own their characters in fixed arrays (the kernel's own
 * 32-byte fields), so they are self-contained values with no lifetime
 * strings attached, safe to store and pass around.
 *
 * Everything here is read-only and touches no line state. Off Linux the
 * functions exist and return \c gpio_error::unsupported.
 */

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

#include <nexenne/gpio/error.hpp>
#include <nexenne/gpio/line_types.hpp>
#include <nexenne/utility/discard.hpp>

namespace nexenne::gpio {

/**
 * @brief Identity of one GPIO chip: kernel name, label, and line count.
 */
class chip_info {
public:
  using value_type = std::string_view;

private:
  std::array<char, 32> m_name{};
  std::array<char, 32> m_label{};
  std::uint32_t m_lines{0};

  [[nodiscard]] static constexpr auto to_view(std::array<char, 32> const& field
  ) noexcept -> std::string_view {
    std::size_t length{0};
    while (length < field.size() && field[length] != '\0') {
      length += 1;
    }
    return std::string_view{field.data(), length};
  }

public:
  /**
   * @brief Constructs an empty record.
   *
   * @pre None.
   * @post Both names are empty and \c lines() is zero.
   */
  constexpr chip_info() noexcept = default;

  /**
   * @brief Constructs a record from raw kernel fields.
   *
   * @param name Kernel chip name, null-terminated within 32 bytes.
   * @param label Functional chip label, null-terminated within 32 bytes.
   * @param lines Number of lines on the chip.
   *
   * @pre None.
   * @post Every accessor returns the corresponding argument.
   */
  constexpr chip_info(
    std::array<char, 32> const& name, std::array<char, 32> const& label, std::uint32_t const lines
  ) noexcept
      : m_name{name}, m_label{label}, m_lines{lines} {}

  /**
   * @brief The kernel name of the chip, such as \c gpiochip0.
   *
   * @return A view into this record's own storage.
   *
   * @pre None.
   * @post The view is valid for this record's lifetime.
   */
  [[nodiscard]] constexpr auto name() const noexcept -> std::string_view {
    return to_view(m_name);
  }

  /**
   * @brief The functional label of the chip, such as a controller name.
   *
   * @return A view into this record's own storage; may be empty.
   *
   * @pre None.
   * @post The view is valid for this record's lifetime.
   */
  [[nodiscard]] constexpr auto label() const noexcept -> std::string_view {
    return to_view(m_label);
  }

  /**
   * @brief The number of lines this chip exposes.
   *
   * @return The line count; valid offsets are zero up to one below it.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto lines() const noexcept -> std::uint32_t {
    return m_lines;
  }
};

/**
 * @brief State of one line as the kernel reports it.
 *
 * The flags are decoded into the module's own vocabulary, so callers never
 * touch kernel flag bits. Note that \c active_low reports how the CURRENT
 * consumer configured the line, not a property of the hardware.
 */
class line_info {
public:
  using value_type = std::string_view;

private:
  std::array<char, 32> m_name{};
  std::array<char, 32> m_consumer{};
  line_offset m_offset{0};
  line_direction m_direction{line_direction::input};
  line_bias m_bias{line_bias::as_is};
  line_drive m_drive{line_drive::push_pull};
  edge_detection m_edges{edge_detection::none};
  bool m_used{false};
  bool m_active_low{false};

  [[nodiscard]] static constexpr auto to_view(std::array<char, 32> const& field
  ) noexcept -> std::string_view {
    std::size_t length{0};
    while (length < field.size() && field[length] != '\0') {
      length += 1;
    }
    return std::string_view{field.data(), length};
  }

public:
  /**
   * @brief Constructs an empty record.
   *
   * @pre None.
   * @post Every accessor returns its default.
   */
  constexpr line_info() noexcept = default;

  /**
   * @brief Constructs a record from decoded fields.
   *
   * @param name Kernel line name, null-terminated within 32 bytes.
   * @param consumer Current consumer label, null-terminated within 32 bytes.
   * @param offset Line offset within its chip.
   * @param direction Decoded direction.
   * @param bias Decoded bias selection.
   * @param drive Decoded drive topology.
   * @param edges Decoded edge subscription of the current consumer.
   * @param used Whether any consumer holds the line.
   * @param active_low Whether the current consumer inverts the line.
   *
   * @pre None.
   * @post Every accessor returns the corresponding argument.
   */
  constexpr line_info(
    std::array<char, 32> const& name,
    std::array<char, 32> const& consumer,
    line_offset const offset,
    line_direction const direction,
    line_bias const bias,
    line_drive const drive,
    edge_detection const edges,
    bool const used,
    bool const active_low
  ) noexcept
      : m_name{name}
      , m_consumer{consumer}
      , m_offset{offset}
      , m_direction{direction}
      , m_bias{bias}
      , m_drive{drive}
      , m_edges{edges}
      , m_used{used}
      , m_active_low{active_low} {}

  /**
   * @brief The kernel name of the line, as set by the board description.
   *
   * @return A view into this record's own storage; may be empty.
   *
   * @pre None.
   * @post The view is valid for this record's lifetime.
   */
  [[nodiscard]] constexpr auto name() const noexcept -> std::string_view {
    return to_view(m_name);
  }

  /**
   * @brief The label of whoever currently holds the line.
   *
   * @return A view into this record's own storage; empty when unclaimed.
   *
   * @pre None.
   * @post The view is valid for this record's lifetime.
   */
  [[nodiscard]] constexpr auto consumer() const noexcept -> std::string_view {
    return to_view(m_consumer);
  }

  /**
   * @brief The line offset within its chip.
   *
   * @return The stored offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto offset() const noexcept -> line_offset {
    return m_offset;
  }

  /**
   * @brief The line's current direction.
   *
   * @return The decoded direction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto direction() const noexcept -> line_direction {
    return m_direction;
  }

  /**
   * @brief The line's current bias selection.
   *
   * @return The decoded bias; \c line_bias::as_is when none is reported.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bias() const noexcept -> line_bias {
    return m_bias;
  }

  /**
   * @brief The line's current drive topology.
   *
   * @return The decoded drive topology.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto drive() const noexcept -> line_drive {
    return m_drive;
  }

  /**
   * @brief The edge subscription of the line's current consumer.
   *
   * @return The decoded edge-detection mode.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto edges() const noexcept -> edge_detection {
    return m_edges;
  }

  /**
   * @brief Whether any consumer currently holds the line.
   *
   * @return \c true when the line is unavailable for a new request.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto used() const noexcept -> bool {
    return m_used;
  }

  /**
   * @brief Whether the current consumer configured the line active-low.
   *
   * @return \c true when the kernel inverts the line for its consumer.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto active_low() const noexcept -> bool {
    return m_active_low;
  }
};

}  // namespace nexenne::gpio

#ifdef __linux__

#  include <cerrno>
#  include <cstdio>

#  include <fcntl.h>
#  include <linux/gpio.h>
#  include <nexenne/utility/unique_resource.hpp>
#  include <sys/ioctl.h>
#  include <unistd.h>

namespace nexenne::gpio {

namespace detail {

/// @cond INTERNAL
struct info_fd_closer {
  auto operator()(int const fd) const noexcept -> void {
    ::close(fd);
  }
};

[[nodiscard]] inline auto open_chip_readonly(chip_id const chip
) noexcept -> utility::unique_resource<int, info_fd_closer> {
  std::array<char, 32> path{};
  utility::discard(
    std::snprintf(path.data(), path.size(), "/dev/gpiochip%u", static_cast<unsigned>(chip.get()))
  );
  return utility::make_unique_resource_checked(
    ::open(path.data(), O_RDONLY | O_CLOEXEC), -1, info_fd_closer{}
  );
}

[[nodiscard]] inline auto info_errno() noexcept -> gpio_error {
  switch (errno) {
    case ENOENT:
    case ENODEV:
    case ENXIO:
      return gpio_error::not_found;
    case EACCES:
    case EPERM:
      return gpio_error::permission_denied;
    case EBUSY:
      return gpio_error::busy;
    default:
      return gpio_error::io_error;
  }
}

[[nodiscard]] inline auto decode_line_info(::gpio_v2_line_info const& raw) -> line_info {
  std::array<char, 32> name{};
  std::array<char, 32> consumer{};
  for (std::size_t i{0}; i < name.size(); ++i) {
    name[i] = raw.name[i];
    consumer[i] = raw.consumer[i];
  }

  auto const flags{raw.flags};
  auto const direction{
    (flags & GPIO_V2_LINE_FLAG_OUTPUT) != 0 ? line_direction::output : line_direction::input
  };
  auto bias{line_bias::as_is};
  if ((flags & GPIO_V2_LINE_FLAG_BIAS_PULL_UP) != 0) {
    bias = line_bias::pull_up;
  } else if ((flags & GPIO_V2_LINE_FLAG_BIAS_PULL_DOWN) != 0) {
    bias = line_bias::pull_down;
  } else if ((flags & GPIO_V2_LINE_FLAG_BIAS_DISABLED) != 0) {
    bias = line_bias::disabled;
  }
  auto drive{line_drive::push_pull};
  if ((flags & GPIO_V2_LINE_FLAG_OPEN_DRAIN) != 0) {
    drive = line_drive::open_drain;
  } else if ((flags & GPIO_V2_LINE_FLAG_OPEN_SOURCE) != 0) {
    drive = line_drive::open_source;
  }
  bool const rising{(flags & GPIO_V2_LINE_FLAG_EDGE_RISING) != 0};
  bool const falling{(flags & GPIO_V2_LINE_FLAG_EDGE_FALLING) != 0};
  auto edges{edge_detection::none};
  if (rising && falling) {
    edges = edge_detection::both;
  } else if (rising) {
    edges = edge_detection::rising;
  } else if (falling) {
    edges = edge_detection::falling;
  }

  return line_info{
    name,
    consumer,
    line_offset{raw.offset},
    direction,
    bias,
    drive,
    edges,
    (flags & GPIO_V2_LINE_FLAG_USED) != 0,
    (flags & GPIO_V2_LINE_FLAG_ACTIVE_LOW) != 0,
  };
}

/// @endcond

}  // namespace detail

/**
 * @brief Reads a chip's identity from the character device.
 *
 * @param chip Index of the chip; selects \c /dev/gpiochipN.
 *
 * @return The chip's name, label, and line count;
 *         \c gpio_error::not_found when no such device exists,
 *         \c gpio_error::permission_denied on device permissions, otherwise
 *         \c gpio_error::io_error.
 *
 * @pre None.
 * @post No line state is touched.
 */
[[nodiscard]] inline auto read_chip_info(chip_id const chip) -> result<chip_info> {
  auto const fd{detail::open_chip_readonly(chip)};
  if (!fd.owns()) {
    return std::unexpected{detail::info_errno()};
  }
  ::gpiochip_info raw{};
  if (::ioctl(fd.get(), GPIO_GET_CHIPINFO_IOCTL, &raw) < 0) {
    return std::unexpected{detail::info_errno()};
  }
  std::array<char, 32> name{};
  std::array<char, 32> label{};
  for (std::size_t i{0}; i < name.size(); ++i) {
    name[i] = raw.name[i];
    label[i] = raw.label[i];
  }
  return chip_info{name, label, raw.lines};
}

/**
 * @brief Reads one line's kernel-reported state.
 *
 * @param chip Index of the chip; selects \c /dev/gpiochipN.
 * @param offset Line offset within the chip.
 *
 * @return The decoded line information; \c gpio_error::not_found when the
 *         chip does not exist, \c gpio_error::invalid_argument when the
 *         offset is out of range for it, otherwise an errno-mapped error.
 *
 * @pre None.
 * @post No line state is touched.
 */
[[nodiscard]] inline auto
read_line_info(chip_id const chip, line_offset const offset) -> result<line_info> {
  auto const fd{detail::open_chip_readonly(chip)};
  if (!fd.owns()) {
    return std::unexpected{detail::info_errno()};
  }
  ::gpio_v2_line_info raw{};
  raw.offset = static_cast<std::uint32_t>(offset.get());
  if (::ioctl(fd.get(), GPIO_V2_GET_LINEINFO_IOCTL, &raw) < 0) {
    return std::unexpected{errno == EINVAL ? gpio_error::invalid_argument : detail::info_errno()};
  }
  return detail::decode_line_info(raw);
}

/**
 * @brief Resolves a kernel line name to its offset on one chip.
 *
 * Walks the chip's lines comparing kernel names. Line names are not
 * guaranteed unique; the first match wins, matching kernel tooling.
 *
 * @param chip Index of the chip; selects \c /dev/gpiochipN.
 * @param name Kernel line name to look for.
 *
 * @return The offset of the first line with that name, or \c std::nullopt
 *         when no line on the chip has it (a clean miss, not an error);
 *         \c gpio_error::not_found when the chip itself does not exist,
 *         otherwise an errno-mapped error.
 *
 * @pre None.
 * @post No line state is touched.
 */
[[nodiscard]] inline auto
find_line(chip_id const chip, std::string_view const name) -> result<std::optional<line_offset>> {
  auto const info{read_chip_info(chip)};
  if (!info.has_value()) {
    return std::unexpected{info.error()};
  }
  for (std::uint32_t i{0}; i < info->lines(); ++i) {
    auto const line{read_line_info(chip, line_offset{i})};
    if (!line.has_value()) {
      return std::unexpected{line.error()};
    }
    if (line->name() == name) {
      return std::optional<line_offset>{line_offset{i}};
    }
  }
  return std::optional<line_offset>{};
}

}  // namespace nexenne::gpio

#else  // not Linux: stubs that compile everywhere and report unsupported.

namespace nexenne::gpio {

/**
 * @brief Reports that chip discovery is unsupported off Linux.
 *
 * @param chip Ignored.
 *
 * @return Always \c gpio_error::unsupported.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto read_chip_info(chip_id const chip) -> result<chip_info> {
  utility::discard(chip);
  return std::unexpected{gpio_error::unsupported};
}

/**
 * @brief Reports that line discovery is unsupported off Linux.
 *
 * @param chip Ignored.
 * @param offset Ignored.
 *
 * @return Always \c gpio_error::unsupported.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto
read_line_info(chip_id const chip, line_offset const offset) -> result<line_info> {
  utility::discard(chip, offset);
  return std::unexpected{gpio_error::unsupported};
}

/**
 * @brief Reports that name resolution is unsupported off Linux.
 *
 * @param chip Ignored.
 * @param name Ignored.
 *
 * @return Always \c gpio_error::unsupported.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] inline auto
find_line(chip_id const chip, std::string_view const name) -> result<std::optional<line_offset>> {
  utility::discard(chip, name);
  return std::unexpected{gpio_error::unsupported};
}

}  // namespace nexenne::gpio

#endif  // __linux__
