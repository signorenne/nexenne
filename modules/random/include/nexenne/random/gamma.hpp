#pragma once

/**
 * @file
 * @brief Gamma distribution sampler (Marsaglia-Tsang).
 *
 * Generates samples from \c Gamma(alpha, theta) where alpha is shape and theta is
 * scale (so mean = alpha*theta, variance = alpha*theta^2). Used directly for waiting
 * times in compound Poisson processes, prior distributions in
 * Bayesian inference, and as a building block for beta / chi-square
 * sampling.
 *
 * Algorithm: Marsaglia & Tsang (2000) "A Simple Method for Generating
 * Gamma Variables" - fast, no special functions in the hot loop, valid
 * for alpha >= 1. For alpha < 1 we use Marsaglia's boost: sample with alpha + 1
 * and apply the power transform.
 *
 * @tparam T Floating-point sample type. Default \c double.
 */

#include <cmath>
#include <concepts>
#include <numbers>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

template <std::floating_point T = double>
class gamma_distribution {
public:
  using value_type = T;

private:
  T m_shape{};
  T m_scale{};
  // Marsaglia-Tsang constants for the effective alpha (shape for shape >= 1, or
  // shape + 1 for the boost path), precomputed once so the per-sample loop avoids
  // recomputing them and their sqrt on every draw.
  T m_d{};
  T m_c{};

  template <rng_engine Engine>
  [[nodiscard]] static auto sample_unit_normal(Engine& engine) noexcept -> T {
    // Box-Muller (one variate per call, wastes the second).
    T u1{};
    do {
      u1 = static_cast<T>(uniform_real(engine));
    } while (u1 == T{0});
    auto const u2{static_cast<T>(uniform_real(engine))};
    constexpr T two_pi{T{2} * std::numbers::pi_v<T>};
    return std::sqrt(T{-2} * std::log(u1)) * std::cos(two_pi * u2);
  }

  template <rng_engine Engine>
  [[nodiscard]] auto sample_marsaglia_tsang(Engine& engine) const noexcept -> T {
    // Marsaglia-Tsang for the cached effective alpha (>= 1).
    while (true) {
      T x{};
      T v{};
      do {
        x = sample_unit_normal(engine);
        v = T{1} + m_c * x;
      } while (v <= T{0});
      v = v * v * v;
      auto const u{static_cast<T>(uniform_real(engine))};
      auto const x2{x * x};
      if (u < T{1} - static_cast<T>(0.0331) * x2 * x2) {
        return m_d * v;
      }
      if (std::log(u) < T{0.5} * x2 + m_d * (T{1} - v + std::log(v))) {
        return m_d * v;
      }
    }
  }

public:
  /**
   * @brief Constructs a gamma distribution from shape and scale.
   *
   * @param shape Shape parameter alpha; controls the distribution's
   *              skew. The mean is \c shape * scale.
   * @param scale Scale parameter theta; stretches the distribution.
   *
   * @pre \p shape and \p scale are strictly positive.
   * @post \c shape() returns \p shape and \c scale() returns \p scale.
   */
  explicit gamma_distribution(T const shape = T{1}, T const scale = T{1}) noexcept
      : m_shape{shape}, m_scale{scale} {
    // The Marsaglia-Tsang core always runs at an alpha of at least one: shape
    // itself when shape >= 1, or shape + 1 for the boost path used below it.
    auto const effective_alpha{shape >= T{1} ? shape : shape + T{1}};
    m_d = effective_alpha - T{1} / T{3};
    m_c = T{1} / std::sqrt(T{9} * m_d);
  }

  /**
   * @brief Returns the configured shape parameter.
   *
   * @return The shape alpha.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto shape() const noexcept -> T {
    return m_shape;
  }

  /**
   * @brief Returns the configured scale parameter.
   *
   * @return The scale theta.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto scale() const noexcept -> T {
    return m_scale;
  }

  /**
   * @brief Draws one sample from the distribution.
   *
   * Applies the Marsaglia-Tsang squeeze for shape at least one. For a
   * smaller shape it samples with shape plus one and applies the
   * power-transform boost, then multiplies by the scale.
   *
   * @tparam Engine Engine type satisfying \c rng_engine.
   * @param engine Engine to draw uniforms from.
   *
   * @return A non-negative gamma-distributed sample.
   *
   * @pre \c shape() and \c scale() are strictly positive.
   * @post The returned value is non-negative; \p engine has advanced.
   *
   * @complexity Expected \c O(1); the acceptance loop rarely rejects.
   */
  template <rng_engine Engine>
  [[nodiscard]] auto sample(Engine& engine) const noexcept -> T {
    if (m_shape >= T{1}) {
      return m_scale * sample_marsaglia_tsang(engine);
    }
    // Boost: sample Gamma(alpha+1, 1) * U^(1/alpha), then scale. The cached
    // m_d/m_c were computed for the effective alpha = shape + 1.
    auto const g{sample_marsaglia_tsang(engine)};
    T u{};
    do {
      u = static_cast<T>(uniform_real(engine));
    } while (u == T{0});
    return m_scale * g * std::pow(u, T{1} / m_shape);
  }
};

}  // namespace nexenne::random
