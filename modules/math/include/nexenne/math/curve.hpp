#pragma once

/**
 * @file
 * @brief Parametric curve interpolation: Bezier, Catmull-Rom, Hermite, easing.
 *
 * Each curve comes as an evaluator plus a matching derivative (tangent) for when
 * you need the velocity at the sampled point. Every routine is templated on the
 * floating-point parameter type and an \c affine_point type: typically a
 * \c vector<Real, N>, but the scalar parameter type itself qualifies too, which
 * is handy for easing a single animated value along a curve.
 *
 * The polynomial forms are the standard ones; the per-segment references are in
 * the function comments, and the umbrella overview is in the math module guide.
 */

#include <concepts>

#include <nexenne/math/concepts.hpp>
#include <nexenne/math/scalar.hpp>

namespace nexenne::math {

/**
 * @brief Evaluates a quadratic Bezier curve at parameter \p t.
 *
 * Bernstein form B(t) = (1-t)^2*p0 + 2*(1-t)*t*p1 + t^2*p2: a blend of the three
 * points weighted by the degree-2 Bernstein polynomials, which sum to 1 so the
 * curve stays in their convex hull.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type (vector or scalar).
 * @param p0 Start point.
 * @param p1 Control point.
 * @param p2 End point.
 * @param t Curve parameter, normally in [0, 1].
 *
 * @return The point at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals \p p0 at \c t=0 and \p p2 at \c t=1.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto bezier_quadratic(
  Point const& p0, Point const& p1, Point const& p2, Real const t
) noexcept -> Point {
  auto const one_minus_t{Real{1} - t};
  auto const a{one_minus_t * one_minus_t};
  auto const b{Real{2} * one_minus_t * t};
  auto const c{t * t};
  return p0 * a + p1 * b + p2 * c;
}

/**
 * @brief Tangent (first derivative) of a quadratic Bezier at \p t.
 *
 * B'(t) = 2*(1-t)*(p1-p0) + 2*t*(p2-p1): the derivative of the Bernstein form,
 * itself a linear Bezier between the two control-leg vectors.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Start point.
 * @param p1 Control point.
 * @param p2 End point.
 * @param t Curve parameter, normally in [0, 1].
 *
 * @return The tangent vector at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals the first derivative of \c bezier_quadratic at \p t.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto bezier_quadratic_tangent(
  Point const& p0, Point const& p1, Point const& p2, Real const t
) noexcept -> Point {
  auto const one_minus_t{Real{1} - t};
  return (p1 - p0) * (Real{2} * one_minus_t) + (p2 - p1) * (Real{2} * t);
}

/**
 * @brief Evaluates a cubic Bezier curve at parameter \p t.
 *
 * Bernstein form
 * B(t) = (1-t)^3*p0 + 3*(1-t)^2*t*p1 + 3*(1-t)*t^2*p2 + t^3*p3, the workhorse of
 * vector graphics and animation (two endpoints, two free control handles).
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Start point.
 * @param p1 First control point.
 * @param p2 Second control point.
 * @param p3 End point.
 * @param t Curve parameter, normally in [0, 1].
 *
 * @return The point at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals \p p0 at \c t=0 and \p p3 at \c t=1.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto bezier_cubic(
  Point const& p0, Point const& p1, Point const& p2, Point const& p3, Real const t
) noexcept -> Point {
  auto const one_minus_t{Real{1} - t};
  auto const t2{t * t};
  auto const t3{t2 * t};
  auto const o2{one_minus_t * one_minus_t};
  auto const o3{o2 * one_minus_t};
  return p0 * o3 + p1 * (Real{3} * o2 * t) + p2 * (Real{3} * one_minus_t * t2) + p3 * t3;
}

/**
 * @brief Tangent (first derivative) of a cubic Bezier at \p t.
 *
 * B'(t) = 3*(1-t)^2*(p1-p0) + 6*(1-t)*t*(p2-p1) + 3*t^2*(p3-p2): a quadratic
 * Bezier between the three control-leg vectors.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Start point.
 * @param p1 First control point.
 * @param p2 Second control point.
 * @param p3 End point.
 * @param t Curve parameter, normally in [0, 1].
 *
 * @return The tangent vector at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals the first derivative of \c bezier_cubic at \p t.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto bezier_cubic_tangent(
  Point const& p0, Point const& p1, Point const& p2, Point const& p3, Real const t
) noexcept -> Point {
  auto const one_minus_t{Real{1} - t};
  auto const o2{one_minus_t * one_minus_t};
  auto const t2{t * t};
  return (p1 - p0) * (Real{3} * o2) + (p2 - p1) * (Real{6} * one_minus_t * t)
         + (p3 - p2) * (Real{3} * t2);
}

/**
 * @brief Evaluates a uniform Catmull-Rom spline segment between \p p1 and \p p2.
 *
 * A Catmull-Rom segment passes through \p p1 at \c t=0 and \p p2 at \c t=1 with
 * endpoint tangents 0.5*(p2-p0) and 0.5*(p3-p1), so chaining the segments over a
 * point list gives a C1-continuous curve that interpolates every point. The
 * closed form below is the expanded cubic in the four points:
 * 0.5*(2*p1 + (p2-p0)*t + (2*p0-5*p1+4*p2-p3)*t^2 + (-p0+3*p1-3*p2+p3)*t^3).
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Pre-segment control point.
 * @param p1 Segment start (returned at \c t=0).
 * @param p2 Segment end (returned at \c t=1).
 * @param p3 Post-segment control point.
 * @param t Curve parameter in [0, 1].
 *
 * @return The point at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals \p p1 at \c t=0 and \p p2 at \c t=1.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto catmull_rom(
  Point const& p0, Point const& p1, Point const& p2, Point const& p3, Real const t
) noexcept -> Point {
  auto const t2{t * t};
  auto const t3{t2 * t};
  return (p1 * Real{2} + (p2 - p0) * t + (p0 * Real{2} - p1 * Real{5} + p2 * Real{4} - p3) * t2
          + (p1 * Real{3} - p0 - p2 * Real{3} + p3) * t3)
         * Real{0.5};
}

/**
 * @brief Tangent of a uniform Catmull-Rom segment at \p t.
 *
 * The derivative of \c catmull_rom in \p t:
 * 0.5*((p2-p0) + (2*p0-5*p1+4*p2-p3)*2*t + (-p0+3*p1-3*p2+p3)*3*t^2). Useful for
 * smooth velocity along a Catmull-Rom path.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Pre-segment control point.
 * @param p1 Segment start.
 * @param p2 Segment end.
 * @param p3 Post-segment control point.
 * @param t Curve parameter in [0, 1].
 *
 * @return The tangent vector at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals the first derivative of \c catmull_rom at \p t.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto catmull_rom_tangent(
  Point const& p0, Point const& p1, Point const& p2, Point const& p3, Real const t
) noexcept -> Point {
  auto const t2{t * t};
  return ((p2 - p0) + (p0 * Real{2} - p1 * Real{5} + p2 * Real{4} - p3) * (Real{2} * t)
          + (p1 * Real{3} - p0 - p2 * Real{3} + p3) * (Real{3} * t2))
         * Real{0.5};
}

/**
 * @brief Evaluates a cubic Hermite spline segment.
 *
 * Defined by two endpoints and their tangents, blended by the Hermite basis:
 * H(t) = h00*p0 + h10*m0 + h01*p1 + h11*m1 with
 * h00 = 2t^3-3t^2+1, h10 = t^3-2t^2+t, h01 = -2t^3+3t^2, h11 = t^3-t^2. Bezier
 * and Catmull-Rom are both reparmeterizations of this basis.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Start point.
 * @param tangent0 Tangent at \p p0.
 * @param p1 End point.
 * @param tangent1 Tangent at \p p1.
 * @param t Curve parameter in [0, 1].
 *
 * @return The point at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals \p p0 at \c t=0 and \p p1 at \c t=1.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto hermite(
  Point const& p0, Point const& tangent0, Point const& p1, Point const& tangent1, Real const t
) noexcept -> Point {
  auto const t2{t * t};
  auto const t3{t2 * t};
  auto const h00{Real{2} * t3 - Real{3} * t2 + Real{1}};
  auto const h10{t3 - Real{2} * t2 + t};
  auto const h01{Real{-2} * t3 + Real{3} * t2};
  auto const h11{t3 - t2};
  return p0 * h00 + tangent0 * h10 + p1 * h01 + tangent1 * h11;
}

/**
 * @brief Tangent of a cubic Hermite segment at \p t.
 *
 * The derivative of \c hermite in \p t, with basis derivatives
 * h00' = 6t^2-6t, h10' = 3t^2-4t+1, h01' = -6t^2+6t, h11' = 3t^2-2t.
 *
 * @tparam Real  Floating-point parameter type.
 * @tparam Point Affine point type.
 * @param p0 Start point.
 * @param tangent0 Tangent at \p p0.
 * @param p1 End point.
 * @param tangent1 Tangent at \p p1.
 * @param t Curve parameter in [0, 1].
 *
 * @return The tangent vector at parameter \p t.
 *
 * @pre \p t is finite.
 * @post Equals the first derivative of \c hermite at \p t.
 */
template <std::floating_point Real, affine_point<Real> Point>
[[nodiscard]] constexpr auto hermite_tangent(
  Point const& p0, Point const& tangent0, Point const& p1, Point const& tangent1, Real const t
) noexcept -> Point {
  auto const t2{t * t};
  auto const h00d{Real{6} * t2 - Real{6} * t};
  auto const h10d{Real{3} * t2 - Real{4} * t + Real{1}};
  auto const h01d{Real{-6} * t2 + Real{6} * t};
  auto const h11d{Real{3} * t2 - Real{2} * t};
  return p0 * h00d + tangent0 * h10d + p1 * h01d + tangent1 * h11d;
}

/**
 * @brief Smoothstep easing: the cubic 3t^2-2t^3, clamped to [0, 1].
 *
 * Convenience wrapper over \c smoothstep(0, 1, t) for use as an easing curve
 * (zero first derivative at both ends, so motion eases in and out).
 *
 * @tparam Real Floating-point type.
 * @param t Parameter, clamped to [0, 1].
 *
 * @return The eased parameter.
 *
 * @pre None.
 * @post Lies in [0, 1].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ease_smoothstep(Real const t) noexcept -> Real {
  return smoothstep(Real{0}, Real{1}, t);
}

/**
 * @brief Smootherstep easing: the quintic 6t^5-15t^4+10t^3, clamped to [0, 1].
 *
 * Perlin's improved easing curve. Both its first and second derivatives vanish
 * at the endpoints (smoothstep zeroes only the first), so it starts and stops
 * even more gently, at a small extra cost. The input is saturated first so the
 * polynomial is only ever evaluated on its well-behaved [0, 1] range.
 * https://en.wikipedia.org/wiki/Smoothstep#Variations
 *
 * @tparam Real Floating-point type.
 * @param t Parameter, clamped to [0, 1].
 *
 * @return The eased parameter.
 *
 * @pre None.
 * @post Lies in [0, 1].
 */
template <std::floating_point Real>
[[nodiscard]] constexpr auto ease_smootherstep(Real const t) noexcept -> Real {
  auto const u{saturate(t)};
  return u * u * u * (u * (u * Real{6} - Real{15}) + Real{10});
}

}  // namespace nexenne::math
