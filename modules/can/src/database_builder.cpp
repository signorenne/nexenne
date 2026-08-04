#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/database_builder.hpp>

namespace nexenne::can {

auto database_builder::add_message(message msg) -> database_builder& {
  m_messages.push_back(std::move(msg));
  return *this;
}

auto database_builder::build() noexcept -> database {
  return database{std::move(m_messages)};
}

}  // namespace nexenne::can
