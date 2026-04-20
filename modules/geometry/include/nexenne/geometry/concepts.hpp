#pragma once

/**
 * @file
 * @brief Concepts for constraining nexenne::geometry templates.
 *
 * Currently the support-function protocol the GJK and EPA collision algorithms
 * require of a shape. Kept in its own header, mirroring the other modules, so a
 * leaf header can constrain a template without pulling in an algorithm header.
 */

#include <concepts>

#include <nexenne/math/vector.hpp>

namespace nexenne::geometry {

/**
 * @brief A convex shape usable with the support-function collision algorithms.
 *
 * Satisfied by any \p Shape for which \c support(shape, direction), found by
 * argument-dependent lookup, returns the \c vector<Real, 3> surface point
 * furthest along \p direction. That support mapping is the entire interface GJK
 * and EPA require: they never inspect a shape's faces or edges. \c convex_hull3
 * models it, and an analytic primitive does so by supplying its own \c support
 * overload.
 *
 * @tparam Shape Candidate shape type.
 * @tparam Real Floating-point component type of the direction and result.
 */
template <typename Shape, typename Real>
concept convex_shape =
  std::floating_point<Real>
  && requires(Shape const& shape, nexenne::math::vector<Real, 3> const& direction) {
       { support(shape, direction) } -> std::same_as<nexenne::math::vector<Real, 3>>;
     };

}  // namespace nexenne::geometry
