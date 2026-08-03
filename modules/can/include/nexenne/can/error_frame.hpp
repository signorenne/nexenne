#pragma once

/**
 * @file
 * @brief Decoding CAN error frames into a controller state and error counters.
 *
 * A CAN controller does not only carry data; when the bus misbehaves it reports
 * errors. Linux SocketCAN surfaces these as ordinary frames with the error flag
 * set, where the identifier carries a bitmask of error classes and the eight data
 * bytes carry the details, including the controller status and the transmit and
 * receive error counters. This header gives those fields names and a small
 * decoder that turns an error frame into a \c bus.hpp \c bus_state and
 * \c error_counters. The constants mirror \c linux/can/error.h but are plain
 * values, so the header is portable.
 *
 * Reference: the Linux kernel \c linux/can/error.h error-frame layout.
 */

#include <cstdint>
#include <optional>

#include <nexenne/can/bus.hpp>
#include <nexenne/can/frame.hpp>
#include <nexenne/can/id.hpp>

namespace nexenne::can {

/**
 * @brief Error class bit: the controller changed state; details in data byte 1.
 */
inline constexpr std::uint32_t err_class_controller{0x0000'0004};

/**
 * @brief Error class bit: the controller has gone bus-off.
 */
inline constexpr std::uint32_t err_class_bus_off{0x0000'0040};

/**
 * @brief Error class bit: the error counters in data bytes 6 and 7 are valid.
 */
inline constexpr std::uint32_t err_class_counters{0x0000'0200};

/**
 * @brief Controller status bit (data byte 1): the receive side is error-passive.
 */
inline constexpr std::uint8_t err_controller_rx_passive{0x10};

/**
 * @brief Controller status bit (data byte 1): the transmit side is error-passive.
 */
inline constexpr std::uint8_t err_controller_tx_passive{0x20};

/**
 * @brief The decoded contents of a CAN error frame.
 */
struct error_report {
  bus_state state{bus_state::error_active};  ///< The controller state the frame implies.
  error_counters counters{};                 ///< The transmit and receive error counters.
  std::uint32_t classes{0};                  ///< The raw error class bitmask from the id.

  /**
   * @brief Equality over all decoded fields.
   *
   * @param lhs Left operand.
   * @param rhs Right operand.
   *
   * @return \c true when the state, counters, and classes are equal.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] friend constexpr auto
  operator==(error_report const& lhs, error_report const& rhs) noexcept -> bool = default;
};

/**
 * @brief Reports whether a frame is a CAN error frame.
 *
 * @param f Frame to test.
 *
 * @return \c true when the frame's error flag is set.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto is_error_frame(frame const& f) noexcept -> bool {
  return f.id().error_frame();
}

/**
 * @brief Decodes a CAN error frame into a controller state and counters.
 *
 * Reads the error class mask from the identifier, the controller status from data
 * byte 1, and the error counters from data bytes 6 and 7 when present. The state
 * is bus-off if the bus-off class is set, otherwise error-passive if either
 * passive controller-status bit is set, otherwise error-active.
 *
 * @param f Frame to decode.
 *
 * @return The decoded report, or \c std::nullopt when \p f is not an error frame.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto decode_error_frame(frame const& f
) noexcept -> std::optional<error_report> {
  if (!is_error_frame(f)) {
    return std::nullopt;
  }
  error_report report;
  report.classes = f.id().raw() & extended_id_mask;
  auto const payload{f.data()};

  // Per linux/can/error.h, data byte 1 carries the controller status only when the
  // controller error class is set, and the counters in bytes 6 and 7 only when the
  // counter class is set. Reading them otherwise would fabricate a state or counts
  // from bytes that belong to an unrelated error class.
  std::uint8_t controller_status{0};
  if ((report.classes & err_class_controller) != 0U && payload.size() > 1) {
    controller_status = std::to_integer<std::uint8_t>(payload[1]);
  }
  if ((report.classes & err_class_counters) != 0U && payload.size() > 7) {
    report.counters.transmit = std::to_integer<std::uint8_t>(payload[6]);
    report.counters.receive = std::to_integer<std::uint8_t>(payload[7]);
  }

  if ((report.classes & err_class_bus_off) != 0U) {
    report.state = bus_state::bus_off;
  } else if ((controller_status & (err_controller_rx_passive | err_controller_tx_passive)) != 0U) {
    report.state = bus_state::error_passive;
  } else {
    report.state = bus_state::error_active;
  }
  return report;
}

}  // namespace nexenne::can
