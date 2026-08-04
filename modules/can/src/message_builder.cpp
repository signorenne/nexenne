#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/message_builder.hpp>

namespace nexenne::can {

message_builder::message_builder(can_id const id, std::string_view const name) noexcept
    : m_message{id, name} {}

auto message_builder::identifier(can_id const id) noexcept -> message_builder& {
  m_message.id() = id;
  return *this;
}

auto message_builder::name(std::string_view const name) noexcept -> message_builder& {
  m_message.name() = name;
  return *this;
}

auto message_builder::byte_length(std::uint8_t const byte_length) noexcept -> message_builder& {
  assert(byte_length <= max_fd_length && "message_builder: byte_length exceeds 64");
  m_message.byte_length() = byte_length;
  return *this;
}

auto message_builder::add(signal const& sig) -> message_builder& {
  m_message.add_signal(sig);
  return *this;
}

auto message_builder::build() const -> message {
  return m_message;
}

}  // namespace nexenne::can
