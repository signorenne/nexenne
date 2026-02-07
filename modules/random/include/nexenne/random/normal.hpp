#pragma once

/**
 * @file
 * @brief Gaussian (normal) distribution sampler via Box-Muller.
 *
 * Generates samples from \c N(mu, sigma^2) using the trigonometric
 * (Cartesian) form of the Box-Muller transform. Each transform produces
 * two standard-normal samples from two uniform samples; the
 * second is cached and returned on the next call, halving the
 * per-sample arithmetic cost.
 *
 * \code
 * auto rng{rnd::pcg32{42, 1}};
 * auto dist{rnd::normal_distribution<double>{0.0, 1.0}};
 *
 * for (auto i{0}; i < 1000; ++i) {
 *     auto const x{dist.sample(rng)};   // ~ N(0, 1)
 * }
 * \endcode
 *
 * Why not \c std::normal_distribution? Because it does not even
 * pin the algorithm: its output is implementation-defined across
 * \c libstdc++ / \c libc++ / \c MSVC, so the same engine and seed
 * produce different sequences on different toolchains. This type
 * pins the algorithm, so the exact sequence of operations is fixed
 * everywhere and the stream is bit-identical on a given platform.
 *
 * \note Cross-platform bit-identity additionally depends on the C
 *       library: the transform calls \c std::log / \c std::sqrt /
 *       \c std::cos, whose last-ULP results can differ between libm
 *       implementations and versions. The same caveat applies to the
 *       other transcendental-based distributions (gamma, exponential,
 *       poisson). The engines and the integer/real uniform draws are
 *       bit-identical everywhere; these samplers are not guaranteed to
 *       be to the last bit across toolchains.
 *
 * \warning Not cryptographically secure (the underlying
 *          engine isn't, and the trigonometric path doesn't
 *          change that).
 *
 * @tparam T Floating-point sample type. Default \c double.
 */

#include <cmath>
#include <concepts>
#include <limits>
#include <numbers>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

template <std::floating_point T = double>
class normal_distribution {
public:
  using value_type = T;

private:
  T m_mean{};
  T m_stddev{};
  T m_cached{T{0}};
  bool m_has_cached{false};

public:
  /**
   * @brief Constructs a normal distribution with the given parameters.
   *
   * A negative standard deviation is folded to its absolute value so
   * the parameter is always valid.
   *
   * @param mean Centre of the distribution.
   * @param stddev Standard deviation; the magnitude is used.
   *
   * @pre None. A negative \p stddev is stored as its absolute value.
   * @post \c mean() returns \p mean, \c stddev() returns the magnitude
   *       of \p stddev, and no Box-Muller value is cached.
   */
  constexpr normal_distribution(T const mean = T{0}, T const stddev = T{1}) noexcept
      : m_mean{mean}, m_stddev{stddev < T{0} ? -stddev : stddev} {}

  /**
   * @brief Draws one sample from the distribution.
   *
   * Returns the cached second variate of the previous Box-Muller pair
   * when available, otherwise generates a fresh pair and caches the
   * second value for the next call.
   *
   * @tparam G Engine type satisfying \c rng_engine.
   * @param g Engine to draw uniforms from.
   *
   * @return A normally distributed sample with the configured mean and
   *         standard deviation.
   *
   * @pre None.
   * @post \p g has advanced; a second variate may now be cached.
   *
   * @complexity Amortised \c O(1); the trigonometric transform runs on
   *             every other call.
   */
  template <rng_engine G>
  [[nodiscard]] auto sample(G& g) noexcept -> T {
    if (m_has_cached) {
      m_has_cached = false;
      return m_mean + m_stddev * m_cached;
    }
    // Box-Muller (trigonometric form). \c u1 must be > 0
    // since we take its log; the engine virtually never
    // returns exactly zero, but guard anyway.
    auto u1{uniform_real(g)};
    if (u1 == 0.0) {
      u1 = std::numeric_limits<double>::min();
    }
    auto const u2{uniform_real(g)};
    auto const r{std::sqrt(static_cast<T>(-2.0 * std::log(u1)))};
    auto const theta{static_cast<T>(2.0 * std::numbers::pi_v<double> * u2)};
    auto const z0{r * std::cos(theta)};
    auto const z1{r * std::sin(theta)};
    m_cached = z1;
    m_has_cached = true;
    return m_mean + m_stddev * z0;
  }

  /**
   * @brief Returns the configured mean.
   *
   * @return The centre of the distribution.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mean() const noexcept -> T {
    return m_mean;
  }

  /**
   * @brief Returns the configured standard deviation.
   *
   * @return The non-negative standard deviation.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto stddev() const noexcept -> T {
    return m_stddev;
  }

  /**
   * @brief Discards any cached Box-Muller variate.
   *
   * Call after re-seeding the underlying engine so the next \c sample
   * starts a fresh pair rather than returning a stale cached value.
   *
   * @pre None.
   * @post No second variate is cached; the next \c sample regenerates
   *       a pair.
   */
  constexpr auto reset() noexcept -> void {
    m_cached = T{0};
    m_has_cached = false;
  }
};

/**
 * @brief Draws one normal sample without a distribution object.
 *
 * A stateless convenience wrapper around the Box-Muller transform; the
 * pair's second variate is discarded, so it does roughly twice the work
 * of \c normal_distribution::sample when many samples are needed.
 *
 * @tparam T Floating-point sample type.
 * @tparam G Engine type satisfying \c rng_engine.
 * @param g Engine to draw uniforms from.
 * @param mean Centre of the distribution.
 * @param stddev Standard deviation.
 *
 * @return A normally distributed sample.
 *
 * @pre \p stddev is non-negative for the result to have the intended
 *       sign convention.
 * @post \p g has advanced.
 *
 * @complexity \c O(1).
 */
template <std::floating_point T = double, rng_engine G>
[[nodiscard]] auto normal(G& g, T const mean = T{0}, T const stddev = T{1}) noexcept -> T {
  auto u1{uniform_real(g)};
  if (u1 == 0.0) {
    u1 = std::numeric_limits<double>::min();
  }
  auto const u2{uniform_real(g)};
  auto const z{
    std::sqrt(static_cast<T>(-2.0 * std::log(u1)))
    * std::cos(static_cast<T>(2.0 * std::numbers::pi_v<double> * u2))
  };
  return mean + stddev * z;
}

}  // namespace nexenne::random
