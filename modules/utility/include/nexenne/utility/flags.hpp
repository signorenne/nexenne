#pragma once

/**
 * @file
 * @brief Type-safe bitfield over a scoped enum.
 *
 * Wraps a scoped enum whose enumerators are power-of-two bit values and offers
 * \c set / \c clear / \c toggle / \c has plus bitwise operators with correct
 * return types, so there is no implicit \c int promotion and no accidental
 * mixing of unrelated enums.
 */

#include <concepts>
#include <cstddef>
#include <type_traits>

namespace nexenne::utility {

/**
 * @brief A scoped (class) enumeration.
 *
 * @tparam E Candidate type.
 */
template <typename E>
concept scoped_enum = std::is_scoped_enum_v<E>;

/**
 * @brief Type-safe bitfield over a scoped enum.
 *
 * Wraps the underlying integer of a scoped enum whose enumerators are
 * power-of-two bit values and exposes \c set / \c clear / \c toggle / \c has
 * plus bitwise operators with correct return types.
 *
 * @tparam E Scoped enum type; its enumerators are expected to be distinct bit
 *           values.
 *
 * @pre None.
 * @post A default-constructed \c flags has no bits set.
 *
 * @par Example
 * \code
 * enum class perm : std::uint8_t { read = 1 << 0, write = 1 << 1, exec = 1 << 2 };
 * auto f{nexenne::utility::flags<perm>{}};
 * f.set(perm::read).set(perm::exec);
 * auto const combined{nexenne::utility::flags{perm::read} | perm::write};
 * \endcode
 */
template <scoped_enum E>
class flags {
public:
  using enum_type = E;
  using underlying_type = std::underlying_type_t<E>;

private:
  underlying_type m_bits{0};

public:
  /**
   * @brief Constructs an empty flag set with no bits.
   *
   * @pre None.
   * @post \c none() returns \c true.
   */
  constexpr flags() noexcept = default;

  /**
   * @brief Constructs from a single enumerator, setting its bits.
   *
   * @param e Enumerator whose bits become the initial value.
   *
   * @pre None.
   * @post \c raw() equals the underlying value of \p e.
   */
  constexpr flags(enum_type const e) noexcept  // NOLINT(hicpp-explicit-conversions)
      : m_bits{static_cast<underlying_type>(e)} {}

  /**
   * @brief Sets the bits in \p e.
   *
   * @param e Enumerator whose bits to set.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post Every bit in \p e is set; previously set bits are retained.
   */
  constexpr auto set(enum_type const e) noexcept -> flags& {
    m_bits |= static_cast<underlying_type>(e);
    return *this;
  }

  /**
   * @brief Clears the bits in \p e.
   *
   * @param e Enumerator whose bits to clear.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post Every bit in \p e is cleared; other bits are retained.
   */
  constexpr auto clear(enum_type const e) noexcept -> flags& {
    m_bits &= ~static_cast<underlying_type>(e);
    return *this;
  }

  /**
   * @brief Flips the bits in \p e.
   *
   * @param e Enumerator whose bits to toggle.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post Every bit in \p e has been inverted; other bits are retained.
   */
  constexpr auto toggle(enum_type const e) noexcept -> flags& {
    m_bits ^= static_cast<underlying_type>(e);
    return *this;
  }

  /**
   * @brief Reports whether all bits in \p e are set.
   *
   * @param e Enumerator, possibly an OR-combination, to test.
   *
   * @return \c true when every bit in \p e is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto has(enum_type const e) const noexcept -> bool {
    auto const mask{static_cast<underlying_type>(e)};
    return (m_bits & mask) == mask;
  }

  /**
   * @brief Reports whether all bits in \p e are set (an explicit spelling of
   *        \c has for call sites where all-versus-any matters).
   *
   * @param e Enumerator, possibly an OR-combination, to test.
   *
   * @return \c true when every bit in \p e is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto has_all(enum_type const e) const noexcept -> bool {
    return has(e);
  }

  /**
   * @brief Reports whether any bit in \p e is set.
   *
   * @param e Enumerator, possibly an OR-combination, to test.
   *
   * @return \c true when at least one bit in \p e is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto has_any(enum_type const e) const noexcept -> bool {
    return (m_bits & static_cast<underlying_type>(e)) != underlying_type{0};
  }

  /**
   * @brief Reports whether any bit is set.
   *
   * @return \c true when at least one bit is set.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto any() const noexcept -> bool {
    return m_bits != underlying_type{0};
  }

  /**
   * @brief Reports whether no bit is set.
   *
   * @return \c true when the flag set is empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto none() const noexcept -> bool {
    return m_bits == underlying_type{0};
  }

  /**
   * @brief Clears every bit.
   *
   * @pre None.
   * @post \c none() returns \c true.
   */
  constexpr auto clear_all() noexcept -> void {
    m_bits = underlying_type{0};
  }

  /**
   * @brief The raw underlying value, for serialisation or hardware registers.
   *
   * @return The underlying integer holding the bits.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto raw() const noexcept -> underlying_type {
    return m_bits;
  }

  /**
   * @brief Constructs a flag set from a raw underlying value.
   *
   * @param value Raw underlying value to wrap.
   *
   * @return A \c flags whose bits are \p value.
   *
   * @pre None.
   * @post \c raw() of the result equals \p value.
   */
  [[nodiscard]] static constexpr auto from_raw(underlying_type const value) noexcept -> flags {
    auto result{flags{}};
    result.m_bits = value;
    return result;
  }

  /**
   * @brief ORs \p other into this flag set.
   *
   * @param other Flag set to OR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise OR of its previous value and \p other.
   */
  constexpr auto operator|=(flags const other) noexcept -> flags& {
    m_bits |= other.m_bits;
    return *this;
  }

  /**
   * @brief ANDs \p other into this flag set.
   *
   * @param other Flag set to AND in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise AND of its previous value and \p other.
   */
  constexpr auto operator&=(flags const other) noexcept -> flags& {
    m_bits &= other.m_bits;
    return *this;
  }

  /**
   * @brief XORs \p other into this flag set.
   *
   * @param other Flag set to XOR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise XOR of its previous value and \p other.
   */
  constexpr auto operator^=(flags const other) noexcept -> flags& {
    m_bits ^= other.m_bits;
    return *this;
  }

  /**
   * @brief ORs the bits of enumerator \p e into this flag set.
   *
   * @param e Enumerator to OR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise OR of its previous value and \p e.
   */
  constexpr auto operator|=(enum_type const e) noexcept -> flags& {
    return *this |= flags{e};
  }

  /**
   * @brief ANDs the bits of enumerator \p e into this flag set.
   *
   * @param e Enumerator to AND in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise AND of its previous value and \p e.
   */
  constexpr auto operator&=(enum_type const e) noexcept -> flags& {
    return *this &= flags{e};
  }

  /**
   * @brief XORs the bits of enumerator \p e into this flag set.
   *
   * @param e Enumerator to XOR in.
   *
   * @return Reference to \c *this.
   *
   * @pre None.
   * @post This holds the bitwise XOR of its previous value and \p e.
   */
  constexpr auto operator^=(enum_type const e) noexcept -> flags& {
    return *this ^= flags{e};
  }

  /**
   * @brief Bitwise OR of two flag sets.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return A flag set holding the union of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator|(flags a, flags const b) noexcept -> flags {
    a |= b;
    return a;
  }

  /**
   * @brief Bitwise AND of two flag sets.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return A flag set holding the intersection of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator&(flags a, flags const b) noexcept -> flags {
    a &= b;
    return a;
  }

  /**
   * @brief Bitwise XOR of two flag sets.
   *
   * @param a Left operand.
   * @param b Right operand.
   *
   * @return A flag set holding the symmetric difference of \p a and \p b.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator^(flags a, flags const b) noexcept -> flags {
    a ^= b;
    return a;
  }

  /**
   * @brief Bitwise OR of a flag set and an enumerator.
   *
   * @param f Flag set operand.
   * @param e Enumerator to OR in.
   *
   * @return A flag set holding the union of \p f and \p e.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator|(flags f, enum_type const e) noexcept -> flags {
    f |= e;
    return f;
  }

  /**
   * @brief Bitwise OR of an enumerator and a flag set.
   *
   * @param e Enumerator to OR in.
   * @param f Flag set operand.
   *
   * @return A flag set holding the union of \p e and \p f.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto operator|(enum_type const e, flags f) noexcept -> flags {
    f |= e;
    return f;
  }

  /**
   * @brief Bitwise complement of this flag set.
   *
   * @return A flag set with every bit of the underlying type inverted.
   *
   * @pre None.
   * @post None.
   *
   * @warning Bits that do not correspond to a defined enumerator are also set.
   */
  [[nodiscard]] constexpr auto operator~() const noexcept -> flags {
    auto result{flags{}};
    result.m_bits = static_cast<underlying_type>(~m_bits);
    return result;
  }

  /**
   * @brief Equality of two flag sets, comparing the raw bits.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when both flag sets hold the same bits.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(flags const lhs, flags const rhs) noexcept -> bool = default;
};

}  // namespace nexenne::utility
