/**
 * @file
 * @brief Sampling the continuous and discrete distributions in depth.
 *
 * For each distribution we draw many samples from a fixed seed and check the
 * empirical mean (and, where it is the point, the variance) against the
 * theoretical value - the law of large numbers in action:
 *
 *   1. normal(10, 2): symmetric noise around a centre, the default model for
 *      "value plus measurement error" (ability scores, sensor readings,
 *      jitter). The parameters are the moments: mean 10, variance 2^2 = 4.
 *   2. exponential(4): the waiting time until the next event in a Poisson
 *      process. Mean and stddev both equal 1 / rate, so the variance is the
 *      mean squared (0.25 and 0.0625): a heavy-tailed, memoryless shape.
 *   3. gamma(2, 100): a sum of shape exponentials, strictly positive with an
 *      adjustable skew. Mean shape * scale = 200, variance shape * scale^2 =
 *      20000. Good for gold drops, service times, and Bayesian priors.
 *   4. poisson(3): the discrete partner of the exponential, the count of
 *      events in a unit window. Its defining quirk: mean equals variance, both
 *      lambda.
 *   5. A histogram of exponential(2) samples in [0, 2): the falling staircase
 *      makes "most gaps are short, a few are long" concrete.
 */

#include <array>
#include <cmath>
#include <print>

#include <nexenne/random/exponential.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>

namespace {

namespace rnd = nexenne::random;
constexpr int kN{200000};

/**
 * @brief Empirical mean and variance of \c kN samples, in one pass.
 *
 * Uses the naive two-moment sum: Welford's algorithm would be steadier, but
 * this is plenty for a demo and keeps the intent obvious.
 *
 * @tparam Dist Distribution type with a \c sample(Engine&) member.
 * @tparam Engine Engine type the distribution draws from.
 * @param dist Distribution to sample.
 * @param g Engine to draw from.
 *
 * @return The pair {mean, variance}.
 *
 * @pre None.
 * @post \p g has advanced by \c kN samples' worth of draws.
 */
template <typename Dist, typename Engine>
auto mean_var(Dist& dist, Engine& g) -> std::array<double, 2> {
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{static_cast<double>(dist.sample(g))};
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  return {mean, sum_sq / kN - mean * mean};
}

}  // namespace

auto main() -> int {
  rnd::pcg32 g{7, 1};

  rnd::normal_distribution<double> gaussian{10.0, 2.0};
  auto const [n_mean, n_var]{mean_var(gaussian, g)};
  std::println("normal(10, 2):      mean {:.2f} (~10),  var {:.2f} (~4.0)", n_mean, n_var);

  rnd::exponential_distribution<double> decay{4.0};
  auto const [e_mean, e_var]{mean_var(decay, g)};
  std::println("exponential(4):     mean {:.3f} (~0.250), var {:.4f} (~0.0625)", e_mean, e_var);

  rnd::gamma_distribution<double> payout{2.0, 100.0};
  auto const [g_mean, g_var]{mean_var(payout, g)};
  std::println("gamma(2, 100):      mean {:.1f} (~200),  var {:.0f} (~20000)", g_mean, g_var);

  rnd::poisson_distribution<int> arrivals{3.0};
  auto const [p_mean, p_var]{mean_var(arrivals, g)};
  std::println("poisson(3):         mean {:.2f} (~3.0),  var {:.2f} (~3.0)", p_mean, p_var);

  rnd::exponential_distribution<double> hist_dist{2.0};
  std::array<int, 8> buckets{};
  for (int i{0}; i < kN; ++i) {
    auto const x{hist_dist.sample(g)};
    auto const b{static_cast<std::size_t>(x / 0.25)};
    if (b < buckets.size()) {
      ++buckets[b];
    }
  }
  std::println("exponential(2) histogram (bucket width 0.25):");
  for (std::size_t b{0}; b < buckets.size(); ++b) {
    auto const bars{buckets[b] * 50 / kN};
    auto const lo{static_cast<double>(b) * 0.25};
    std::print("  [{:.2f},{:.2f}) ", lo, lo + 0.25);
    for (int j{0}; j < bars; ++j) {
      std::print("#");
    }
    std::println("");
  }
  return 0;
}
