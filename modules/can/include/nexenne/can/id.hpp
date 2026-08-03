#pragma once

/**
 * @file
 * @brief The CAN identifier with its standard or extended id and frame flags.
 *
 * A \c can_id stores a single 32-bit word whose bit layout is the one Linux
 * SocketCAN uses for the \c can_id field of \c struct can_frame: the low 29 bits
 * hold the identifier and the top three bits are the extended-frame, remote, and
 * error flags. Mirroring that layout means a later SocketCAN backend converts a
 * \c frame to and from the kernel struct by copying this word, not by re-packing
 * bits. The constants are plain values, so this header has no platform
 * dependency and the same layout is used on every target.
 */

#include <bit>
#include <cassert>
#include <cstdint>

namespace nexenne::can {

/**
 * @brief Mask selecting the 11-bit standard identifier (bits 0 through 10).
 */
inline constexpr std::uint32_t standard_id_mask{0x0000'07FFU};

/**
 * @brief Mask selecting the 29-bit extended identifier (bits 0 through 28).
 */
inline constexpr std::uint32_t extended_id_mask{0x1FFF'FFFFU};

/**
 * @brief Flag bit marking an extended (29-bit) identifier (SocketCAN \c CAN_EFF_FLAG).
 */
inline constexpr std::uint32_t extended_flag{0x8000'0000U};

/**
 * @brief Flag bit marking a remote transmission request (SocketCAN \c CAN_RTR_FLAG).
 */
inline constexpr std::uint32_t remote_flag{0x4000'0000U};

/**
 * @brief Flag bit marking an error frame (SocketCAN \c CAN_ERR_FLAG).
 */
inline constexpr std::uint32_t error_flag{0x2000'0000U};

/**
 * @brief A CAN identifier with its standard or extended id and frame flags.
 *
 * The stored word packs the identifier in its low bits and the
 * extended-frame, remote, and error markers in its top bits, matching the
 * SocketCAN \c can_id layout. Construct through \c standard or \c extended; both
 * mask the identifier to its width as a defensive measure and assert in debug
 * when the caller passes a value that does not fit.
 */
class can_id {
public:
  using value_type = std::uint32_t;

  /**
   * @brief A settable reference to one masked flag bit of a \c can_id.
   *
   * The standard library offers this only as \c std::bitset::reference, which
   * binds a bit of a \c std::bitset rather than a bit of a plain integer lvalue.
   * This proxy gives the same read-as-bool, assign-to-set behaviour over the raw
   * identifier word, so a mutable flag accessor reads and writes through one call
   * and can appear on either side of an assignment (for example
   * \c id.extended() \c = \c true).
   */
  class flag_reference {
  public:
    /**
     * @brief Binds the reference to a single masked bit of \p word.
     *
     * @param word Raw identifier word the flag lives in.
     * @param mask Single-bit mask selecting the flag.
     *
     * @pre \p mask has exactly one bit set.
     * @post The reference reads and writes the masked bit of \p word.
     */
    constexpr flag_reference(value_type& word, value_type const mask) noexcept
        : m_word{&word}, m_mask{mask} {
      assert(std::has_single_bit(mask) && "flag_reference: mask must be a single bit");
    }

    /**
     * @brief Reads the flag.
     *
     * @return \c true when the masked bit is set.
     *
     * @pre None.
     * @post None.
     */
    [[nodiscard]] constexpr operator bool() const noexcept {
      return (*m_word & m_mask) != value_type{0};
    }

    /**
     * @brief Sets or clears the flag.
     *
     * @param on New flag value.
     *
     * @return Reference to \c *this.
     *
     * @pre None.
     * @post The masked bit equals \p on.
     */
    constexpr auto operator=(bool const on) noexcept -> flag_reference& {
      *m_word = on ? (*m_word | m_mask) : (*m_word & ~m_mask);
      return *this;
    }

    /**
     * @brief Assigns from another reference by its current flag value.
     *
     * @param other Reference whose value to copy.
     *
     * @return Reference to \c *this.
     *
     * @pre None.
     * @post This flag equals the value of \p other.
     */
    constexpr auto operator=(flag_reference const& other) noexcept -> flag_reference& {
      return *this = static_cast<bool>(other);
    }

  private:
    value_type* m_word{nullptr};
    value_type m_mask{0};
  };

private:
  value_type m_raw{0};

  explicit constexpr can_id(value_type const raw) noexcept : m_raw{raw} {}

public:
  /**
   * @brief Constructs the zero standard identifier.
   *
   * @pre None.
   * @post \c raw() is zero; \c extended(), \c remote(), and \c error_frame()
   *       are all \c false.
   */
  constexpr can_id() noexcept = default;

  /**
   * @brief Builds a standard (11-bit) identifier.
   *
   * @param id Identifier value in the range \c [0, 0x7FF].
   *
   * @return The standard identifier carrying \p id.
   *
   * @pre \p id is at most \c 0x7FF; a larger value asserts in debug and is
   *      masked to 11 bits in release.
   * @post \c extended() is \c false and \c identifier() equals \p id.
   */
  [[nodiscard]] static constexpr auto standard(std::uint16_t const id) noexcept -> can_id {
    assert((id & ~standard_id_mask) == 0U && "can_id::standard: id exceeds 11 bits");
    return can_id{static_cast<value_type>(id) & standard_id_mask};
  }

  /**
   * @brief Builds an extended (29-bit) identifier.
   *
   * @param id Identifier value in the range \c [0, 0x1FFFFFFF].
   *
   * @return The extended identifier carrying \p id.
   *
   * @pre \p id is at most \c 0x1FFFFFFF; a larger value asserts in debug and is
   *      masked to 29 bits in release.
   * @post \c extended() is \c true and \c identifier() equals \p id.
   */
  [[nodiscard]] static constexpr auto extended(std::uint32_t const id) noexcept -> can_id {
    assert((id & ~extended_id_mask) == 0U && "can_id::extended: id exceeds 29 bits");
    return can_id{(id & extended_id_mask) | extended_flag};
  }

  /**
   * @brief Wraps a raw SocketCAN-layout identifier word verbatim.
   *
   * @param raw Raw word with the identifier in its low bits and the flag bits
   *            in its top bits.
   *
   * @return A \c can_id holding \p raw unchanged.
   *
   * @pre None.
   * @post \c raw() equals \p raw.
   */
  [[nodiscard]] static constexpr auto from_raw(value_type const raw) noexcept -> can_id {
    return can_id{raw};
  }

  /**
   * @brief The raw SocketCAN-layout word, identifier and flag bits together.
   *
   * @return Mutable reference to the stored 32-bit word.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto raw() & noexcept -> value_type& {
    return m_raw;
  }

  /**
   * @brief The raw SocketCAN-layout word, identifier and flag bits together.
   *
   * @return Const reference to the stored 32-bit word.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto raw() const& noexcept -> value_type const& {
    return m_raw;
  }

  /**
   * @brief The identifier value, masked to its width.
   *
   * @return The low 29 bits for an extended id, or the low 11 bits otherwise.
   *
   * @pre None.
   * @post The result is at most \c 0x1FFFFFFF for an extended id, or \c 0x7FF
   *       for a standard id.
   */
  [[nodiscard]] constexpr auto identifier() const noexcept -> value_type {
    return extended() ? (m_raw & extended_id_mask) : (m_raw & standard_id_mask);
  }

  /**
   * @brief Whether this is an extended (29-bit) identifier.
   *
   * @return Settable reference to the extended-frame flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto extended() & noexcept -> flag_reference {
    return flag_reference{m_raw, extended_flag};
  }

  /**
   * @brief Whether this is an extended (29-bit) identifier.
   *
   * @return \c true when the extended-frame flag is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto extended() const& noexcept -> bool {
    return (m_raw & extended_flag) != 0U;
  }

  /**
   * @brief Whether this identifier marks a remote transmission request.
   *
   * @return Settable reference to the remote flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto remote() & noexcept -> flag_reference {
    return flag_reference{m_raw, remote_flag};
  }

  /**
   * @brief Whether this identifier marks a remote transmission request.
   *
   * @return \c true when the remote flag is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto remote() const& noexcept -> bool {
    return (m_raw & remote_flag) != 0U;
  }

  /**
   * @brief Whether this identifier marks an error frame.
   *
   * @return Settable reference to the error flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto error_frame() & noexcept -> flag_reference {
    return flag_reference{m_raw, error_flag};
  }

  /**
   * @brief Whether this identifier marks an error frame.
   *
   * @return \c true when the error flag is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto error_frame() const& noexcept -> bool {
    return (m_raw & error_flag) != 0U;
  }

  /**
   * @brief Equality over the raw identifier word, flags included.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both identifiers hold the same raw word.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator==(can_id const& lhs, can_id const& rhs) noexcept
    -> bool = default;
};

}  // namespace nexenne::can
