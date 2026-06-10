#pragma once

/**
 * @file
 * @brief Placeholder API for the nexenne::__MODULE__ module; replace it.
 */

#include <concepts>

namespace nexenne::__MODULE__ {

/**
 * @brief Returns \p value unchanged.
 *
 * Placeholder so the freshly-scaffolded module has at least one public
 * symbol to compile, test, and document. Delete it once the module has
 * real content.
 *
 * @tparam Real Floating-point type satisfying \c std::floating_point.
 * @param value The value to return.
 *
 * @return \p value, of the same type \p Real.
 *
 * @pre None.
 * @post Returned value compares equal to \p value.
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto identity(Real const value) noexcept -> Real {
  return Real{value};
}

}  // namespace nexenne::__MODULE__
