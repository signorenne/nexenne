#pragma once

/**
 * @file
 * @brief Error codes and the result alias for fallible nexenne::can operations.
 *
 * Module-wide error policy:
 * - Every non-allocating function is \c noexcept; nothing in this module throws.
 * - An operation that can fail on input it can itself detect (an out-of-range
 *   identifier, a data length the frame type cannot hold, a signal field that
 *   runs past the payload, a physical value that does not fit its bits) returns
 *   \c result<T>, i.e. \c std::expected<T, can_error>. There is no separate
 *   precondition "fast path": a caller who knows the input is valid still
 *   unwraps via \c *result, and the compiler elides the dead error branch, so
 *   the runtime cost is zero.
 * - A query that can legitimately have no answer (no message in a database for
 *   an identifier, no frame ready on a non-blocking receive) returns
 *   \c std::optional, not \c result: a clean miss is an answer, not an error.
 * - Programmer precondition violations (a bit index past a field width handed to
 *   a low-level helper) are documented with a precondition tag and checked by
 *   debug asserts; they are not reported as errors.
 *
 * Formatting for \c can_error lives in format.hpp so this header stays free of
 * the standard format header.
 */

#include <expected>
#include <string_view>

namespace nexenne::can {

/**
 * @brief Recoverable error reported by a fallible CAN operation.
 */
enum class can_error {
  invalid_id,           ///< Identifier exceeds its 11-bit or 29-bit range.
  invalid_dlc,          ///< Data length code or length is not representable.
  payload_too_large,    ///< Data length exceeds the frame type's capacity.
  signal_out_of_range,  ///< A signal field runs past the frame's data length.
  value_out_of_range,   ///< A physical value cannot be encoded in a signal's bits.
  unsupported,          ///< The backend is unavailable on this platform.
  io_error,             ///< A socket open, read, write, or option call failed.
  bus_off,              ///< The controller is bus-off and refused the operation.
  buffer_full,          ///< A transmit or loopback queue is at capacity.
  parse_error,          ///< A text database (DBC) could not be parsed.
};

/**
 * @brief The result of a fallible CAN operation: a value or an error.
 *
 * An alias for \c std::expected<T, can_error>, the module's single fallible
 * return type.
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, can_error>;

/**
 * @brief Human-readable name of a \c can_error.
 *
 * @param err Error to describe.
 *
 * @return A static string view naming the error.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(can_error const err) noexcept -> std::string_view {
  switch (err) {
    case can_error::invalid_id:
      return "invalid_id";
    case can_error::invalid_dlc:
      return "invalid_dlc";
    case can_error::payload_too_large:
      return "payload_too_large";
    case can_error::signal_out_of_range:
      return "signal_out_of_range";
    case can_error::value_out_of_range:
      return "value_out_of_range";
    case can_error::unsupported:
      return "unsupported";
    case can_error::io_error:
      return "io_error";
    case can_error::bus_off:
      return "bus_off";
    case can_error::buffer_full:
      return "buffer_full";
    case can_error::parse_error:
      return "parse_error";
  }
  return "unknown";
}

}  // namespace nexenne::can
