#pragma once

/**
 * @file
 * @brief Error codes and the result alias for fallible nexenne::gpio operations.
 *
 * Module-wide error policy:
 * - Nothing in this module throws; every fallible operation returns
 *   \c result<T>, i.e. \c std::expected<T, gpio_error>.
 * - A query that can legitimately have no answer (no edge event ready before
 *   the timeout, no line with a given name in a request set) reports the miss
 *   inside the success channel (\c std::optional inside \c result, or a plain
 *   \c std::optional), never as an error: a clean miss is an answer.
 * - Programmer precondition violations (an offset that was never requested
 *   handed to a low-level helper) are documented with a precondition tag and
 *   checked by debug asserts; they are not reported as errors.
 *
 * Backends map their native failure codes (errno on Linux, a HAL status on an
 * embedded target) onto these neutral categories, so portable code can branch
 * on intent ("retry when \c busy") without knowing which backend produced the
 * error. Formatting for \c gpio_error lives in format.hpp so this header stays
 * free of the standard format header.
 */

#include <expected>
#include <string_view>

namespace nexenne::gpio {

/**
 * @brief Recoverable error reported by a fallible GPIO operation.
 */
enum class gpio_error {
  invalid_argument,   ///< An argument was rejected before reaching the hardware.
  not_found,          ///< No requested line matches the given name or offset.
  not_open,           ///< The operation needs an open request and there is none.
  permission_denied,  ///< The OS denied access to the device.
  busy,               ///< The line or chip is held by another consumer.
  io_error,           ///< A device open, read, write, or control call failed.
  timeout,            ///< A timed wait elapsed without producing a result.
  overflow,           ///< An event buffer overflowed and events were dropped.
  unsupported,        ///< The operation is unavailable on this backend or platform.
};

/**
 * @brief The result of a fallible GPIO operation: a value or an error.
 *
 * An alias for \c std::expected<T, gpio_error>, the module's single fallible
 * return type.
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, gpio_error>;

/**
 * @brief Human-readable name of a \c gpio_error.
 *
 * @param err Error to describe.
 *
 * @return A static string view naming the error.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(gpio_error const err) noexcept -> std::string_view {
  switch (err) {
    case gpio_error::invalid_argument:
      return "invalid_argument";
    case gpio_error::not_found:
      return "not_found";
    case gpio_error::not_open:
      return "not_open";
    case gpio_error::permission_denied:
      return "permission_denied";
    case gpio_error::busy:
      return "busy";
    case gpio_error::io_error:
      return "io_error";
    case gpio_error::timeout:
      return "timeout";
    case gpio_error::overflow:
      return "overflow";
    case gpio_error::unsupported:
      return "unsupported";
  }
  return "unknown";
}

/**
 * @brief Whether an error is worth retrying with backoff.
 *
 * A production system that supervises a GPIO connection needs to know which
 * failures may clear on their own (a device briefly held by another consumer,
 * an interrupted kernel call, an overflowed buffer) and which never will
 * without operator action (wrong permissions, a chip that does not exist, a
 * platform without the facility). This classification is the library's;
 * the retry policy built on it (backoff, limits, alarms) is the caller's.
 *
 * @param err Error to classify.
 *
 * @return \c true for \c busy, \c timeout, \c io_error, and \c overflow;
 *         \c false for every error that indicates a configuration, argument,
 *         permission, or platform problem.
 *
 * @pre None.
 * @post None.
 */
[[nodiscard]] constexpr auto is_transient(gpio_error const err) noexcept -> bool {
  switch (err) {
    case gpio_error::busy:
    case gpio_error::timeout:
    case gpio_error::io_error:
    case gpio_error::overflow:
      return true;
    case gpio_error::invalid_argument:
    case gpio_error::not_found:
    case gpio_error::not_open:
    case gpio_error::permission_denied:
    case gpio_error::unsupported:
      return false;
  }
  return false;
}

}  // namespace nexenne::gpio
