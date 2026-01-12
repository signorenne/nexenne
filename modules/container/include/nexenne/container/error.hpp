#pragma once

/**
 * @file
 * @brief Error codes and the result alias for fallible container operations.
 *
 * Module-wide error policy:
 * - All non-allocating functions are \c noexcept.
 * - An operation that mutates state and can fail at a documented boundary
 *   (popping an empty queue, pushing onto a full fixed-capacity buffer) returns
 *   \c result<T>, i.e. \c std::expected<T, container_error>; there is no
 *   precondition-based "fast path" variant.
 * - An operation whose only failure mode is \c std::bad_alloc from the
 *   underlying allocator (for example \c slot_map::insert or \c small_vector
 *   growth past its inline capacity) is \c noexcept: allocation failure calls
 *   \c std::terminate, matching the rest of nexenne. Callers needing recovery
 *   should pre-allocate with \c reserve.
 */

#include <expected>
#include <string_view>

namespace nexenne::container {

/**
 * @brief Recoverable error reported by a fallible container operation.
 */
enum class container_error {
  full,          ///< The container is at capacity (push to a full ring buffer).
  empty,         ///< The container has no elements (pop from an empty queue).
  out_of_range,  ///< An index was outside the container's logical size.
  not_found,     ///< A looked-up key or handle is not present.
};

/**
 * @brief The result of a fallible container operation: a value or an error.
 *
 * An alias for \c std::expected<T, container_error>, the module's single
 * fallible-return type.
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, container_error>;

/**
 * @brief Human-readable name of a \c container_error.
 *
 * @param err Error to describe.
 *
 * @return A static string view naming the error.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(container_error const err) noexcept -> std::string_view {
  switch (err) {
    case container_error::full:
      return "full";
    case container_error::empty:
      return "empty";
    case container_error::out_of_range:
      return "out_of_range";
    case container_error::not_found:
      return "not_found";
  }
  return "unknown";
}

}  // namespace nexenne::container
