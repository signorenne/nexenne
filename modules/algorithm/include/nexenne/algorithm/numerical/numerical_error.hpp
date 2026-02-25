#pragma once

/**
 * @file
 * @brief Error vocabulary shared by the numerical routines.
 *
 * Numerical failures are not container failures: a root-finder that exhausts
 * its iteration budget, or an FFT handed a non-power-of-two size, needs codes
 * that name the numerical precondition that broke rather than borrowing a
 * container error. Routines report failure through
 * \c std::expected<T, numerical_error>.
 */

#include <cstdint>
#include <string_view>

namespace nexenne::algorithm {

/**
 * @brief Error codes for the numerical routines.
 *
 * Each names the numerical precondition that broke.
 */
enum class numerical_error : std::uint8_t {
  not_bracketed,   ///< Bracket endpoints do not straddle a sign change.
  no_convergence,  ///< Iteration budget exhausted before tolerance was met.
  invalid_size,    ///< Input size violates a routine precondition.
};

/**
 * @brief Returns a stable lowercase name for \p e.
 *
 * @param e Error code to name.
 *
 * @return The enumerator name, or "?" for an unrecognised value.
 *
 * @pre None.
 * @post The returned view refers to a static string and outlives the call.
 *
 * @complexity \c O(1).
 */
[[nodiscard]] constexpr auto to_string(numerical_error const e) noexcept -> std::string_view {
  switch (e) {
    case numerical_error::not_bracketed:
      return "not_bracketed";
    case numerical_error::no_convergence:
      return "no_convergence";
    case numerical_error::invalid_size:
      return "invalid_size";
  }
  return "?";
}

}  // namespace nexenne::algorithm
