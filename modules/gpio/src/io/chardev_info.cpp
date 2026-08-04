#include <nexenne/gpio/io/chardev_info.hpp>

#ifdef __linux__

#include <algorithm>
#include <cerrno>
#include <charconv>

#include <fcntl.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace nexenne::gpio {

namespace detail {

auto info_fd_closer::operator()(int const fd) const noexcept -> void {
  ::close(fd);
}

auto open_chip_readonly(chip_id const chip) noexcept
  -> utility::unique_resource<int, info_fd_closer> {
  // Rendered with to_chars rather than a vararg call: type-safe, and the
  // buffer is wide enough for the prefix plus any 16-bit index.
  std::array<char, 32> path{};
  constexpr std::string_view prefix{"/dev/gpiochip"};
  std::ranges::copy(prefix, path.begin());
  auto const rendered{
    std::to_chars(path.data() + prefix.size(), path.data() + path.size() - 1, chip.get())
  };
  *rendered.ptr = '\0';
  return utility::make_unique_resource_checked(
    ::open(path.data(), O_RDONLY | O_CLOEXEC), -1, info_fd_closer{}
  );
}

auto info_errno() noexcept -> gpio_error {
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

auto decode_line_info(::gpio_v2_line_info const& raw) -> line_info {
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

}  // namespace detail

auto read_chip_info(chip_id const chip) -> result<chip_info> {
  auto const fd{detail::open_chip_readonly(chip)};
  if (!fd.owns()) {
    return std::unexpected{detail::info_errno()};
  }
  ::gpiochip_info raw{};
  if (::ioctl(fd.get(), GPIO_GET_CHIPINFO_IOCTL, &raw)
      < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
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

auto read_line_info(chip_id const chip, line_offset const offset) -> result<line_info> {
  auto const fd{detail::open_chip_readonly(chip)};
  if (!fd.owns()) {
    return std::unexpected{detail::info_errno()};
  }
  ::gpio_v2_line_info raw{};
  raw.offset = static_cast<std::uint32_t>(offset.get());
  if (::ioctl(fd.get(), GPIO_V2_GET_LINEINFO_IOCTL, &raw)
      < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    return std::unexpected{errno == EINVAL ? gpio_error::invalid_argument : detail::info_errno()};
  }
  return detail::decode_line_info(raw);
}

auto find_line(chip_id const chip, std::string_view const name)
  -> result<std::optional<line_offset>> {
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

#endif  // __linux__
