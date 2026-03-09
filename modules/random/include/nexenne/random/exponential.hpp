#pragma once

/**
 * @file
 * @brief Exponential distribution sampler via inverse-CDF.
 *
 * Samples \c Exp(lambda), the canonical waiting-time distribution. Use
 * it for inter-event intervals in a Poisson process, packet inter-
 * arrivals, sensor noise modelling, lifetime simulations.
 *
 * Algorithm: \c -log(1 - U) / lambda where \c U ~ Uniform(0, 1). One
 * log call per sample. We use \c -log(U) (which is mathematically
 * equivalent) and skip the subtraction, but only when \c U > 0,
 * since \c log(0) is undefined.
 *
 * @tparam T Floating-point sample type. Default \c double.
 */

#include <cmath>
#include <concepts>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

template <std::floating_point T = double>
class exponential_distribution {
public:
  using value_type = T;

private:
  T m_rate{};

public:
  /**
   * @brief Constructs an exponential distribution with rate \p rate.
   *
   * @param rate Rate parameter lambda; the distribution has mean
   *             \c 1 / rate.
   *
   * @pre \p rate is strictly positive; a non-positive rate makes the
   *       sampled value non-positive or non-finite.
   * @post \c rate() returns \p rate.
   */
  constexpr explicit exponential_distribution(T const rate = T{1}) noexcept : m_rate{rate} {}

  /**
   * @brief Returns the configured rate parameter.
   *
   * @return The rate lambda.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto rate() const noexcept -> T {
    return m_rate;
  }

  /**
   * @brief Draws one sample from the distribution.
   *
   * Inverts the CDF using \c -log(U) / lambda, rejecting the zero
   * uniform so the logarithm stays finite.
   *
   * @tparam Engine Engine type satisfying \c rng_engine.
   * @param engine Engine to draw uniforms from.
   *
   * @return A non-negative exponentially distributed sample.
   *
   * @pre \c rate() is strictly positive.
   * @post The returned value is non-negative; \p engine has advanced.
   *
   * @complexity Expected \c O(1).
   */
  template <rng_engine Engine>
  [[nodiscard]] auto sample(Engine& engine) const noexcept -> T {
    // Draw U in (0, 1] to avoid log(0).
    T u{};
    do {
      u = static_cast<T>(uniform_real(engine));
    } while (u == T{0});
    return -std::log(u) / m_rate;
  }
};

}  // namespace nexenne::random
