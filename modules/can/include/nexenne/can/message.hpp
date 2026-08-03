#pragma once

/**
 * @file
 * @brief A named CAN message: an identifier and the signals packed into its frame.
 *
 * A \c message groups the signals that share one frame identifier, each stored
 * with its name and its compiled \c packing_plan.hpp so decoding a frame
 * does no plan building on the hot path. A message owns its signals in a small
 * inline vector that spills to the heap only when a frame carries many signals,
 * which happens once at build time, never while decoding. Build one directly with
 * \c add_signal, or fluently with a \c message_builder.hpp.
 */

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

#include <nexenne/can/dlc.hpp>
#include <nexenne/can/id.hpp>
#include <nexenne/can/packing_plan.hpp>
#include <nexenne/can/signal.hpp>
#include <nexenne/container/small_vector.hpp>

namespace nexenne::can {

/**
 * @brief A signal paired with its compiled packing plan.
 *
 * The plan is computed once from the signal so the codec never rebuilds it.
 */
struct signal_entry {
  signal definition;  ///< The signal description, including its name and scaling.
  packing_plan plan;  ///< The compiled bit layout of \c definition.
};

/**
 * @brief A named message: a frame identifier and the signals it carries.
 *
 * Build one by constructing with an identifier and name, then adding signals;
 * each added signal is compiled into a \c signal_entry. The identifier and name
 * have mutable and const accessors. Lookups by signal name are linear, which
 * suits the handful of signals a frame holds.
 */
class message {
public:
  using value_type = signal_entry;

  /// @brief Inline signal capacity before the storage spills to the heap.
  static constexpr std::size_t inline_signals{4};

private:
  can_id m_id{};
  std::string_view m_name{};
  std::uint8_t m_byte_length{max_classic_length};
  container::small_vector<signal_entry, inline_signals> m_signals{};

public:
  /**
   * @brief Constructs an empty, unnamed message with the zero identifier.
   *
   * @pre None.
   * @post \c id() is the zero standard identifier, \c name() is empty, and the
   *       message has no signals.
   */
  message() = default;

  /**
   * @brief Constructs a message with an identifier and name.
   *
   * @param id Frame identifier the message uses.
   * @param name Non-owning view of the message name.
   * @param byte_length On-wire payload length in bytes; defaults to 8.
   *
   * @pre The string \p name refers to outlives the message; \p byte_length is at
   *      most 64.
   * @post \c id(), \c name(), and \c byte_length() reflect the arguments and the
   *       message has no signals.
   */
  message(
    can_id const id, std::string_view const name, std::uint8_t const byte_length = max_classic_length
  ) noexcept
      : m_id{id}, m_name{name}, m_byte_length{byte_length} {}

  /**
   * @brief Adds a signal, compiling and storing its packing plan.
   *
   * @param sig Signal to add.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre \c sig.length() is between 1 and 64.
   * @post \c signal_count() has grown by one and the new entry holds \p sig and
   *       its compiled plan.
   */
  auto add_signal(signal const& sig) -> message& {
    m_signals.push_back(signal_entry{sig, packing_plan::from_signal(sig)});
    return *this;
  }

  /**
   * @brief The message identifier.
   *
   * @return Mutable reference to the stored \c can_id.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto id() noexcept -> can_id& {
    return m_id;
  }

  /**
   * @brief The message identifier.
   *
   * @return Const reference to the stored \c can_id.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto id() const noexcept -> can_id const& {
    return m_id;
  }

  /**
   * @brief The message name.
   *
   * @return Mutable reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto name() noexcept -> std::string_view& {
    return m_name;
  }

  /**
   * @brief The message name.
   *
   * @return Const reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto name() const noexcept -> std::string_view const& {
    return m_name;
  }

  /**
   * @brief The on-wire payload length in bytes.
   *
   * @return Mutable reference to the byte length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto byte_length() noexcept -> std::uint8_t& {
    return m_byte_length;
  }

  /**
   * @brief The on-wire payload length in bytes.
   *
   * @return Const reference to the byte length.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto byte_length() const noexcept -> std::uint8_t const& {
    return m_byte_length;
  }

  /**
   * @brief Mutable view of the signal entries.
   *
   * @return A span of the message's \c signal_entry values.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto signals() noexcept -> std::span<signal_entry> {
    return std::span<signal_entry>{m_signals.data(), m_signals.size()};
  }

  /**
   * @brief Read-only view of the signal entries.
   *
   * @return A span of the message's \c signal_entry values.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto signals() const noexcept -> std::span<signal_entry const> {
    return std::span<signal_entry const>{m_signals.data(), m_signals.size()};
  }

  /**
   * @brief The number of signals in the message.
   *
   * @return The signal count.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto signal_count() const noexcept -> std::size_t {
    return m_signals.size();
  }

  /**
   * @brief Finds a signal by name.
   *
   * @param name Signal name to look up.
   *
   * @return The index into \c signals() of the first signal with that name, or
   *         \c std::nullopt when none matches.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] auto find_signal(std::string_view const name
  ) const noexcept -> std::optional<std::size_t> {
    for (std::size_t i{0}; i < m_signals.size(); ++i) {
      if (m_signals[i].definition.name() == name) {
        return i;
      }
    }
    return std::nullopt;
  }
};

}  // namespace nexenne::can
