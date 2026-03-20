#pragma once

/**
 * @file
 * @brief Geometric samplers layered on the nexenne::random engines.
 *
 * The random-sampling operations that need math types (\c vector, \c radians)
 * and so cannot live in the engine-agnostic nexenne::random module. Scalar
 * sampling (uniform int/real, Bernoulli, normal) belongs to nexenne::random;
 * this header reuses those primitives rather than re-deriving them.
 *
 * Every sampler is parameterized on the caller's generator, so a
 * \c random::pcg32, \c random::xoshiro256ss, or any other type satisfying
 * \c random::rng_engine can drive them. The generator owns the sequence state
 * and is advanced in place.
 */

#include <concepts>

#include <nexenne/math/angle.hpp>
#include <nexenne/math/constants.hpp>
#include <nexenne/math/power.hpp>
#include <nexenne/math/trigonometry.hpp>
#include <nexenne/math/vector.hpp>
#include <nexenne/random/uniform.hpp>

namespace nexenne::math {

namespace detail {

/**
 * @brief Uniform \c Real in [lo, hi), built on \c random::uniform_real.
 *
 * Affinely remaps the engine's canonical [0, 1) draw onto [lo, hi). Reuses the
 * module's one real draw rather than re-deriving a ranged generator.
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 * @param lo Lower bound, inclusive.
 * @param hi Upper bound, exclusive.
 *
 * @return A uniform \c Real in [lo, hi).
 *
 * @pre \p lo is less than \p hi.
 * @post The result lies in [lo, hi); \p g has advanced.
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] constexpr auto uniform_real_in(G& g, Real const lo, Real const hi) noexcept -> Real {
  auto const result{lo + (hi - lo) * static_cast<Real>(random::uniform_real(g))};
  // Keep the interval half-open. uniform_real draws in [0, 1) as a double; when
  // Real is narrower (float), rounding the product back to Real can land exactly
  // on hi (the largest double below 1 rounds up to 1.0f). Map that excluded
  // endpoint back to lo, which preserves [lo, hi) at a negligible (~2^-24) bias
  // and is exactly right for cyclic ranges like [-pi, pi) where hi == -lo wraps.
  return result < hi ? result : lo;
}

}  // namespace detail

/**
 * @brief Uniform random 2D unit vector.
 *
 * Samples a direction angle uniformly over a full turn and returns its point on
 * the unit circle. \c sincos gives both components from one trig evaluation.
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 *
 * @return A unit-length 2D vector in a uniformly distributed direction.
 *
 * @pre None.
 * @post The result has unit length.
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] auto unit_vector2(G& g) noexcept -> vector<Real, 2> {
  auto const theta{detail::uniform_real_in<Real>(g, Real{0}, tau_v<Real>)};
  auto const sc{sincos(radians<Real>{theta})};
  return vector<Real, 2>{sc.cos(), sc.sin()};
}

/**
 * @brief Uniform random 3D unit vector via Marsaglia's method.
 *
 * Marsaglia (1972): rejection-sample a point (a, b) uniformly in the unit disc,
 * let s = a^2 + b^2, and map to
 * (2a*sqrt(1-s), 2b*sqrt(1-s), 1-2s). This is exactly uniform on the sphere.
 * Why: by Archimedes' hat-box theorem a uniform sphere has its z coordinate
 * uniform on [-1, 1] and its azimuth uniform; for a uniform disc point s is
 * uniform on [0, 1) (disc area grows as the squared radius), so z = 1-2s is the
 * required uniform z, and since 1 - z^2 = 4s(1-s) the in-plane radius
 * sqrt(1-z^2) = 2*sqrt(s(1-s)) reproduces the disc point scaled onto the
 * circle of latitude. Only two draws and one sqrt per accepted sample, no
 * trigonometry. Acceptance probability pi/4, so ~1.27 attempts on average.
 * https://projecteuclid.org/euclid.aoms/1177692644
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 *
 * @return A unit-length 3D vector uniformly distributed on the sphere.
 *
 * @pre None.
 * @post The result has unit length.
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] auto unit_vector3(G& g) noexcept -> vector<Real, 3> {
  while (true) {
    auto const a{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    auto const b{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    auto const s{a * a + b * b};
    if (s < Real{1}) {
      auto const factor{Real{2} * sqrt(Real{1} - s)};
      return vector<Real, 3>{a * factor, b * factor, Real{1} - Real{2} * s};
    }
  }
}

/**
 * @brief Uniform random point inside the unit disc.
 *
 * Rejection sampling: draw a point in the bounding square and keep it only when
 * it falls inside the disc. This avoids the clustering near the center that a
 * naive (radius, angle) sample would produce, and costs ~1.27 attempts on
 * average (the square-to-disc area ratio 4/pi).
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 *
 * @return A point with squared length strictly less than 1.
 *
 * @pre None.
 * @post The result has squared length strictly less than 1.
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] auto point_in_unit_disc(G& g) noexcept -> vector<Real, 2> {
  while (true) {
    auto const x{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    auto const y{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    if (x * x + y * y < Real{1}) {
      return vector<Real, 2>{x, y};
    }
  }
}

/**
 * @brief Uniform random point inside the unit ball (3D).
 *
 * Rejection sampling against the bounding cube, the 3D analogue of
 * \c point_in_unit_disc. The cube-to-ball acceptance is pi/6 (~52%), so ~1.91
 * attempts on average - still cheaper than the alternatives for three
 * dimensions.
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 *
 * @return A point with squared length strictly less than 1.
 *
 * @pre None.
 * @post The result has squared length strictly less than 1.
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] auto point_in_unit_ball(G& g) noexcept -> vector<Real, 3> {
  while (true) {
    auto const x{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    auto const y{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    auto const z{detail::uniform_real_in<Real>(g, Real{-1}, Real{1})};
    if (x * x + y * y + z * z < Real{1}) {
      return vector<Real, 3>{x, y, z};
    }
  }
}

/**
 * @brief Uniform random angle in [-pi, pi).
 *
 * Sampled directly in range (no wrapping needed), as a strong \c radians.
 *
 * @tparam Real Floating-point component type.
 * @tparam G Generator type satisfying \c random::rng_engine.
 * @param g Generator to advance.
 *
 * @return An angle uniformly distributed over [-pi, pi).
 *
 * @pre None.
 * @post The result lies in [-pi, pi).
 */
template <std::floating_point Real, random::rng_engine G>
[[nodiscard]] auto random_angle(G& g) noexcept -> radians<Real> {
  return radians<Real>{detail::uniform_real_in<Real>(g, -pi_v<Real>, pi_v<Real>)};
}

}  // namespace nexenne::math
