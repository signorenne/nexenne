#pragma once

/**
 * @file
 * @brief Poisson distribution sampler.
 *
 * Returns the number of events occurring in a unit interval given a
 * mean event count \c lambda. For small \c lambda uses Knuth's product-of-
 * uniforms algorithm (no library calls beyond \c exp on construction).
 * For large \c lambda (>= 30) the simple algorithm becomes slow; falls back
 * to a normal-approximation-with-correction (good enough for most
 * embedded uses, for high-quality sampling at large lambda, use a
 * rejection-based algorithm like \c PA from Hormann).
 *
 * @tparam T Result integer type. Default \c std::uint32_t.
 */

#include <cmath>
#include <concepts>
#include <cstdint>
#include <limits>

#include <nexenne/random/uniform.hpp>

namespace nexenne::random {

template <std::integral T = std::uint32_t>
class poisson_distribution {
public:
  using value_type = T;

private:
  double m_lambda{0.0};
  double m_exp_neg_lambda{0.0};  ///< precomputed exp(-lambda) for the small-lambda path

public:
  /**
   * @brief Constructs a Poisson distribution with mean \p lambda.
   *
   * Precomputes \c exp(-lambda) for the small-mean Knuth path; the
   * factor is left at zero when \p lambda selects the large-mean
   * normal-approximation path.
   *
   * @param lambda Mean event count over the unit interval.
   *
   * @pre \p lambda is non-negative and finite.
   * @post \c mean() returns \p lambda.
   */
  constexpr explicit poisson_distribution(double const lambda = 1.0) noexcept
      : m_lambda{lambda}, m_exp_neg_lambda{lambda < 30.0 ? std::exp(-lambda) : 0.0} {}

  /**
   * @brief Returns the configured mean.
   *
   * @return The mean event count lambda.
   *
   * @pre None.
   * @post None.
   */
  [[nodiscard]] constexpr auto mean() const noexcept -> double {
    return m_lambda;
  }

  /**
   * @brief Draws one sample from the distribution.
   *
   * For a mean below \c 30 uses Knuth's product-of-uniforms method;
   * for larger means it uses a normal approximation rounded to the
   * nearest non-negative integer.
   *
   * @tparam Engine Engine type satisfying \c rng_engine.
   * @param engine Engine to draw uniforms from.
   *
   * @return A non-negative event count.
   *
   * @pre \c mean() is non-negative and finite.
   * @post The returned value is non-negative; \p engine has advanced.
   *
   * @complexity Expected \c O(mean) on the small-mean path, \c O(1) on
   *             the large-mean path.
   */
  template <rng_engine Engine>
  [[nodiscard]] auto sample(Engine& engine) const noexcept -> T {
    if (m_lambda < 30.0) {
      // Knuth's algorithm: multiply U[0,1) until product < exp(-lambda).
      double product{1.0};
      T k{0};
      while (true) {
        product *= uniform_real(engine);
        if (product < m_exp_neg_lambda) {
          return k;
        }
        ++k;
      }
    }
    // Normal approximation for large lambda via Box-Muller. Round to integer.
    auto const u1{uniform_real(engine)};
    auto const u2{uniform_real(engine)};
    auto const z{
      std::sqrt(-2.0 * std::log(u1 == 0.0 ? 1e-300 : u1)) * std::cos(6.283185307179586 * u2)
    };
    auto const x{m_lambda + std::sqrt(m_lambda) * z};
    if (x <= 0.0) {
      return T{0};
    }
    // Saturate before the cast: converting a double beyond T's range is
    // undefined behaviour, so clamp a value that overflows T to its maximum
    // rather than letting it wrap or trap.
    auto const rounded{x + 0.5};
    if (rounded >= static_cast<double>(std::numeric_limits<T>::max())) {
      return std::numeric_limits<T>::max();
    }
    return static_cast<T>(rounded);
  }
};

}  // namespace nexenne::random
