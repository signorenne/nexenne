#pragma once

/**
 * @file
 * @brief Error codes for fallible nexenne::geometry operations.
 *
 * Module error policy:
 *   - Every operation that can fail on input the operation itself can detect
 *     (a degenerate primitive, a non-finite component, parallel features that
 *     cannot intersect) returns \c std::expected<T, geometry_error>. There is
 *     no separate precondition "fast path": a caller who knows the input is
 *     valid still unwraps via \c *result, and the compiler elides the dead
 *     error branch, so the runtime cost is essentially zero.
 *   - A query that can legitimately have "no result" (a ray that misses a
 *     sphere, a segment that does not cross a plane) returns \c std::optional,
 *     not \c std::expected. \c geometry_error is reserved for invalid input.
 *
 * Math-side failures live in \c nexenne::math::math_error: each module owns its
 * own error space. Formatting for \c geometry_error lives in
 * \c nexenne/geometry/format.hpp, so this header stays free of \c \<format\>.
 */

#include <expected>
#include <string_view>

namespace nexenne::geometry {

/**
 * @brief Error enumeration for fallible geometry operations.
 */
enum class geometry_error {
  /// Primitive is degenerate (zero-length segment, zero-radius circle,
  /// collinear triangle vertices, and similar).
  degenerate_primitive,

  /// Input violated a basic precondition the operation can detect (a NaN
  /// component, a non-finite radius, and similar).
  invalid_input,

  /// Two features are parallel and cannot produce the requested intersection
  /// (parallel lines, parallel planes).
  parallel,
};

/**
 * @brief The result of a fallible geometry operation: a value or an error.
 *
 * An alias for \c std::expected<T, geometry_error>, the module's single
 * fallible return type. A query that may legitimately have no answer returns
 * \c std::optional instead (see the file-level error policy).
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, geometry_error>;

/**
 * @brief Human-readable name of a \c geometry_error.
 *
 * @param err Error to describe.
 *
 * @return Static string view naming the error; "unknown" for an out-of-range
 *         value.
 *
 * @pre None.
 * @post The returned view points to a string literal with static storage.
 */
[[nodiscard]] constexpr auto to_string(geometry_error const err) noexcept -> std::string_view {
  switch (err) {
    case geometry_error::degenerate_primitive:
      return "degenerate_primitive";
    case geometry_error::invalid_input:
      return "invalid_input";
    case geometry_error::parallel:
      return "parallel";
  }
  return "unknown";
}

}  // namespace nexenne::geometry
