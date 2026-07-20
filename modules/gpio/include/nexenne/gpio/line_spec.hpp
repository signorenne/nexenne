#pragma once

/**
 * @file
 * @brief The static, value-typed description of one GPIO line.
 *
 * A \c line_spec answers "which chip, which offset, which direction, which
 * polarity, which electrical setup"; it is the description an application
 * writes once and reuses. It carries no runtime state (no level, no
 * timestamp) and is trivially copyable, so a table of specs can live in a
 * constexpr array on Linux or a static array on a bare-metal target. What a
 * line is asked to do at request time (edge subscription, debounce, initial
 * value) is the separate \c line_config.hpp, and what happens on the line
 * travels as \c line_event.hpp and \c line_value.hpp.
 *
 * The \c name is a non-owning view. The caller keeps the referenced string
 * alive for as long as the spec is used; string literals and a configuration
 * object's own storage both satisfy this, and it keeps \c line_spec
 * trivially copyable and allocation-free.
 */

#include <string_view>

#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief Describes one GPIO line: its address, direction, and electrical setup.
 *
 * Construct directly through the constructor and the field accessors, or use
 * the \c input and \c output factories that name the common cases. Every
 * field has a mutable and a const accessor.
 */
class line_spec {
public:
  using value_type = bool;

private:
  std::string_view m_name{};
  chip_id m_chip{0};
  line_offset m_offset{0};
  line_direction m_direction{line_direction::input};
  line_polarity m_polarity{line_polarity::active_high};
  line_bias m_bias{line_bias::as_is};
  line_drive m_drive{line_drive::push_pull};

public:
  /**
   * @brief Constructs an unnamed active-high input on chip zero, offset zero.
   *
   * @pre None.
   * @post The bias is untouched and the drive topology is push-pull.
   */
  constexpr line_spec() noexcept = default;

  /**
   * @brief Constructs a fully-specified line description.
   *
   * @param name Non-owning view of the logical name; it must outlive the spec.
   * @param chip Identifier of the owning GPIO chip.
   * @param offset Zero-based line offset within \p chip.
   * @param direction Line direction.
   * @param polarity Physical-to-logical level mapping.
   * @param bias Requested on-chip bias network.
   * @param drive Output driver topology.
   *
   * @pre None.
   * @post Every accessor returns the corresponding argument.
   */
  constexpr line_spec(
    std::string_view const name,
    chip_id const chip,
    line_offset const offset,
    line_direction const direction,
    line_polarity const polarity = line_polarity::active_high,
    line_bias const bias = line_bias::as_is,
    line_drive const drive = line_drive::push_pull
  ) noexcept
    : m_name{name},
      m_chip{chip},
      m_offset{offset},
      m_direction{direction},
      m_polarity{polarity},
      m_bias{bias},
      m_drive{drive} {}

  /**
   * @brief Builds a spec for an input line.
   *
   * Fixes the direction to \c line_direction::input and the drive topology to
   * push-pull (a driver topology is an output concern); polarity and bias are
   * selectable.
   *
   * @param name Non-owning view of the logical name; it must outlive the spec.
   * @param chip Identifier of the owning GPIO chip.
   * @param offset Zero-based line offset within \p chip.
   * @param polarity Physical-to-logical level mapping.
   * @param bias Requested on-chip bias network.
   *
   * @return A spec whose \c direction() is \c line_direction::input.
   *
   * @pre None.
   * @post The result's \c drive() is \c line_drive::push_pull.
   */
  [[nodiscard]] static constexpr auto input(
    std::string_view const name,
    chip_id const chip,
    line_offset const offset,
    line_polarity const polarity = line_polarity::active_high,
    line_bias const bias = line_bias::as_is
  ) noexcept -> line_spec {
    return line_spec{name, chip, offset, line_direction::input, polarity, bias};
  }

  /**
   * @brief Builds a spec for an output line.
   *
   * Fixes the direction to \c line_direction::output and the bias to
   * \c line_bias::as_is (a bias network is an input concern); polarity and
   * drive topology are selectable.
   *
   * @param name Non-owning view of the logical name; it must outlive the spec.
   * @param chip Identifier of the owning GPIO chip.
   * @param offset Zero-based line offset within \p chip.
   * @param polarity Physical-to-logical level mapping.
   * @param drive Output driver topology.
   *
   * @return A spec whose \c direction() is \c line_direction::output.
   *
   * @pre None.
   * @post The result's \c bias() is \c line_bias::as_is.
   */
  [[nodiscard]] static constexpr auto output(
    std::string_view const name,
    chip_id const chip,
    line_offset const offset,
    line_polarity const polarity = line_polarity::active_high,
    line_drive const drive = line_drive::push_pull
  ) noexcept -> line_spec {
    return line_spec{
      name, chip, offset, line_direction::output, polarity, line_bias::as_is, drive
    };
  }

  /**
   * @brief The logical name of the line.
   *
   * @return The stored name view; may be empty.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() const noexcept -> std::string_view {
    return m_name;
  }

  /**
   * @brief Mutable access to the logical name.
   *
   * @return Reference to the stored name view.
   *
   * @pre The storage backing an assigned view outlives the spec.
   * @post None.
   */
  [[nodiscard]] constexpr auto name() noexcept -> std::string_view& {
    return m_name;
  }

  /**
   * @brief The identifier of the owning GPIO chip.
   *
   * @return The stored chip identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto chip() const noexcept -> chip_id {
    return m_chip;
  }

  /**
   * @brief Mutable access to the owning chip identifier.
   *
   * @return Reference to the stored chip identifier.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto chip() noexcept -> chip_id& {
    return m_chip;
  }

  /**
   * @brief The zero-based line offset within the chip.
   *
   * @return The stored offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto offset() const noexcept -> line_offset {
    return m_offset;
  }

  /**
   * @brief Mutable access to the line offset.
   *
   * @return Reference to the stored offset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto offset() noexcept -> line_offset& {
    return m_offset;
  }

  /**
   * @brief The configured line direction.
   *
   * @return The stored direction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto direction() const noexcept -> line_direction {
    return m_direction;
  }

  /**
   * @brief Mutable access to the line direction.
   *
   * @return Reference to the stored direction.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto direction() noexcept -> line_direction& {
    return m_direction;
  }

  /**
   * @brief The physical-to-logical level mapping.
   *
   * @return The stored polarity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto polarity() const noexcept -> line_polarity {
    return m_polarity;
  }

  /**
   * @brief Mutable access to the polarity.
   *
   * @return Reference to the stored polarity.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto polarity() noexcept -> line_polarity& {
    return m_polarity;
  }

  /**
   * @brief The requested on-chip bias network.
   *
   * @return The stored bias.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bias() const noexcept -> line_bias {
    return m_bias;
  }

  /**
   * @brief Mutable access to the bias network selection.
   *
   * @return Reference to the stored bias.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto bias() noexcept -> line_bias& {
    return m_bias;
  }

  /**
   * @brief The output driver topology.
   *
   * @return The stored drive topology.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto drive() const noexcept -> line_drive {
    return m_drive;
  }

  /**
   * @brief Mutable access to the drive topology.
   *
   * @return Reference to the stored drive topology.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto drive() noexcept -> line_drive& {
    return m_drive;
  }

  /**
   * @brief Converts a physical level to this line's logical level.
   *
   * @param physical Physical level read from the hardware.
   *
   * @return The logical level after applying \c polarity().
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto to_logical(bool const physical) const noexcept -> bool {
    return apply_polarity(physical, m_polarity);
  }

  /**
   * @brief Converts a logical level to this line's physical level.
   *
   * The inverse of \c to_logical; the polarity mapping is its own inverse.
   *
   * @param logical Logical level to drive.
   *
   * @return The physical level to hand to the hardware.
   *
   * @pre None.
   * @post \c to_logical(to_physical(x)) equals \c x.
   */
  [[nodiscard]] constexpr auto to_physical(bool const logical) const noexcept -> bool {
    return apply_polarity(logical, m_polarity);
  }

  /**
   * @brief Member-wise equality of two specs.
   *
   * The name views compare by content, so two specs naming the same line
   * through distinct storage with equal characters compare equal.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when every field is equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(line_spec const& lhs, line_spec const& rhs) noexcept -> bool = default;
};

}  // namespace nexenne::gpio
