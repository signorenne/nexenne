/**
 * @file
 * @brief The Linux character-device backend: request rendering, value ioctls and edge reads.
 */

#include <nexenne/gpio/io/chardev_chip.hpp>

#ifdef __linux__

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstring>
#include <ctime>
#include <limits>

#include <fcntl.h>
#include <linux/gpio.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace nexenne::gpio {

// The header spells max_lines as a literal to avoid including <linux/gpio.h>;
// keep the two in lockstep.
static_assert(
  chardev_chip::max_lines == GPIO_V2_LINES_MAX,
  "chardev_chip::max_lines must match the kernel per-request line limit"
);

namespace detail {

auto fd_closer::operator()(int const fd) const noexcept -> void {
  ::close(fd);
}

}  // namespace detail

auto chardev_chip::index_of(line_offset const offset) const noexcept -> std::optional<std::size_t> {
  for (std::size_t i{0}; i < m_offsets.size(); ++i) {
    if (m_offsets[i] == offset) {
      return i;
    }
  }
  return std::nullopt;
}

auto chardev_chip::kernel_flags(line_spec const& spec, line_config const& config) noexcept
  -> std::uint64_t {
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

auto chardev_chip::build_line_config(
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

  // Grouped by the whole microseconds the kernel sees: periods that round alike share a slot.
  for (std::size_t i{0}; i < specs.size(); ++i) {
    auto const period{detail::debounce_microseconds(configs[i].debounce_period())};
    if (period.count() <= 0) {
      continue;
    }
    bool grouped{false};
    for (std::size_t j{0}; j < i; ++j) {
      if (detail::debounce_microseconds(configs[j].debounce_period()) == period) {
        grouped = true;
        break;
      }
    }
    if (grouped) {
      continue;
    }
    auto const microseconds{period.count()};
    if (microseconds > std::int64_t{std::numeric_limits<std::uint32_t>::max()}) {
      return std::unexpected{gpio_error::invalid_argument};
    }
    std::uint64_t mask{0};
    for (std::size_t j{i}; j < specs.size(); ++j) {
      if (detail::debounce_microseconds(configs[j].debounce_period()) == period) {
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

auto chardev_chip::values_ioctl(unsigned long const request, ::gpio_v2_line_values* values) const
  -> result<void> {
  if (!m_request.owns()) {
    return std::unexpected{gpio_error::not_open};
  }
  if (::ioctl(m_request.get(), request, values) < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    return std::unexpected{detail::errno_to_gpio_error(errno)};
  }
  return {};
}

static_assert(
  GPIO_MAX_NAME_SIZE == 32, "chardev_chip: the consumer buffer must track the kernel field"
);

chardev_chip::chardev_chip(
  chip_id const chip, std::string_view const consumer, std::uint32_t const event_buffer_size
) noexcept
    : m_chip{chip}, m_consumer_size{consumer.size()}, m_event_buffer_size{event_buffer_size} {
  // Copy what fits; m_consumer_size keeps the requested length so open() can
  // still refuse a label the kernel field cannot hold.
  std::copy_n(consumer.data(), std::min(consumer.size(), m_consumer.size() - 1), m_consumer.data());
}

auto chardev_chip::chip() const noexcept -> chip_id {
  return m_chip;
}

auto chardev_chip::consumer() const noexcept -> std::string_view {
  return std::string_view{m_consumer.data(), std::min(m_consumer_size, m_consumer.size() - 1)};
}

auto chardev_chip::is_open() const noexcept -> bool {
  return m_request.owns();
}

auto chardev_chip::line_count() const noexcept -> std::size_t {
  return m_offsets.size();
}

auto chardev_chip::close() noexcept -> void {
  m_request.reset();
  m_last_sequence = 0;
  m_offsets.clear();
  m_clocks.clear();
  m_pending.clear();
  m_pending_next = 0;
}

auto chardev_chip::open(
  std::span<line_spec const> const specs, std::span<line_config const> const configs
) -> result<void> {
  if (specs.size() != configs.size() || specs.empty() || specs.size() > max_lines
      || detail::has_duplicate_offset(specs)) {
    return std::unexpected{gpio_error::invalid_argument};
  }
  if (m_consumer_size >= GPIO_MAX_NAME_SIZE) {
    return std::unexpected{gpio_error::invalid_argument};
  }
  for (auto const& spec : specs) {
    if (spec.chip() != m_chip) {
      return std::unexpected{gpio_error::invalid_argument};
    }
  }

  close();

  ::gpio_v2_line_request request{};
  request.num_lines = static_cast<std::uint32_t>(specs.size());
  request.event_buffer_size = m_event_buffer_size;
  std::memcpy(static_cast<void*>(request.consumer), m_consumer.data(), m_consumer_size);
  for (std::size_t i{0}; i < specs.size(); ++i) {
    request.offsets[i] = static_cast<std::uint32_t>(specs[i].offset().get());
  }
  if (auto const built{build_line_config(specs, configs, &request.config)}; !built.has_value()) {
    return std::unexpected{built.error()};
  }

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

  if (::ioctl(chip_fd.get(), GPIO_V2_GET_LINE_IOCTL, &request)
      < 0) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
    return std::unexpected{detail::errno_to_gpio_error(errno)};
  }
  m_request.reset(request.fd);
  for (std::size_t i{0}; i < specs.size(); ++i) {
    utility::ignore(m_offsets.push_back(specs[i].offset()));
    utility::ignore(m_clocks.push_back(configs[i].clock()));
  }
  return {};
}

auto chardev_chip::reconfigure(
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
  for (std::size_t i{0}; i < configs.size(); ++i) {
    m_clocks[i] = configs[i].clock();
  }
  return {};
}

auto chardev_chip::read(line_offset const offset) const -> result<bool> {
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

auto chardev_chip::write(line_offset const offset, bool const physical) -> result<void> {
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

auto chardev_chip::read_lines(
  std::span<line_offset const> const offsets, std::span<bool> const levels_out
) const -> result<void> {
  if (offsets.size() != levels_out.size()) {
    return std::unexpected{gpio_error::invalid_argument};
  }
  // The kernel rejects a zero mask, so an empty batch makes no call.
  if (offsets.empty()) {
    return {};
  }
  // The index table below is max_lines wide. A request set cannot hold more
  // than that, but nothing stops a caller repeating one offset past the
  // limit: every repeat resolves, and the writes run off the end of indices.
  if (offsets.size() > max_lines) {
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

auto chardev_chip::write_lines(
  std::span<line_offset const> const offsets, std::span<bool const> const levels_in
) -> result<void> {
  if (offsets.size() != levels_in.size()) {
    return std::unexpected{gpio_error::invalid_argument};
  }
  if (offsets.empty()) {
    return {};
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

auto chardev_chip::wait_event(std::chrono::nanoseconds const timeout)
  -> result<std::optional<line_event>> {
  if (!m_request.owns()) {
    return std::unexpected{gpio_error::not_open};
  }
  if (m_pending_next < m_pending.size()) {
    return std::optional<line_event>{m_pending[m_pending_next++]};
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

  // The kernel hands out whole records only: a partial size is a broken read.
  std::array<::gpio_v2_line_event, event_batch> records{};
  ssize_t const got{::read(m_request.get(), records.data(), sizeof(records))};
  if (got < 0) {
    if (errno == EAGAIN || errno == EINTR) {
      return std::optional<line_event>{};
    }
    return std::unexpected{detail::errno_to_gpio_error(errno)};
  }
  auto const bytes{static_cast<std::size_t>(got)};
  if (bytes == 0 || bytes % sizeof(::gpio_v2_line_event) != 0) {
    return std::unexpected{gpio_error::io_error};
  }

  m_pending.clear();
  m_pending_next = 0;
  for (std::size_t i{0}; i < bytes / sizeof(::gpio_v2_line_event); ++i) {
    auto const& record{records[i]};
    line_event event{};
    event.chip = m_chip;
    event.offset = line_offset{record.offset};
    // Widened in arrival order, so the batch keeps the sequence monotonic.
    m_last_sequence = detail::widen_sequence(m_last_sequence, record.seqno);
    event.sequence = event_sequence{m_last_sequence};
    auto const index{index_of(event.offset)};
    event.timestamp = event_time{
      std::chrono::nanoseconds{static_cast<std::int64_t>(record.timestamp_ns)},
      index.has_value() ? m_clocks[*index] : line_clock::monotonic
    };
    event.edge =
      record.id == GPIO_V2_LINE_EVENT_RISING_EDGE ? edge_kind::rising : edge_kind::falling;
    event.physical = event.edge == edge_kind::rising;
    utility::ignore(m_pending.push_back(event));
  }
  return std::optional<line_event>{m_pending[m_pending_next++]};
}

auto chardev_chip::native_handle() const noexcept -> native_handle_type {
  return m_request.owns() ? m_request.get() : -1;
}

}  // namespace nexenne::gpio

#endif  // __linux__
