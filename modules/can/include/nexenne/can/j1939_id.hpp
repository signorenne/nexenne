#pragma once

/**
 * @file
 * @brief The SAE J1939 view of a 29-bit CAN identifier.
 *
 * J1939 is the heavy-vehicle protocol layered on extended CAN. It gives the
 * 29-bit identifier a fixed structure: a 3-bit priority, two page bits, an 8-bit
 * PDU format, an 8-bit PDU specific field, and an 8-bit source address. From
 * those it derives the Parameter Group Number (PGN) that names the message and,
 * for destination-specific messages, the destination address. This header wraps a
 * \c can_id and exposes those fields and the PGN, plus a builder that assembles an
 * identifier from a priority, PGN, and addresses. Transport protocol (multi-packet
 * messages) and address claiming are later layers; this is the identifier base.
 *
 * Reference: SAE J1939-21, the data link layer, identifier and PGN structure.
 */

#include <cassert>
#include <cstdint>
#include <optional>

#include <nexenne/can/id.hpp>

namespace nexenne::can {

/**
 * @brief The PDU format value at and above which a message is PDU2 (broadcast).
 *
 * A PDU format below this is PDU1 (destination-specific); at or above it the PDU
 * specific byte is a group extension rather than a destination address.
 */
inline constexpr std::uint8_t j1939_pdu2_threshold{240};

/**
 * @brief The J1939 global (broadcast) destination address.
 */
inline constexpr std::uint8_t j1939_global_address{0xFF};

/**
 * @brief A J1939 interpretation of a 29-bit extended CAN identifier.
 *
 * Wraps a \c can_id and decodes the J1939 fields from it. Build one by decoding
 * an extended identifier or by assembling components with \c make.
 */
class j1939_id {
public:
  using value_type = std::uint32_t;

private:
  can_id m_id{};

  explicit constexpr j1939_id(can_id const id) noexcept : m_id{id} {}

public:
  /**
   * @brief Decodes a J1939 identifier from an extended CAN identifier.
   *
   * @param id Identifier to interpret; J1939 uses only extended identifiers.
   *
   * @return The J1939 view, or \c std::nullopt when \p id is not extended.
   *
   * @pre None.
   * @post On success \c identifier() equals \p id.
   */
  [[nodiscard]] static constexpr auto decode(can_id const id) noexcept -> std::optional<j1939_id> {
    if (!id.extended()) {
      return std::nullopt;
    }
    return j1939_id{id};
  }

  /**
   * @brief Assembles a J1939 identifier from a priority, PGN, and addresses.
   *
   * For a PDU1 (destination-specific) PGN the PDU specific byte is set to
   * \p destination; for a PDU2 (broadcast) PGN it is the group extension carried
   * in the PGN and \p destination is ignored.
   *
   * @param priority Message priority, 0 (highest) through 7.
   * @param pgn Parameter Group Number, an 18-bit value.
   * @param source Source address of the sending node.
   * @param destination Destination address for a PDU1 PGN; defaults to the global
   *                    address.
   *
   * @return The assembled J1939 identifier.
   *
   * @pre \p priority is at most 7 and \p pgn fits in 18 bits.
   * @post \c priority(), \c pgn(), and \c source_address() reflect the arguments.
   */
  [[nodiscard]] static constexpr auto make(
    std::uint8_t const priority,
    std::uint32_t const pgn,
    std::uint8_t const source,
    std::uint8_t const destination = j1939_global_address
  ) noexcept -> j1939_id {
    assert(priority <= 7U && "j1939_id::make: priority exceeds 3 bits");
    assert(pgn <= 0x3FFFFU && "j1939_id::make: pgn exceeds 18 bits");
    auto const edp{(pgn >> 17) & 0x1U};
    auto const dp{(pgn >> 16) & 0x1U};
    auto const pf{(pgn >> 8) & 0xFFU};
    auto const ps{
      pf < j1939_pdu2_threshold ? static_cast<std::uint32_t>(destination) : (pgn & 0xFFU)
    };
    auto const raw{
      ((priority & 0x7U) << 26U) | (edp << 25U) | (dp << 24U) | (pf << 16U) | (ps << 8U) | source
    };
    return j1939_id{can_id::extended(raw)};
  }

  /**
   * @brief The underlying CAN identifier.
   *
   * @return The wrapped \c can_id.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto identifier() const noexcept -> can_id {
    return m_id;
  }

  /**
   * @brief The 29-bit identifier value.
   *
   * @return The extended identifier as a 29-bit number.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto raw() const noexcept -> value_type {
    return m_id.identifier();
  }

  /**
   * @brief The message priority.
   *
   * @return The 3-bit priority, 0 (highest) through 7.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto priority() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((raw() >> 26U) & 0x7U);
  }

  /**
   * @brief The extended data page bit.
   *
   * @return The EDP bit, 0 or 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto extended_data_page() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((raw() >> 25U) & 0x1U);
  }

  /**
   * @brief The data page bit.
   *
   * @return The DP bit, 0 or 1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto data_page() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((raw() >> 24U) & 0x1U);
  }

  /**
   * @brief The PDU format byte.
   *
   * @return The 8-bit PDU format (PF).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto pdu_format() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((raw() >> 16U) & 0xFFU);
  }

  /**
   * @brief The PDU specific byte.
   *
   * @return The 8-bit PDU specific field (PS): a destination address for PDU1 or
   *         a group extension for PDU2.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto pdu_specific() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>((raw() >> 8U) & 0xFFU);
  }

  /**
   * @brief The source address of the sending node.
   *
   * @return The 8-bit source address (SA).
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto source_address() const noexcept -> std::uint8_t {
    return static_cast<std::uint8_t>(raw() & 0xFFU);
  }

  /**
   * @brief Reports whether this is a PDU1 (destination-specific) identifier.
   *
   * @return \c true when the PDU format is below the PDU2 threshold.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_pdu1() const noexcept -> bool {
    return pdu_format() < j1939_pdu2_threshold;
  }

  /**
   * @brief Reports whether this is a PDU2 (broadcast) identifier.
   *
   * @return \c true when the PDU format is at or above the PDU2 threshold.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_pdu2() const noexcept -> bool {
    return !is_pdu1();
  }

  /**
   * @brief The Parameter Group Number naming the message.
   *
   * @return The 18-bit PGN: the page bits and PDU format, plus the PDU specific
   *         byte for PDU2, or zero in that byte for PDU1.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto pgn() const noexcept -> value_type {
    auto const high{
      (static_cast<value_type>(extended_data_page()) << 17U)
      | (static_cast<value_type>(data_page()) << 16U)
      | (static_cast<value_type>(pdu_format()) << 8U)
    };
    return is_pdu1() ? high : (high | pdu_specific());
  }

  /**
   * @brief The destination address of the message.
   *
   * @return The PDU specific byte for a PDU1 message, or the global address for a
   *         PDU2 (broadcast) message.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto destination_address() const noexcept -> std::uint8_t {
    return is_pdu1() ? pdu_specific() : j1939_global_address;
  }

  /**
   * @brief Reports whether the message is broadcast to every node.
   *
   * @return \c true for a PDU2 message, or a PDU1 message addressed to the global
   *         address.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_broadcast() const noexcept -> bool {
    return is_pdu2() || destination_address() == j1939_global_address;
  }

  /**
   * @brief Equality over the underlying identifier.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both wrap the same identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(j1939_id const lhs, j1939_id const rhs) noexcept -> bool = default;
};

}  // namespace nexenne::can
