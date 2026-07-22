#pragma once

/**
 * @file
 * @brief One observation of a GPIO line, in the logical domain.
 *
 * A \c line_value is what application code consumes: the logical level with
 * polarity already applied, enough identity to route the sample (chip,
 * offset, and the line's name), the logical edge that produced it, and the
 * timestamp and sequence number carried over from the originating event.
 * The physical-domain counterpart that backends emit is \c line_event.hpp;
 * \c decode.hpp converts one to the other against a \c line_spec.hpp.
 *
 * The type is trivially copyable and safe to pass by value across threads.
 * The name is a non-owning view whose backing storage (normally the spec's
 * name) must outlive the value.
 */

#include <string_view>

#include <nexenne/gpio/line_spec.hpp>
#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief One logical observation of a line: level, identity, edge, and time.
 *
 * Construct directly from fields, or from a \c line_spec that supplies the
 * identity. The stored level must already be logical; this type never
 * applies polarity itself, because only the decode step that owns the spec
 * can convert physical to logical exactly once.
 */
class line_value {
public:
  using value_type = bool;

private:
  std::string_view m_name{};
  event_time m_timestamp{};
  event_sequence m_sequence{0};
  line_offset m_offset{0};
  chip_id m_chip{0};
  bool m_logical{false};
  edge_kind m_edge{edge_kind::none};

public:
  /**
   * @brief Constructs an empty observation with every field at its default.
   *
   * @pre None.
   * @post The name is empty, the level is \c false, and the edge is
   *       \c edge_kind::none.
   */
  constexpr line_value() noexcept = default;

  /**
   * @brief Constructs an observation from explicit identity and state.
   *
   * @param name Non-owning view of the line name; it must outlive the value.
   * @param chip Identifier of the owning chip.
   * @param offset Zero-based line offset within \p chip.
   * @param logical Logical level, polarity already applied.
   * @param edge Logical edge that produced the observation.
   * @param sequence Sequence number; zero means unset.
   * @param timestamp When the observation was made, on the event clock.
   *
   * @pre None.
   * @post Every accessor returns the corresponding argument.
   */
  constexpr line_value(
    std::string_view const name,
    chip_id const chip,
    line_offset const offset,
    bool const logical,
    edge_kind const edge = edge_kind::none,
    event_sequence const sequence = event_sequence{0},
    event_time const timestamp = event_time{}
  ) noexcept
    : m_name{name},
      m_timestamp{timestamp},
      m_sequence{sequence},
      m_offset{offset},
      m_chip{chip},
      m_logical{logical},
      m_edge{edge} {}

  /**
   * @brief Constructs an observation whose identity comes from a spec.
   *
   * Copies the name, chip, and offset from \p spec. Polarity is NOT applied
   * here; pass an already-logical level.
   *
   * @param spec Spec supplying the identity; its name storage must outlive
   *             the value.
   * @param logical Logical level, polarity already applied.
   * @param edge Logical edge that produced the observation.
   * @param sequence Sequence number; zero means unset.
   * @param timestamp When the observation was made, on the event clock.
   *
   * @pre None.
   * @post \c name(), \c chip(), and \c offset() mirror \p spec.
   */
  constexpr line_value(
    line_spec const& spec,
    bool const logical,
    edge_kind const edge = edge_kind::none,
    event_sequence const sequence = event_sequence{0},
    event_time const timestamp = event_time{}
  ) noexcept
    : line_value{spec.name(), spec.chip(), spec.offset(), logical, edge, sequence, timestamp} {}

  /**
   * @brief The logical name of the observed line.
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
   * @brief The identifier of the owning chip.
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
   * @brief The logical level of the observation.
   *
   * @return The stored level, polarity already applied.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto logical() const noexcept -> bool {
    return m_logical;
  }

  /**
   * @brief The logical edge that produced the observation.
   *
   * @return The stored edge; \c edge_kind::none for a steady-state sample.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto edge() const noexcept -> edge_kind {
    return m_edge;
  }

  /**
   * @brief The sequence number carried over from the originating event.
   *
   * @return The stored sequence number; zero means unset.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto sequence() const noexcept -> event_sequence {
    return m_sequence;
  }

  /**
   * @brief When the observation was made, on the event clock.
   *
   * @return The stored timestamp.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto timestamp() const noexcept -> event_time {
    return m_timestamp;
  }

  /**
   * @brief Member-wise equality of two observations.
   *
   * The name views compare by content.
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
  operator==(line_value const& lhs, line_value const& rhs) noexcept -> bool = default;
};

}  // namespace nexenne::gpio
