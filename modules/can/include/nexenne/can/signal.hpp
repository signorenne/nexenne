#pragma once

/**
 * @file
 * @brief A CAN signal: a payload bit field plus the scaling to a physical value.
 *
 * A \c signal is the description an application writes once and reuses: the bit
 * position and width of a field inside a frame, its byte order and signedness,
 * and the linear scaling that turns the raw integer into a physical value
 * (\c physical = \c raw * \c scale + \c offset). It carries no payload and does
 * no bit work itself; the compiled form that packs and unpacks bits is
 * \c packing_plan.hpp, the functions that apply a signal to a frame are in
 * \c codec.hpp, and a fluent way to build one is \c signal_builder.hpp.
 *
 * The \c name and \c unit are non-owning views. The caller keeps the referenced
 * strings alive for as long as the signal is used; string literals and a
 * database's own string storage both satisfy this, and it keeps \c signal
 * trivially copyable and allocation-free.
 */

#include <cassert>
#include <cstdint>
#include <limits>
#include <string_view>
#include <type_traits>

#include <nexenne/can/byte_order.hpp>

namespace nexenne::can {

/**
 * @brief A signal's role in message multiplexing.
 *
 * A multiplexed message has one \c selector signal whose value chooses which
 * group of \c multiplexed signals is present in a given frame; all other signals
 * are \c none and always present.
 */
enum class multiplex_role : std::uint8_t {
  none,         ///< Always present; not part of multiplexing.
  selector,     ///< The multiplexor; its value selects the active group.
  multiplexed,  ///< Present only when the selector equals this signal's mux value.
};

/**
 * @brief Which extreme raw value of a signal means "not available".
 *
 * Protocols reserve an extreme raw value to mean "no data". SAE J1939 reserves a
 * parameter's all-ones value as "not available" (and the next value down as an
 * error indicator); all-zeros is normally a real value, but some designs treat it
 * as empty too. This policy lets decoding recognise the all-ones value, the
 * all-zeros value, or both as not-available, so a registry skips them instead of
 * reporting a meaningless number.
 */
enum class invalid_value : std::uint8_t {
  none,               ///< Every raw value is a real value.
  all_ones,           ///< The all-ones raw value means "not available" (J1939).
  all_zeros,          ///< The all-zeros raw value means "not available".
  all_ones_or_zeros,  ///< Either extreme means "not available".
};

/**
 * @brief Describes one named field inside a CAN frame and its linear scaling.
 *
 * Construct directly through the constructor and the field accessors, or build
 * one fluently with a \c signal_builder.hpp. Every field has a mutable and
 * a const accessor. The default clamp range is unbounded, so no clamping happens
 * unless a range is set.
 */
class signal {
public:
  using value_type = double;

private:
  std::uint16_t m_start_bit{0};
  std::uint16_t m_length{1};
  byte_order m_order{byte_order::little_endian};
  bool m_is_signed{false};
  bool m_is_float{false};
  double m_scale{1.0};
  double m_offset{0.0};
  double m_minimum{-std::numeric_limits<double>::infinity()};
  double m_maximum{std::numeric_limits<double>::infinity()};
  std::string_view m_name{};
  std::string_view m_unit{};
  multiplex_role m_mux_role{multiplex_role::none};
  std::uint32_t m_mux_value{0};
  invalid_value m_invalid{invalid_value::none};

public:
  /**
   * @brief Constructs an unnamed unsigned single-bit little-endian signal.
   *
   * @pre None.
   * @post The bit length is one, the scaling is identity, and the clamp range is
   *       unbounded.
   */
  constexpr signal() noexcept = default;

  /**
   * @brief Constructs a signal from its bit layout and linear scaling.
   *
   * @param start_bit Bit position of the field in DBC numbering: for little-endian
   *                  the least significant bit, for big-endian the most significant.
   * @param length Field width in bits, 1 through 64.
   * @param order Byte order of the field.
   * @param is_signed Whether the raw value is two's-complement signed.
   * @param scale Scale applied to the raw value.
   * @param offset Offset added after scaling.
   *
   * @pre \p length is between 1 and 64.
   * @post The clamp range is unbounded and the name and unit are empty.
   */
  constexpr signal(
    std::uint16_t const start_bit,
    std::uint16_t const length,
    byte_order const order,
    bool const is_signed,
    double const scale = 1.0,
    double const offset = 0.0
  ) noexcept
      : m_start_bit{start_bit}
      , m_length{length}
      , m_order{order}
      , m_is_signed{is_signed}
      , m_scale{scale}
      , m_offset{offset} {
    assert(length >= 1U && length <= 64U && "signal: length must be between 1 and 64");
  }

  /**
   * @brief The start bit in DBC numbering.
   *
   * @return Mutable reference to the start bit.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto start_bit() noexcept -> std::uint16_t& {
    return m_start_bit;
  }

  /**
   * @brief The start bit in DBC numbering.
   *
   * @return Const reference to the start bit.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto start_bit() const noexcept -> std::uint16_t const& {
    return m_start_bit;
  }

  /**
   * @brief The field width in bits.
   *
   * @return Mutable reference to the bit length.
   *
   * @pre A length written through this reference is between 1 and 64.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() noexcept -> std::uint16_t& {
    return m_length;
  }

  /**
   * @brief The field width in bits.
   *
   * @return Const reference to the bit length, 1 through 64.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto length() const noexcept -> std::uint16_t const& {
    return m_length;
  }

  /**
   * @brief The byte order of the field.
   *
   * @return Mutable reference to the byte order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto order() noexcept -> byte_order& {
    return m_order;
  }

  /**
   * @brief The byte order of the field.
   *
   * @return Const reference to the byte order.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto order() const noexcept -> byte_order const& {
    return m_order;
  }

  /**
   * @brief Whether the raw value is two's-complement signed.
   *
   * @return Mutable reference to the signedness flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_signed() noexcept -> bool& {
    return m_is_signed;
  }

  /**
   * @brief Whether the raw value is two's-complement signed.
   *
   * @return Const reference to the signedness flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_signed() const noexcept -> bool const& {
    return m_is_signed;
  }

  /**
   * @brief Whether the raw bits are an IEEE-754 float rather than an integer.
   *
   * A float signal reinterprets its raw 32 or 64 bits as a \c float or \c double
   * (DBC \c SIG_VALTYPE_, cantools \c is_float) before applying the scaling,
   * instead of reading them as a two's-complement integer.
   *
   * @return Mutable reference to the float flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_float() noexcept -> bool& {
    return m_is_float;
  }

  /**
   * @brief Whether the raw bits are an IEEE-754 float rather than an integer.
   *
   * @return Const reference to the float flag.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto is_float() const noexcept -> bool const& {
    return m_is_float;
  }

  /**
   * @brief The scale applied to the raw value.
   *
   * @return Mutable reference to the scale.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto scale() noexcept -> double& {
    return m_scale;
  }

  /**
   * @brief The scale applied to the raw value.
   *
   * @return Const reference to the scale.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto scale() const noexcept -> double const& {
    return m_scale;
  }

  /**
   * @brief The offset added after scaling.
   *
   * @return Mutable reference to the offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto offset() noexcept -> double& {
    return m_offset;
  }

  /**
   * @brief The offset added after scaling.
   *
   * @return Const reference to the offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto offset() const noexcept -> double const& {
    return m_offset;
  }

  /**
   * @brief The lowest physical value a packed signal may hold.
   *
   * @return Mutable reference to the minimum.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto minimum() noexcept -> double& {
    return m_minimum;
  }

  /**
   * @brief The lowest physical value a packed signal may hold.
   *
   * @return Const reference to the minimum, negative infinity when unbounded.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto minimum() const noexcept -> double const& {
    return m_minimum;
  }

  /**
   * @brief The highest physical value a packed signal may hold.
   *
   * @return Mutable reference to the maximum.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto maximum() noexcept -> double& {
    return m_maximum;
  }

  /**
   * @brief The highest physical value a packed signal may hold.
   *
   * @return Const reference to the maximum, positive infinity when unbounded.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto maximum() const noexcept -> double const& {
    return m_maximum;
  }

  /**
   * @brief The signal name.
   *
   * @return Mutable reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() noexcept -> std::string_view& {
    return m_name;
  }

  /**
   * @brief The signal name, or an empty view when unset.
   *
   * @return Const reference to the name view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() const noexcept -> std::string_view const& {
    return m_name;
  }

  /**
   * @brief The unit string.
   *
   * @return Mutable reference to the unit view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto unit() noexcept -> std::string_view& {
    return m_unit;
  }

  /**
   * @brief The unit string, or an empty view when unset.
   *
   * @return Const reference to the unit view.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto unit() const noexcept -> std::string_view const& {
    return m_unit;
  }

  /**
   * @brief The signal's multiplexing role.
   *
   * @return Mutable reference to the multiplex role.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mux_role() noexcept -> multiplex_role& {
    return m_mux_role;
  }

  /**
   * @brief The signal's multiplexing role.
   *
   * @return Const reference to the multiplex role.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mux_role() const noexcept -> multiplex_role const& {
    return m_mux_role;
  }

  /**
   * @brief The selector value this multiplexed signal belongs to.
   *
   * @return Mutable reference to the mux value; meaningful when the role is
   *         \c multiplex_role::multiplexed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mux_value() noexcept -> std::uint32_t& {
    return m_mux_value;
  }

  /**
   * @brief The selector value this multiplexed signal belongs to.
   *
   * @return Const reference to the mux value; meaningful when the role is
   *         \c multiplex_role::multiplexed.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mux_value() const noexcept -> std::uint32_t const& {
    return m_mux_value;
  }

  /**
   * @brief The policy for which extreme raw value means "not available".
   *
   * @return Mutable reference to the invalid-value policy.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto invalid() noexcept -> invalid_value& {
    return m_invalid;
  }

  /**
   * @brief The policy for which extreme raw value means "not available".
   *
   * @return Const reference to the invalid-value policy.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto invalid() const noexcept -> invalid_value const& {
    return m_invalid;
  }
};

static_assert(
  std::is_trivially_copyable_v<signal>,
  "signal must stay trivially copyable: it holds only scalars and string views"
);

}  // namespace nexenne::can
