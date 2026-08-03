#pragma once

/**
 * @file
 * @brief The compact edge event a backend emits, in the physical domain.
 *
 * A \c line_event carries only what is needed to route one edge
 * notification: the originating chip and offset, the raw physical level, the
 * physical edge direction, a timestamp, and a sequence number for drop
 * detection. Backends emit the physical level only; polarity is applied
 * later, in one place, when \c decode.hpp turns the event into a logical
 * \c line_value.hpp against the line's spec.
 *
 * The struct is trivially copyable and laid out largest-field-first so it
 * packs into 24 bytes with no internal padding: events are stored inline
 * many times over in transport rings, so every byte saved scales with the
 * queue depth. It is safe to pass by value across threads and through
 * lock-free queues.
 */

#include <nexenne/gpio/line_types.hpp>

namespace nexenne::gpio {

/**
 * @brief One raw edge notification from a backend: physical level, no polarity.
 */
struct line_event {
  using value_type = bool;

  event_sequence sequence{0};       ///< Monotonic sequence number; zero means unset.
  event_time timestamp{};           ///< When the edge fired, on the event clock.
  line_offset offset{0};            ///< Zero-based line offset within \c chip.
  chip_id chip{0};                  ///< Identifier of the originating chip.
  bool physical{false};             ///< Raw physical level after the edge.
  edge_kind edge{edge_kind::none};  ///< Physical edge direction that produced the event.

  /**
   * @brief Member-wise equality of two events.
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
  operator==(line_event const& lhs, line_event const& rhs) noexcept -> bool = default;
};

}  // namespace nexenne::gpio
