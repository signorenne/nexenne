/**
 * @file
 * @brief Sampling the continuous and discrete distributions in depth.
 *
 * For each distribution we draw many samples from a fixed seed and check the
 * empirical mean (and, where it is the point, the variance) against the
 * theoretical value - the law of large numbers in action. Covered here:
 * normal, exponential, gamma, and poisson, with a note on *why* each is the
 * right shape and how its parameters map to its moments. A tiny histogram makes
 * the exponential's decay visible.
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

// Empirical mean and variance in one pass (Welford would be steadier, but the
// naive two-moment sum is plenty for a demo and keeps the intent obvious).
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

  // --- Normal: symmetric noise around a centre --------------------------------
  // The default model for "value plus measurement error": ability scores, sensor
  // readings, jitter. Parameters ARE the moments: mean and variance = stddev^2.
  rnd::normal_distribution<double> gaussian{10.0, 2.0};  // mean 10, variance 4
  auto const [n_mean, n_var]{mean_var(gaussian, g)};
  std::println("normal(10, 2):      mean {:.2f} (~10),  var {:.2f} (~4.0)", n_mean, n_var);

  // --- Exponential: waiting time, memoryless ----------------------------------
  // Time until the next event in a Poisson process. Mean and stddev both equal
  // 1/rate, so the variance is mean^2 - a heavy-tailed, memoryless shape.
  rnd::exponential_distribution<double> decay{4.0};  // mean 1/4 = 0.25
  auto const [e_mean, e_var]{mean_var(decay, g)};
  std::println("exponential(4):     mean {:.3f} (~0.250), var {:.4f} (~0.0625)", e_mean, e_var);

  // --- Gamma: positive, right-skewed payout -----------------------------------
  // A sum of `shape` exponentials: strictly positive with an adjustable skew.
  // Mean = shape*scale, variance = shape*scale^2. Good for gold drops, service
  // times, Bayesian priors - anything positive with a typical value and a tail.
  rnd::gamma_distribution<double> payout{2.0, 100.0};  // mean 200, var 20000
  auto const [g_mean, g_var]{mean_var(payout, g)};
  std::println("gamma(2, 100):      mean {:.1f} (~200),  var {:.0f} (~20000)", g_mean, g_var);

  // --- Poisson: count of events in an interval --------------------------------
  // The discrete partner of the exponential: how many events land in a unit
  // window. Its defining quirk - mean equals variance, both equal lambda.
  rnd::poisson_distribution<int> arrivals{3.0};  // mean 3, var 3
  auto const [p_mean, p_var]{mean_var(arrivals, g)};
  std::println("poisson(3):         mean {:.2f} (~3.0),  var {:.2f} (~3.0)", p_mean, p_var);

  // --- A histogram of the exponential's decay ---------------------------------
  // Bucket samples in [0, 2) and print a bar per bucket. The classic falling
  // staircase makes the "most gaps are short, a few are long" shape concrete.
  rnd::exponential_distribution<double> hist_dist{2.0};  // mean 0.5
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
  // normal(10, 2):      mean ~10.0, var ~4.0
  // exponential(4):     mean ~0.250, var ~0.0625
  // gamma(2, 100):      mean ~200, var ~20000
  // poisson(3):         mean ~3.0, var ~3.0
  return 0;
}
