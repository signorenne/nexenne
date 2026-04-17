#pragma once

/**
 * @file
 * @brief A fluent builder for a CAN signal.
 *
 * A \c signal.hpp has many fields, and a constructor call that lists them
 * positionally is hard to read. The builder gives each one its own named step,
 * one setter per field, so every property is set explicitly and the call reads
 * top to bottom. The signal itself stays a plain value type with constructors and
 * accessors; this builder is the ergonomic path, not the only one.
 */

#include <cassert>
#include <cstdint>
#include <string_view>

#include <nexenne/can/byte_order.hpp>
#include <nexenne/can/signal.hpp>

namespace nexenne::can {

/**
 * @brief Assembles a \c signal one named field at a time.
 *
 * Every setter sets exactly one field and returns a reference to the builder, so
 * calls chain; \c build returns a copy of the signal under construction.
 */
class signal_builder {
public:
  using value_type = signal;

private:
  signal m_signal{};

public:
  /**
   * @brief Constructs a builder holding a default signal.
   *
   * @pre None.
   * @post The pending signal is the default single-bit unsigned little-endian one.
   */
  constexpr signal_builder() noexcept = default;

  /**
   * @brief Sets the signal name.
   *
   * @param name Non-owning view of the name; it must outlive the built signal.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's name equals \p name.
   */
  constexpr auto name(std::string_view const name) noexcept -> signal_builder& {
    m_signal.name() = name;
    return *this;
  }

  /**
   * @brief Sets the start bit in DBC numbering.
   *
   * @param start_bit Start bit: the least significant bit for little-endian, the
   *                  most significant bit for big-endian.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's start bit equals \p start_bit.
   */
  constexpr auto start_bit(std::uint16_t const start_bit) noexcept -> signal_builder& {
    m_signal.start_bit() = start_bit;
    return *this;
  }

  /**
   * @brief Sets the field width in bits.
   *
   * @param length Field width in bits, 1 through 64.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre \p length is between 1 and 64.
   * @post The pending signal's length equals \p length.
   */
  constexpr auto length(std::uint16_t const length) noexcept -> signal_builder& {
    assert(length >= 1U && length <= 64U && "signal_builder: length must be between 1 and 64");
    m_signal.length() = length;
    return *this;
  }

  /**
   * @brief Sets the byte order.
   *
   * @param endianness Byte order of the field.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's byte order equals \p endianness.
   */
  constexpr auto endianness(byte_order const endianness) noexcept -> signal_builder& {
    m_signal.order() = endianness;
    return *this;
  }

  /**
   * @brief Sets whether the field is two's-complement signed.
   *
   * @param is_signed \c true for a signed field.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's signedness equals \p is_signed.
   */
  constexpr auto is_signed(bool const is_signed) noexcept -> signal_builder& {
    m_signal.is_signed() = is_signed;
    return *this;
  }

  /**
   * @brief Sets whether the raw bits are an IEEE-754 float.
   *
   * @param is_float \c true to reinterpret the raw 32 or 64 bits as a \c float or
   *                 \c double before scaling.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre The signal length is 32 or 64 when \p is_float is \c true.
   * @post The pending signal's float flag equals \p is_float.
   */
  constexpr auto is_float(bool const is_float) noexcept -> signal_builder& {
    m_signal.is_float() = is_float;
    return *this;
  }

  /**
   * @brief Sets the scale applied to the raw value.
   *
   * @param scale Scale factor applied to the raw value.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's scale equals \p scale.
   */
  constexpr auto scale(double const scale) noexcept -> signal_builder& {
    m_signal.scale() = scale;
    return *this;
  }

  /**
   * @brief Sets the offset added after scaling.
   *
   * @param offset Offset value.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's offset equals \p offset.
   */
  constexpr auto offset(double const offset) noexcept -> signal_builder& {
    m_signal.offset() = offset;
    return *this;
  }

  /**
   * @brief Sets the lowest physical value a packed signal may hold.
   *
   * @param minimum Clamp minimum.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's minimum equals \p minimum.
   */
  constexpr auto minimum(double const minimum) noexcept -> signal_builder& {
    m_signal.minimum() = minimum;
    return *this;
  }

  /**
   * @brief Sets the highest physical value a packed signal may hold.
   *
   * @param maximum Clamp maximum.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's maximum equals \p maximum.
   */
  constexpr auto maximum(double const maximum) noexcept -> signal_builder& {
    m_signal.maximum() = maximum;
    return *this;
  }

  /**
   * @brief Sets the unit string.
   *
   * @param unit Non-owning view of the unit; it must outlive the built signal.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's unit equals \p unit.
   */
  constexpr auto unit(std::string_view const unit) noexcept -> signal_builder& {
    m_signal.unit() = unit;
    return *this;
  }

  /**
   * @brief Sets the signal's multiplexing role.
   *
   * @param role Multiplexing role (none, selector, or multiplexed).
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's multiplex role equals \p role.
   */
  constexpr auto mux_role(multiplex_role const role) noexcept -> signal_builder& {
    m_signal.mux_role() = role;
    return *this;
  }

  /**
   * @brief Sets the selector value a multiplexed signal belongs to.
   *
   * @param value Selector value that activates this signal.
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's mux value equals \p value.
   */
  constexpr auto mux_value(std::uint32_t const value) noexcept -> signal_builder& {
    m_signal.mux_value() = value;
    return *this;
  }

  /**
   * @brief Sets which extreme raw value is treated as "not available".
   *
   * @param policy Invalid-value policy (none, all-ones, all-zeros, or both).
   *
   * @return Reference to \c *this for chaining.
   *
   * @pre None.
   * @post The pending signal's invalid-value policy equals \p policy.
   */
  constexpr auto invalid(invalid_value const policy) noexcept -> signal_builder& {
    m_signal.invalid() = policy;
    return *this;
  }

  /**
   * @brief Returns the assembled signal.
   *
   * @return A copy of the signal under construction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto build() const noexcept -> signal {
    return m_signal;
  }
};

}  // namespace nexenne::can
