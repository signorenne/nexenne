#pragma once

/**
 * @file
 * @brief A fluent builder for a CAN message.
 *
 * A \c message.hpp is an identifier, a name, and a list of signals. The
 * builder assembles one through chained calls and returns it from \c build. The
 * message itself stays a plain value type with constructors and accessors; this
 * builder is the ergonomic path for declaring a message and its signals together.
 */

#include <cassert>
#include <string_view>

#include <nexenne/can/id.hpp>
#include <nexenne/can/message.hpp>
#include <nexenne/can/signal.hpp>

namespace nexenne::can {

/**
 * @brief Assembles a \c message from an identifier, a name, and signals.
 *
 * Every setter returns a reference to the builder, so calls chain; \c build
 * returns a copy of the message under construction.
 */
class message_builder {
public:
  using value_type = message;

private:
  message m_message{};

public:
  /**
   * @brief Constructs a builder for an empty, unnamed message.
   *
   * @pre None.
   * @post The pending message has the zero identifier, an empty name, and no
   *       signals.
   */
  message_builder() = default;

  /**
   * @brief Constructs a builder for a message with an identifier and name.
   *
   * @param id Frame identifier the message uses.
   * @param name Non-owning view of the message name; it must outlive the build.
   *
   * @pre None.
   * @post The pending message carries \p id and \p name.
   */
  message_builder(can_id const id, std::string_view const name) noexcept : m_message{id, name} {}

  /**
   * @brief Sets the message identifier.
   *
   * @param id Frame identifier the message uses.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending message's identifier equals \p id.
   */
  auto identifier(can_id const id) noexcept -> message_builder& {
    m_message.id() = id;
    return *this;
  }

  /**
   * @brief Sets the message name.
   *
   * @param name Non-owning view of the name; it must outlive the build.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending message's name equals \p name.
   */
  auto name(std::string_view const name) noexcept -> message_builder& {
    m_message.name() = name;
    return *this;
  }

  /**
   * @brief Sets the on-wire payload length in bytes.
   *
   * @param byte_length Payload byte count; at most 64.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre \p byte_length is at most 64.
   * @post The pending message's byte length equals \p byte_length.
   */
  auto byte_length(std::uint8_t const byte_length) noexcept -> message_builder& {
    assert(byte_length <= max_fd_length && "message_builder: byte_length exceeds 64");
    m_message.byte_length() = byte_length;
    return *this;
  }

  /**
   * @brief Adds a signal to the message, compiling its packing plan.
   *
   * @param sig Signal to add.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre \c sig.length() is between 1 and 64.
   * @post The pending message has gained \p sig.
   */
  auto add(signal const& sig) -> message_builder& {
    m_message.add_signal(sig);
    return *this;
  }

  /**
   * @brief Returns the assembled message.
   *
   * @return A copy of the message under construction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto build() const -> message {
    return m_message;
  }
};

}  // namespace nexenne::can
