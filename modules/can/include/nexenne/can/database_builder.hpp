#pragma once

/**
 * @file
 * @brief A builder that accumulates messages into an immutable database.
 *
 * The builder is the one place a \c database.hpp is assembled. Build each
 * \c message.hpp (an identifier, a name, and its signals), hand it to the
 * builder, and call \c build to get the finished database. Keeping the mutable
 * accumulation here lets the database itself stay immutable.
 *
 * A text database format (DBC) is a planned future source that would feed this
 * same builder; the programmatic API is the supported path today.
 */

#include <utility>

#include <nexenne/can/database.hpp>
#include <nexenne/can/message.hpp>

namespace nexenne::can {

/**
 * @brief Accumulates messages and produces an immutable database.
 *
 * Add fully built messages with \c add_message, then call \c build. The builder
 * is left empty after a build and can be reused.
 */
class database_builder {
public:
  using value_type = message;

private:
  message_list m_messages{};

public:
  /**
   * @brief Constructs an empty builder.
   *
   * @pre None.
   * @post The builder holds no messages.
   */
  database_builder() noexcept = default;

  /**
   * @brief Adds a built message to the database under construction.
   *
   * @param msg Message to add; its signals and plans move in.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The message is appended in order.
   */
  auto add_message(message msg) -> database_builder& {
    m_messages.push_back(std::move(msg));
    return *this;
  }

  /**
   * @brief Builds the database, moving the accumulated messages into it.
   *
   * @return The finished, immutable database.
   *
   * @pre None.
   * @post The returned database holds the added messages in order and the
   *       builder is left empty.
   */
  [[nodiscard]] auto build() noexcept -> database {
    return database{std::move(m_messages)};
  }
};

}  // namespace nexenne::can
