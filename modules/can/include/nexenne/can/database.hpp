#pragma once

/**
 * @file
 * @brief An immutable collection of CAN messages and the signals they carry.
 *
 * A \c database is the compiled description of a bus: every message, each with
 * its signals and their packing plans. It is built once through a
 * \c database_builder.hpp and then only read, so it has no mutators. The
 * fast receive-side lookup of a frame to its message lives in the
 * \c registry.hpp, which indexes a database; the database itself offers a
 * straightforward linear \c find for convenience.
 */

#include <cstddef>
#include <span>
#include <string_view>
#include <utility>

#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/container/small_vector.hpp>

namespace nexenne::can {

/**
 * @brief The list type a database holds, inline up to a few messages.
 */
using message_list = container::small_vector<message, 8>;

/**
 * @brief An immutable set of CAN messages.
 *
 * Construct one from a built list of messages (the builder does this) and then
 * read it. The messages keep the order they were added.
 */
class database {
public:
  using value_type = message;

private:
  message_list m_messages{};

public:
  /**
   * @brief Constructs an empty database.
   *
   * @pre None.
   * @post \c message_count() is zero.
   */
  database() noexcept = default;

  /**
   * @brief Constructs a database from a built list of messages.
   *
   * @param messages Messages to take ownership of.
   *
   * @pre None.
   * @post \c messages() returns the given messages in order.
   */
  explicit database(message_list messages) noexcept : m_messages{std::move(messages)} {}

  /**
   * @brief Read-only view of the messages.
   *
   * The database is immutable once built; messages are added through the
   * \c database_builder, never reordered or resized afterward, so a \c registry
   * can index it by position safely.
   *
   * @return A span of the database's messages.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto messages() const noexcept -> std::span<message const> {
    return std::span<message const>{m_messages.data(), m_messages.size()};
  }

  /**
   * @brief The number of messages in the database.
   *
   * @return The message count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto message_count() const noexcept -> std::size_t {
    return m_messages.size();
  }

  /**
   * @brief Finds a message by identifier with a linear scan.
   *
   * Matches the identifier value and the extended-frame flag, ignoring the
   * remote and error flags. For the receive hot path prefer a \c registry.hpp,
   * which indexes the lookup.
   *
   * @param id Identifier to look up.
   *
   * @return A pointer to the first matching message, or \c nullptr when none
   *         matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find(can_id const id) const noexcept -> message const* {
    for (auto const& msg : m_messages) {
      if (msg.id().identifier() == id.identifier() && msg.id().extended() == id.extended()) {
        return &msg;
      }
    }
    return nullptr;
  }

  /**
   * @brief Finds a message by name.
   *
   * A linear scan, off the receive hot path, mirroring cantools'
   * \c get_message_by_name.
   *
   * @param name Message name to look up.
   *
   * @return A pointer to the first message with that name, or \c nullptr when
   *         none matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find(std::string_view const name) const noexcept -> message const* {
    for (auto const& msg : m_messages) {
      if (msg.name() == name) {
        return &msg;
      }
    }
    return nullptr;
  }
};

}  // namespace nexenne::can
