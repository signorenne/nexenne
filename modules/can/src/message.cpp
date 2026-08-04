#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/message.hpp>

namespace nexenne::can {

message::message(
  can_id const id, std::string_view const name, std::uint8_t const byte_length
) noexcept
    : m_id{id}, m_name{name}, m_byte_length{byte_length} {}

auto message::add_signal(signal const& sig) -> message& {
  m_signals.push_back(signal_entry{sig, packing_plan::from_signal(sig)});
  return *this;
}

auto message::id() noexcept -> can_id& {
  return m_id;
}

auto message::id() const noexcept -> can_id const& {
  return m_id;
}

auto message::name() noexcept -> std::string_view& {
  return m_name;
}

auto message::name() const noexcept -> std::string_view const& {
  return m_name;
}

auto message::byte_length() noexcept -> std::uint8_t& {
  return m_byte_length;
}

auto message::byte_length() const noexcept -> std::uint8_t const& {
  return m_byte_length;
}

auto message::signals() noexcept -> std::span<signal_entry> {
  return std::span<signal_entry>{m_signals.data(), m_signals.size()};
}

auto message::signals() const noexcept -> std::span<signal_entry const> {
  return std::span<signal_entry const>{m_signals.data(), m_signals.size()};
}

auto message::signal_count() const noexcept -> std::size_t {
  return m_signals.size();
}

auto message::find_signal(std::string_view const name) const noexcept
  -> std::optional<std::size_t> {
  for (std::size_t i{0}; i < m_signals.size(); ++i) {
    if (m_signals[i].definition.name() == name) {
      return i;
    }
  }
  return std::nullopt;
}

}  // namespace nexenne::can
