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
 * Equivalent to \c std::is_arithmetic_v but exposed as a concept so it can be
 * used with the terse template-argument syntax.
 *
 * @tparam Value Type to test.
 */
template <typename Value>
concept arithmetic = std::is_arithmetic_v<Value>;

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
  { p* s } -> std::convertible_to<Point>;
};

}  // namespace nexenne::math
