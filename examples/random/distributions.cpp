/**
 * @file
 * @brief Sampling continuous and discrete distributions.
 *
 * Draw many samples from each distribution with a fixed seed and report the
 * empirical mean, which converges to the theoretical mean.
 */

#include <print>

#include <nexenne/random/exponential.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>

namespace {

namespace rnd = nexenne::random;
constexpr int kN{100000};

template <typename Dist, typename Engine>
auto empirical_mean(Dist& dist, Engine& g) -> double {
  double sum{0.0};
  for (int i{0}; i < kN; ++i) {
    sum += static_cast<double>(dist.sample(g));
  }
  return sum / kN;
}

}  // namespace

auto main() -> int {
  rnd::pcg32 g{7, 1};

  rnd::normal_distribution<double> gaussian{10.0, 2.0};  // mean 10
  rnd::exponential_distribution<double> decay{4.0};      // mean 1/4 = 0.25
  rnd::poisson_distribution<int> arrivals{3.0};          // mean 3

  std::println("normal(10, 2) empirical mean: {:.2f}", empirical_mean(gaussian, g));
  std::println("exponential(rate=4) empirical mean: {:.3f}", empirical_mean(decay, g));
  std::println("poisson(3) empirical mean: {:.2f}", empirical_mean(arrivals, g));
  // normal(10, 2) empirical mean: ~10.0
  // exponential(rate=4) empirical mean: ~0.250
  // poisson(3) empirical mean: ~3.0
  return 0;
}
