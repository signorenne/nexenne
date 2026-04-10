#pragma once

/**
 * @file
 * @brief Algorithms over \c vector<T, N>: products, norms, normalization,
 *        interpolation, projection, and reductions.
 *
 * Sections: dot, length, and distance; the cross products (a 2D scalar
 * pseudo-cross and a 3D vector cross); normalization (a checked variant, a fast
 * variant, and a fallback variant); element-wise interpolation and clamping;
 * projection, rejection, and reflection; component-wise min/max and the Hadamard
 * product; reductions (sum, product, min/max component); the L1 and L-infinity
 * norms; and the unsigned angle between two vectors.
 *
 * Most algorithms work at any \c N; a few are dimension-specific (the two cross
 * forms, and \c perpendicular for 2D). Fallible operations (normalize, project,
 * angle_between) return \c result<T> (\c std::expected<T, math_error>); the
 * infallible ones return their value directly. Kept separate from vector.hpp so
 * that header stays focused on storage and the element-wise algebra.
 *
 * SIMD note. The element-wise products feeding \c dot are computed with one
 * packed multiply (a \c vector<float,4> dot emits a single SSE \c mulps), but the
 * sum that reduces them stays a sequential scalar add chain: a horizontal SIMD
 * sum would reassociate the floating-point additions and change the rounding, so
 * the library keeps the deterministic left-to-right order. The same holds for the
 * other reductions here.
 */

#include <cmath>
#include <concepts>
#include <cstddef>
#include <expected>

#include <nexenne/math/error.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/scalar.hpp>
#include <nexenne/math/vector.hpp>

namespace nexenne::math {

/**
 * @brief Dot product of two vectors.
 *
 * The sum of the element-wise products. Geometrically \c |a|*|b|*cos(theta), so
 * it is positive when the vectors point the same general way, zero when
 * perpendicular, negative when opposed.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return Sum of the element-wise products.
 *
 * @pre Components are finite.
 * @post Result is finite.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
dot(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> Value {
  // The N products are computed packed (one SSE mulps for a float vec4), then
  // accumulated left to right. The sequential sum is deliberate: reassociating
  // it into a horizontal SIMD reduction would change the floating-point rounding,
  // and the library values a deterministic result over the few saved cycles.
  auto result{Value{}};
  for (std::size_t i{0}; i < N; ++i) {
    result += a[i] * b[i];
  }
  return result;
}

/**
 * @brief Squared length (Euclidean norm squared).
 *
 * Avoids the square root. Use when only the relative ordering of magnitudes
 * matters, for example nearest-neighbor checks, where comparing squared lengths
 * gives the same answer for less work.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector.
 *
 * @return \c dot(v, v).
 *
 * @pre Components are finite and small enough that the sum of squares does not
 *      overflow \p Value. This bites well below the per-component range: for
 *      \c float it overflows to +inf once a component exceeds ~1.8e19
 *      (sqrt(FLT_MAX)) even though the true length is representable, and for a
 *      signed integer \p Value the sum-of-squares is undefined on overflow
 *      (e.g. \c vector2_i{50000, 0} already exceeds INT_MAX). Scale large
 *      floating-point inputs down first; keep integer coordinates bounded.
 * @post Result is non-negative and finite.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto length_squared(vector<Value, N> const& v) noexcept -> Value {
  return dot(v, v);
}

/**
 * @brief Euclidean length (L2 norm).
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector.
 *
 * @return \c sqrt(dot(v, v)).
 *
 * @pre Components are finite.
 * @post Result is non-negative and finite.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto length(vector<Real, N> const& v) noexcept -> Real {
  return sqrt(length_squared(v));
}

/**
 * @brief Squared distance between two points.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a First point.
 * @param b Second point.
 *
 * @return \c length_squared(a - b).
 *
 * @pre Components are finite.
 * @post Result is non-negative and finite.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
distance_squared(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> Value {
  return length_squared(a - b);
}

/**
 * @brief Euclidean distance between two points.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param a First point.
 * @param b Second point.
 *
 * @return \c length(a - b).
 *
 * @pre Components are finite.
 * @post Result is non-negative and finite.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
distance(vector<Real, N> const& a, vector<Real, N> const& b) noexcept -> Real {
  return length(a - b);
}

/**
 * @brief 2D pseudo-cross: the signed scalar \c a.x()*b.y() - a.y()*b.x().
 *
 * Equal to the z-component of the 3D cross when both vectors are treated as
 * \c (x, y, 0); it is the signed area of the parallelogram they span. The sign
 * gives orientation: positive when \p b is counter-clockwise from \p a, negative
 * when clockwise, zero when parallel.
 *
 * @tparam Value Component type.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return The scalar cross.
 *
 * @pre Components are finite.
 * @post Result is finite.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto
cross(vector<Value, 2> const a, vector<Value, 2> const b) noexcept -> Value {
  return a.x() * b.y() - a.y() * b.x();
}

/**
 * @brief 3D vector cross product.
 *
 * The vector perpendicular to both inputs, following the right-hand rule, with
 * magnitude \c |a|*|b|*sin(theta) (the area of the parallelogram they span). Each
 * component is a 2x2 determinant of the other two axes, which is what makes the
 * result orthogonal to both operands.
 *
 * @tparam Value Component type.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return A vector perpendicular to both \p a and \p b.
 *
 * @pre Components are finite.
 * @post Result is perpendicular to both \p a and \p b.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto
cross(vector<Value, 3> const& a, vector<Value, 3> const& b) noexcept -> vector<Value, 3> {
  // Each component drops its own axis and takes the 2x2 determinant of the other
  // two, so the result is orthogonal to both inputs (its dot with either is zero).
  return vector<Value, 3>{
    a.y() * b.z() - a.z() * b.y(),
    a.z() * b.x() - a.x() * b.z(),
    a.x() * b.y() - a.y() * b.x(),
  };
}

/**
 * @brief Counter-clockwise perpendicular of a 2D vector.
 *
 * Returns \c (-y, x), a 90 degree counter-clockwise rotation about the origin.
 *
 * @tparam Value Component type.
 * @param v Input vector.
 *
 * @return The counter-clockwise perpendicular.
 *
 * @pre Components are finite.
 * @post Result is perpendicular to \p v and of equal length.
 */
template <arithmetic Value>
[[nodiscard]] constexpr auto perpendicular(vector<Value, 2> const v) noexcept -> vector<Value, 2> {
  return vector<Value, 2>{-v.y(), v.x()};
}

/**
 * @brief Returns \p v scaled to unit length, or an error when it is too short.
 *
 * Compares \c length_squared(v) against \p threshold and, on success, divides by
 * the length. The threshold is on the squared length, so its default of 1e-20
 * corresponds to a length of about 1e-10: short enough to catch a genuine zero
 * vector while not rejecting any meaningful direction. That reasoning is tuned
 * for \c double; for \c float a length near 1e-10 is already at the edge of the
 * type's precision, so pass a larger, type-appropriate \p threshold when
 * normalizing small \c float vectors (and the same magic constant is reused by
 * \c fast_normalize / \c project / \c reject / \c angle_between for the same
 * reason - see here for the rationale).
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector to normalize.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return The unit vector, or \c math_error::zero_length_vector when too short.
 *
 * @pre Components are finite.
 * @post On success the returned vector has unit length.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto normalize(
  vector<Real, N> const& v, Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<vector<Real, N>> {
  auto const len_sq{length_squared(v)};
  if (len_sq <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  return v / sqrt(len_sq);
}

/**
 * @brief Faster but less accurate normalize via \c fast_inv_sqrt.
 *
 * Multiplies by the bit-trick reciprocal square root (see power.hpp) instead of
 * dividing by an exact root, trading about 5e-6 relative accuracy for speed.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector to normalize.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return The approximate unit vector, or \c math_error::zero_length_vector when
 *         too short.
 *
 * @pre Components are finite.
 * @post On success the returned vector has unit length within \c fast_inv_sqrt
 *       accuracy (about 5e-6 relative).
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto fast_normalize(
  vector<Real, N> const& v, Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<vector<Real, N>> {
  auto const len_sq{length_squared(v)};
  if (len_sq <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  return v * fast_inv_sqrt(len_sq);
}

/**
 * @brief Normalize with a caller-supplied fallback for zero-length input.
 *
 * Tighter than \c normalize when the caller has a useful fallback direction (the
 * world up axis, a last-known heading), since it returns a vector rather than a
 * \c result the caller must unwrap.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector to normalize.
 * @param fallback Unit vector returned when \p v is too short.
 * @param threshold Minimum allowed squared length. Default 1e-20.
 *
 * @return \c v/length(v), or \p fallback when \p v is too short.
 *
 * @pre \p fallback has unit length.
 * @post The returned vector has unit length.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto normalize_or(
  vector<Real, N> const& v,
  vector<Real, N> const& fallback,
  Real const threshold = static_cast<Real>(1e-20)
) noexcept -> vector<Real, N> {
  auto const len_sq{length_squared(v)};
  if (len_sq <= threshold) {
    return fallback;
  }
  return v / sqrt(len_sq);
}

/**
 * @brief Element-wise linear interpolation.
 *
 * Applies the scalar \c lerp per component, so it inherits the same endpoint
 * behaviour: exact at \c t=0, and \p b up to rounding (not bit-exact) at
 * \c t=1.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param a Vector returned when \p t is 0.
 * @param b Vector approached when \p t is 1.
 * @param t Interpolation parameter.
 *
 * @return The component-wise lerp.
 *
 * @pre Components and \p t are finite.
 * @post Result is finite.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
lerp(vector<Real, N> const& a, vector<Real, N> const& b, Real const t) noexcept -> vector<Real, N> {
  auto result{vector<Real, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = lerp(a[i], b[i], t);
  }
  return result;
}

/**
 * @brief Element-wise clamp.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to clamp.
 * @param lo Per-component lower bound.
 * @param hi Per-component upper bound.
 *
 * @return The component-wise clamped vector.
 *
 * @pre \c lo[i] is less than or equal to \c hi[i] for every component.
 * @post Result lies element-wise in [lo, hi].
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto clamp(
  vector<Value, N> const& v, vector<Value, N> const& lo, vector<Value, N> const& hi
) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = clamp(v[i], lo[i], hi[i]);
  }
  return result;
}

/**
 * @brief Element-wise minimum of two vectors.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return The component-wise minimum.
 *
 * @pre Components are not NaN.
 * @post Each component of the result is the smaller of the corresponding pair.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
component_min(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = min(a[i], b[i]);
  }
  return result;
}

/**
 * @brief Element-wise maximum of two vectors.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return The component-wise maximum.
 *
 * @pre Components are not NaN.
 * @post Each component of the result is the larger of the corresponding pair.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
component_max(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  auto result{vector<Value, N>{}};
  for (std::size_t i{0}; i < N; ++i) {
    result[i] = max(a[i], b[i]);
  }
  return result;
}

/**
 * @brief Hadamard (element-wise) product, named explicitly.
 *
 * The same as \c operator*(vector, vector) but spelled out for clarity at call
 * sites where a reader might expect a dot or cross product.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 *
 * @return The component-wise product.
 *
 * @pre None.
 * @post Each component equals the product of the corresponding components.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto
hadamard(vector<Value, N> const& a, vector<Value, N> const& b) noexcept -> vector<Value, N> {
  return a * b;
}

/**
 * @brief Projects \p v onto \p onto.
 *
 * The vector component of \p v that lies along \p onto. Dividing \c dot(v, onto)
 * by \c dot(onto, onto) gives the signed length of that component in units of
 * \p onto, which then scales \p onto. \p onto need not be unit length.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector to project.
 * @param onto Vector defining the projection axis.
 * @param threshold Minimum allowed \c dot(onto, onto). Default 1e-20.
 *
 * @return The projection of \p v onto \p onto, or
 *         \c math_error::zero_length_vector when \p onto is too short.
 *
 * @pre Components are finite.
 * @post On success the returned vector is parallel to \p onto.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto project(
  vector<Real, N> const& v,
  vector<Real, N> const& onto,
  Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<vector<Real, N>> {
  auto const denom{dot(onto, onto)};
  if (denom <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  return onto * (dot(v, onto) / denom);
}

/**
 * @brief Rejection: the component of \p v perpendicular to \p onto.
 *
 * Returns \c v - project(v, onto), which leaves only the part of \p v orthogonal
 * to the projection axis. Together \c project and \c reject decompose \p v into
 * its parallel and perpendicular parts.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Vector being decomposed.
 * @param onto Axis to remove the parallel component along.
 * @param threshold Minimum allowed \c dot(onto, onto). Default 1e-20.
 *
 * @return The component of \p v perpendicular to \p onto, or
 *         \c math_error::zero_length_vector when \p onto is too short.
 *
 * @pre Components are finite.
 * @post On success the returned vector is perpendicular to \p onto.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto reject(
  vector<Real, N> const& v,
  vector<Real, N> const& onto,
  Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<vector<Real, N>> {
  auto const proj{project(v, onto, threshold)};
  if (!proj) {
    return std::unexpected{proj.error()};
  }
  // The rejection is orthogonal to `onto` by construction:
  // dot(v - proj, onto) = dot(v, onto) - (dot(v,onto)/dot(onto,onto))*dot(onto,onto)
  // = 0. So v = proj + reject splits v into parallel and perpendicular parts.
  return v - *proj;
}

/**
 * @brief Reflects \p v about a surface with unit normal \p normal.
 *
 * The mirror reflection \c v - 2*dot(v, n)*n. The term \c dot(v, n)*n is the
 * component of \p v along the normal; subtracting it once removes that component
 * (a slide along the surface), subtracting it twice flips it (a bounce), which is
 * the reflection. Length is preserved for a unit normal: with \c d = dot(v, n),
 * \c |v - 2d*n|^2 = |v|^2 - 4d^2 + 4d^2*|n|^2 = |v|^2 when \c |n| = 1. Assumes
 * \p normal is a unit vector.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param v Incoming vector.
 * @param normal Surface normal. Must be unit length.
 *
 * @return The reflected vector.
 *
 * @pre \p normal is a unit vector.
 * @post The returned vector has the same length as \p v when the precondition
 *       holds.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto
reflect(vector<Real, N> const& v, vector<Real, N> const& normal) noexcept -> vector<Real, N> {
  return v - normal * (Real{2} * dot(v, normal));
}

/**
 * @brief Tolerance-aware equality of two vectors.
 *
 * True when every component pair is \c almost_equal (see scalar.hpp) within the
 * given tolerances, the right test for comparing floating-point vectors instead
 * of exact \c ==.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 * @param abs_tol Absolute tolerance per component. Default 1e-6.
 * @param rel_tol Relative tolerance per component. Default 1e-5.
 *
 * @return \c true when all components match within tolerance.
 *
 * @pre Components are finite.
 * @post Returns \c false when any component pair fails the tolerance check.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto almost_equal(
  vector<Real, N> const& a,
  vector<Real, N> const& b,
  Real const abs_tol = static_cast<Real>(1e-6),
  Real const rel_tol = static_cast<Real>(1e-5)
) noexcept -> bool {
  for (std::size_t i{0}; i < N; ++i) {
    if (!almost_equal(a[i], b[i], abs_tol, rel_tol)) {
      return false;
    }
  }
  return true;
}

/**
 * @brief Moves \p current toward \p target by at most \p max_delta units.
 *
 * The vector analogue of the scalar \c move_toward: returns \p target when within
 * \p max_delta, otherwise steps along the direction toward \p target by exactly
 * \p max_delta. Useful for capped-speed approach without overshoot.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param current Current position.
 * @param target Target position.
 * @param max_delta Maximum step magnitude. Must be non-negative.
 *
 * @return The updated position.
 *
 * @pre \p max_delta is finite and non-negative.
 * @post The result is between \p current and \p target inclusive in distance.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] constexpr auto move_toward(
  vector<Real, N> const& current, vector<Real, N> const& target, Real const max_delta
) noexcept -> vector<Real, N> {
  // A non-positive step means "do not move". The squared-distance test below
  // squares away the sign of max_delta, so without this guard a negative step
  // would fall through and move away from the target with a negative scale.
  if (max_delta <= Real{0}) {
    return current;
  }
  // Compare squared distance against squared step to decide whether to snap,
  // which avoids a square root on the common in-range path.
  auto const delta{target - current};
  auto const dist_sq{length_squared(delta)};
  if (dist_sq <= max_delta * max_delta) {
    return target;
  }
  return current + delta * (max_delta / sqrt(dist_sq));
}

/**
 * @brief Sum of all components.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector whose components are summed.
 *
 * @return The arithmetic sum of the \p N components.
 *
 * @pre None.
 * @post Returned value equals the sum of every component of \p v.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto sum(vector<Value, N> const& v) noexcept -> Value {
  auto total{Value{}};
  for (std::size_t i{0}; i < N; ++i) {
    total += v[i];
  }
  return total;
}

/**
 * @brief Product of all components.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector whose components are multiplied.
 *
 * @return The product of the \p N components.
 *
 * @pre None.
 * @post Returned value equals the product of every component of \p v.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto product(vector<Value, N> const& v) noexcept -> Value {
  auto p{Value{1}};
  for (std::size_t i{0}; i < N; ++i) {
    p *= v[i];
  }
  return p;
}

/**
 * @brief Smallest component.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to scan.
 *
 * @return The minimum over the \p N components.
 *
 * @pre None.
 * @post Returned value equals one component of \p v and is no greater than every
 *       other.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto min_component(vector<Value, N> const& v) noexcept -> Value {
  auto best{v[0]};
  for (std::size_t i{1}; i < N; ++i) {
    best = min(best, v[i]);
  }
  return best;
}

/**
 * @brief Largest component.
 *
 * @tparam Value Component type.
 * @tparam N Component count.
 * @param v Vector to scan.
 *
 * @return The maximum over the \p N components.
 *
 * @pre None.
 * @post Returned value equals one component of \p v and is no less than every
 *       other.
 */
template <arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto max_component(vector<Value, N> const& v) noexcept -> Value {
  auto best{v[0]};
  for (std::size_t i{1}; i < N; ++i) {
    best = max(best, v[i]);
  }
  return best;
}

/**
 * @brief Manhattan (L1) length: the sum of absolute component values.
 *
 * The "taxicab" distance, the distance traveled on an axis-aligned grid. Cheaper
 * than the Euclidean length (no products, no root) when a rough magnitude
 * heuristic suffices.
 *
 * @tparam Value Signed arithmetic component type.
 * @tparam N Component count.
 * @param v Vector.
 *
 * @return The sum of the absolute components.
 *
 * @pre None.
 * @post Returned value is non-negative.
 */
template <signed_arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto manhattan_length(vector<Value, N> const& v) noexcept -> Value {
  auto total{Value{}};
  for (std::size_t i{0}; i < N; ++i) {
    total += abs(v[i]);
  }
  return total;
}

/**
 * @brief Chebyshev (L-infinity) length: the largest absolute component.
 *
 * The "chessboard" distance, the number of king moves between two squares. The
 * limiting case of the p-norm as p goes to infinity.
 *
 * @tparam Value Signed arithmetic component type.
 * @tparam N Component count.
 * @param v Vector.
 *
 * @return The largest absolute component.
 *
 * @pre None.
 * @post Returned value is non-negative.
 */
template <signed_arithmetic Value, std::size_t N>
[[nodiscard]] constexpr auto chebyshev_length(vector<Value, N> const& v) noexcept -> Value {
  auto best{Value{}};
  for (std::size_t i{0}; i < N; ++i) {
    auto const m{abs(v[i])};
    if (m > best) {
      best = m;
    }
  }
  return best;
}

/**
 * @brief Unsigned angle between two vectors, in radians.
 *
 * The angle in [0, pi], computed from \c acos(dot(a, b) / (length(a)*length(b))).
 * The cosine is clamped to [-1, 1] before the \c acos: rounding can push it a
 * hair outside that range for nearly parallel inputs, which would make
 * \c std::acos return NaN, so the clamp keeps the result well-defined.
 *
 * @tparam Real Floating-point component type.
 * @tparam N Component count.
 * @param a First vector.
 * @param b Second vector.
 * @param threshold Minimum allowed squared length of each input. Default 1e-20.
 *
 * @return The angle in radians, or \c math_error::zero_length_vector when either
 *         input is too short.
 *
 * @pre Components are finite and each \c length_squared is finite (no overflow,
 *      so per-component magnitudes well below the square root of the type's max).
 * @post On success the result lies in [0, pi].
 *
 * @note Uses libm \c std::acos, so this is not \c constexpr. Wrap the result in
 *       \c radians at the call site if a strong angle type is wanted.
 */
template <std::floating_point Real, std::size_t N>
[[nodiscard]] auto angle_between(
  vector<Real, N> const& a,
  vector<Real, N> const& b,
  Real const threshold = static_cast<Real>(1e-20)
) noexcept -> result<Real> {
  // Guard each squared length separately, not the product. Testing
  // length_squared(a) * length_squared(b) makes the effective per-vector
  // threshold sqrt(threshold), so a genuinely non-zero short vector (length
  // ~1e-7) would be wrongly rejected; and the product can overflow to infinity.
  auto const la{length_squared(a)};
  auto const lb{length_squared(b)};
  if (la <= threshold || lb <= threshold) {
    return std::unexpected{math_error::zero_length_vector};
  }
  auto const denom{sqrt(la) * sqrt(lb)};
  auto const cos_theta{clamp(dot(a, b) / denom, Real{-1}, Real{1})};
  return std::acos(cos_theta);
}

}  // namespace nexenne::math
