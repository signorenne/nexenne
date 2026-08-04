#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/database.hpp>

namespace nexenne::can {

database::database(message_list messages) noexcept : m_messages{std::move(messages)} {}

auto database::messages() const noexcept -> std::span<message const> {
  return std::span<message const>{m_messages.data(), m_messages.size()};
}

auto database::message_count() const noexcept -> std::size_t {
  return m_messages.size();
}

auto database::find(can_id const id) const noexcept -> message const* {
  for (auto const& msg : m_messages) {
    if (msg.id().identifier() == id.identifier() && msg.id().extended() == id.extended()) {
      return &msg;
    }
  }
  return nullptr;
}

auto database::find(std::string_view const name) const noexcept -> message const* {
  for (auto const& msg : m_messages) {
    if (msg.name() == name) {
      return &msg;
    }
  }
  return nullptr;
}

}  // namespace nexenne::can
