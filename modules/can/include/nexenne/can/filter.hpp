#pragma once

/**
 * @file
 * @brief An identifier filter: an id and mask that accept a set of CAN identifiers.
 *
 * A filter matches an identifier when their masked bits agree:
 * \c (candidate & mask) == (id & mask). A mask of all ones matches one exact
 * identifier; a looser mask matches a range, which is how a node subscribes to a
 * family of messages (for example every J1939 message from one source address).
 * The same type is used for software filtering in the registry and, later, for
 * the kernel hardware filter array in the SocketCAN backend, so one definition
 * serves both. The match runs over the raw SocketCAN-layout word, so the mask can
 * include the extended-frame, remote, and error flag bits.
 */

#include <cassert>
#include <cstdint>

#include <nexenne/can/id.hpp>

namespace nexenne::can {

/**
 * @brief An identifier and mask that accept identifiers agreeing on the masked bits.
 */
class filter {
public:
  using value_type = std::uint32_t;

private:
  value_type m_id{0};
  value_type m_mask{0};

public:
  /**
   * @brief Constructs a match-everything filter with a zero mask.
   *
   * @pre None.
   * @post \c id() and \c mask() are zero, so \c matches accepts every identifier.
   */
  constexpr filter() noexcept = default;

  /**
   * @brief Constructs a filter from a raw id word and mask.
   *
   * @param id Raw identifier word the accepted identifiers must match.
   * @param mask Bits that must agree; bits set to zero are ignored.
   *
   * @pre None.
   * @post \c id() and \c mask() equal the arguments.
   */
  constexpr filter(value_type const id, value_type const mask) noexcept : m_id{id}, m_mask{mask} {}

  /**
   * @brief Builds a filter that accepts exactly one identifier and format.
   *
   * @param id Identifier to accept.
   *
   * @return A filter matching \p id, its 11-bit or 29-bit value, and its
   *         extended-frame flag, while ignoring the remote and error flags.
   *
   * @pre None.
   * @post \c matches(id) is \c true.
   */
  [[nodiscard]] static constexpr auto equals(can_id const id) noexcept -> filter {
    auto const width{id.extended() ? extended_id_mask : standard_id_mask};
    return filter{id.raw(), extended_flag | width};
  }

  /**
   * @brief Builds a filter over standard identifiers.
   *
   * @param id Standard identifier value to match against.
   * @param mask Identifier bits that must agree; defaults to all 11 bits.
   *
   * @return A filter that accepts standard frames whose masked identifier equals
   *         \p id.
   *
   * @pre \p id and \p mask fit in 11 bits.
   * @post Matching requires the extended-frame flag to be clear.
   */
  [[nodiscard]] static constexpr auto
  standard(std::uint16_t const id, std::uint16_t const mask = standard_id_mask) noexcept -> filter {
    assert((id & ~standard_id_mask) == 0U && "filter::standard: id exceeds 11 bits");
    assert((mask & ~standard_id_mask) == 0U && "filter::standard: mask exceeds 11 bits");
    return filter{static_cast<value_type>(id), static_cast<value_type>(mask) | extended_flag};
  }

  /**
   * @brief Builds a filter over extended identifiers.
   *
   * @param id Extended identifier value to match against.
   * @param mask Identifier bits that must agree; defaults to all 29 bits.
   *
   * @return A filter that accepts extended frames whose masked identifier equals
   *         \p id.
   *
   * @pre \p id and \p mask fit in 29 bits.
   * @post Matching requires the extended-frame flag to be set.
   */
  [[nodiscard]] static constexpr auto
  extended(std::uint32_t const id, std::uint32_t const mask = extended_id_mask) noexcept -> filter {
    assert((id & ~extended_id_mask) == 0U && "filter::extended: id exceeds 29 bits");
    assert((mask & ~extended_id_mask) == 0U && "filter::extended: mask exceeds 29 bits");
    return filter{id | extended_flag, mask | extended_flag};
  }

  /**
   * @brief Reports whether \p candidate passes the filter.
   *
   * @param candidate Identifier to test.
   *
   * @return \c true when the masked bits of \p candidate equal those of the
   *         filter's identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto matches(can_id const candidate) const noexcept -> bool {
    return (candidate.raw() & m_mask) == (m_id & m_mask);
  }

  /**
   * @brief The raw identifier word of the filter.
   *
   * @return Mutable reference to the stored identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto id() noexcept -> value_type& {
    return m_id;
  }

  /**
   * @brief The raw identifier word of the filter.
   *
   * @return Const reference to the stored identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto id() const noexcept -> value_type const& {
    return m_id;
  }

  /**
   * @brief The mask of the filter.
   *
   * @return Mutable reference to the stored mask.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mask() noexcept -> value_type& {
    return m_mask;
  }

  /**
   * @brief The mask of the filter.
   *
   * @return Const reference to the stored mask.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mask() const noexcept -> value_type const& {
    return m_mask;
  }

  /**
   * @brief Equality over the identifier and mask.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both filters hold the same id and mask.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(filter const lhs, filter const rhs) noexcept -> bool = default;
};

}  // namespace nexenne::can
