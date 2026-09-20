#pragma once

/**
 * @file
 * @brief Concepts for constraining nexenne::math templates.
 *
 * Re-exports a small set of arithmetic concepts so leaf headers can constrain
 * templates without each one pulling in the type_traits header. The standard
 * \c std::floating_point and \c std::integral concepts already live in the
 * concepts header and are used directly.
 */

#include <concepts>
#include <type_traits>

namespace nexenne::math {

/**
 * @brief Any built-in arithmetic type (integral or floating-point).
 *
 * \c std::is_arithmetic_v minus \c bool and the character types (\c char,
 * \c wchar_t, \c char8_t, \c char16_t, \c char32_t), which are not numbers:
 * \c vector<bool, 3> would negate \c true to \c true. \c signed \c char and
 * \c unsigned \c char, the 8-bit integers, stay admitted.
 *
 * @tparam Value Type to test.
 */
template <typename Value>
concept arithmetic =
  std::is_arithmetic_v<Value> && !std::same_as<std::remove_cv_t<Value>, bool>
  && !std::same_as<std::remove_cv_t<Value>, char> && !std::same_as<std::remove_cv_t<Value>, wchar_t>
  && !std::same_as<std::remove_cv_t<Value>, char8_t>
  && !std::same_as<std::remove_cv_t<Value>, char16_t>
  && !std::same_as<std::remove_cv_t<Value>, char32_t>;

/**
 * @brief Any signed built-in arithmetic type.
 *
 * Equivalent to \c arithmetic plus \c std::is_signed_v. Useful for functions
 * that need a notion of negation (sign, abs, normalize).
 *
 * @tparam Value Type to test.
 */
template <typename Value>
concept signed_arithmetic = arithmetic<Value> && std::is_signed_v<Value>;

/**
 * @brief A point type that can be affinely combined over a scalar field.
 *
 * Requires the operations curve and interpolation routines perform: adding and
 * subtracting two points and scaling a point by a \p Scalar, each yielding a
 * point again. Satisfied by \c vector<Real, N> and by the scalar \p Scalar
 * itself (so a single value can be eased along a curve like a position can).
 *
 * @tparam Point  Point type under test.
 * @tparam Scalar Scalar field the point is scaled by (a floating-point type).
 */
template <typename Point, typename Scalar>
concept affine_point = requires(Point const p, Scalar const s) {
  { p + p } -> std::convertible_to<Point>;
  { p - p } -> std::convertible_to<Point>;
  { p * s } -> std::convertible_to<Point>;
};

}  // namespace nexenne::math
