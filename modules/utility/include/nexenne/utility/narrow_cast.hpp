#pragma once

/**
 * @file
 * @brief Checked narrowing cast that asserts the value is preserved in debug.
 */

#include <cassert>
#include <type_traits>

namespace nexenne::utility {

/**
 * @brief Narrowing cast that, in debug builds, asserts the value is preserved.
 *
 * Performs \c static_cast<To>(from), then in debug builds asserts that casting
 * the result back to \p From reproduces \p from and that the sign did not
 * flip. The sign check matters because a round-trip alone accepts
 * \c narrow_cast<unsigned>(-1), which maps to the maximum value and back yet
 * is almost always a bug. With \c NDEBUG defined the asserts vanish and the
 * call compiles to exactly the \c static_cast.
 *
 * @tparam To Target arithmetic type.
 * @tparam From Source arithmetic type, deduced.
 * @param from Value to narrow.
 *
 * @return \p from converted to \p To.
 *
 * @pre \p from is representable in \p To: casting the result back to \p From
 *      reproduces \p from and leaves the sign unchanged. Violations assert in
 *      debug and are silent under \c NDEBUG.
 * @post Casting the result back to \p From equals \p from.
 *
 * @par Example
 * \code
 * auto const wide{std::int32_t{300}};
 * auto const narrow{nexenne::utility::narrow_cast<std::int16_t>(wide)};
 * \endcode
 */
template <typename To, typename From>
  requires std::is_arithmetic_v<To> && std::is_arithmetic_v<From>
[[nodiscard]] constexpr auto narrow_cast(From const from) noexcept -> To {
  auto const to{static_cast<To>(from)};
  assert(static_cast<From>(to) == from && "narrow_cast: value changed during narrowing conversion");
  if constexpr (!std::is_floating_point_v<To> && !std::is_floating_point_v<From>
                && std::is_signed_v<To> != std::is_signed_v<From>) {
    assert(
      (to < To{}) == (from < From{}) && "narrow_cast: sign changed during narrowing conversion"
    );
  }
  return to;
}

}  // namespace nexenne::utility
