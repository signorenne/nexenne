#pragma once

/**
 * @file
 * @brief Error codes and the result alias for fallible math operations.
 *
 * Module-wide error policy:
 * - Math types are fixed-size and never allocate, so every operation here is
 *   \c noexcept; nothing in this module throws under any circumstance.
 * - An operation with a meaningful runtime failure mode (singular matrix,
 *   zero-length vector) returns \c result<T>, i.e.
 *   \c std::expected<T, math_error>. There is no precondition-based "fast path"
 *   variant: callers who know their input is valid still unwrap via
 *   \c result.value() or \c *result, and the compiler elides the dead branch,
 *   so the runtime cost is essentially zero.
 * - Pure infallible operations (addition, dot product, length squared,
 *   comparison) return their natural type directly.
 *
 * Geometry-side errors live in \c nexenne::geometry. The split is intentional:
 * each module owns its error space, so a \c std::expected from geometry never
 * accidentally carries a math error code or vice versa.
 */

#include <expected>
#include <string_view>

namespace nexenne::math {

/**
 * @brief Recoverable error reported by a fallible math operation.
 */
enum class math_error {
  zero_length_vector,  ///< Required a non-zero vector but got one of zero length.
  singular_matrix,     ///< Required an invertible matrix but got a singular one.
  parallel_vectors,    ///< Vectors that should be independent were (anti-)parallel.
  invalid_input,       ///< Basic precondition failed (NaN/infinite component, negative radius).
};

/**
 * @brief The result of a fallible math operation: a value or an error.
 *
 * An alias for \c std::expected<T, math_error>, the module's single fallible
 * return type.
 *
 * @tparam T Value type on success.
 */
template <typename T>
using result = std::expected<T, math_error>;

/**
 * @brief Human-readable name of a \c math_error.
 *
 * @param err Error to describe.
 *
 * @return A static string view naming the error.
 *
 * @pre None.
 * @post The returned view refers to a string with program lifetime.
 */
[[nodiscard]] constexpr auto to_string(math_error const err) noexcept -> std::string_view {
  switch (err) {
    case math_error::zero_length_vector:
      return "zero_length_vector";
    case math_error::singular_matrix:
      return "singular_matrix";
    case math_error::parallel_vectors:
      return "parallel_vectors";
    case math_error::invalid_input:
      return "invalid_input";
  }
  return "unknown";
}

}  // namespace nexenne::math
