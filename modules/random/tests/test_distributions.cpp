/**
 * @file
 * @brief Tests for nexenne::random distributions (statistical-mean checks).
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include <nexenne/random/discrete.hpp>
#include <nexenne/random/exponential.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>

namespace {

namespace rnd = nexenne::random;
constexpr int kN{200000};

TEST_CASE("nexenne::random::normal_distribution matches its mean and stddev") {
  rnd::pcg32 g{11, 1};
  rnd::normal_distribution<double> dist{2.0, 3.0};
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(2.0).epsilon(0.03));
  CHECK(std::sqrt(var) == doctest::Approx(3.0).epsilon(0.03));
}

TEST_CASE("nexenne::random::exponential_distribution is non-negative with mean 1/rate") {
  rnd::pcg32 g{13, 1};
  rnd::exponential_distribution<double> dist{2.0};  // mean 0.5
  double sum{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
  }
  CHECK(sum / kN == doctest::Approx(0.5).epsilon(0.03));
}

TEST_CASE("nexenne::random::gamma_distribution has mean shape*scale (alpha>=1 and alpha<1)") {
  rnd::pcg32 g{17, 1};
  rnd::gamma_distribution<double> big{2.0, 1.5};  // mean 3.0
  double sum{0.0};
  for (int i{0}; i < kN; ++i) {
    sum += big.sample(g);
  }
  CHECK(sum / kN == doctest::Approx(3.0).epsilon(0.04));

  rnd::gamma_distribution<double> small{0.5, 2.0};  // mean 1.0, exercises the boost path
  double sum2{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{small.sample(g)};
    REQUIRE(x >= 0.0);
    sum2 += x;
  }
  CHECK(sum2 / kN == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("nexenne::random::poisson_distribution has mean lambda (small and large)") {
  rnd::pcg32 g{19, 1};
  rnd::poisson_distribution<int> small{4.0};  // Knuth path
  long long sum{0};
  for (int i{0}; i < kN; ++i) {
    auto const k{small.sample(g)};
    REQUIRE(k >= 0);
    sum += k;
  }
  CHECK(static_cast<double>(sum) / kN == doctest::Approx(4.0).epsilon(0.03));

  rnd::poisson_distribution<int> big{50.0};  // normal-approximation path
  long long sum2{0};
  for (int i{0}; i < kN; ++i) {
    sum2 += big.sample(g);
  }
  CHECK(static_cast<double>(sum2) / kN == doctest::Approx(50.0).epsilon(0.03));
}

TEST_CASE("nexenne::random::poisson_distribution saturates instead of overflowing the result type"
) {
  rnd::pcg32 g{5, 1};
  // A mean far beyond what the result type can hold must clamp to the type
  // maximum, never wrap or invoke a float-to-integer-overflow UB.
  rnd::poisson_distribution<std::uint16_t> small_type{1.0e6};
  for (int i{0}; i < 1000; ++i) {
    auto const k{small_type.sample(g)};
    CHECK(k <= std::numeric_limits<std::uint16_t>::max());
  }
  CHECK(small_type.sample(g) == std::numeric_limits<std::uint16_t>::max());
}

TEST_CASE("nexenne::random::discrete_distribution selects in proportion to weights") {
  rnd::pcg32 g{23, 1};
  std::array<double, 3> const weights{1.0, 3.0, 6.0};  // probabilities 0.1, 0.3, 0.6
  rnd::discrete_distribution<double> dist{weights};
  std::array<int, 3> hits{};
  for (int i{0}; i < kN; ++i) {
    auto const idx{dist.sample(g)};
    REQUIRE(idx < 3);
    ++hits[idx];
  }
  CHECK(static_cast<double>(hits[0]) / kN == doctest::Approx(0.1).epsilon(0.05));
  CHECK(static_cast<double>(hits[1]) / kN == doctest::Approx(0.3).epsilon(0.05));
  CHECK(static_cast<double>(hits[2]) / kN == doctest::Approx(0.6).epsilon(0.05));
}

TEST_CASE("nexenne::random::discrete_distribution never selects a zero-weight outcome") {
  rnd::pcg32 g{7, 1};
  // Leading and interior zero weights: outcomes 0 and 2 are impossible.
  // The boundary case (uniform_real == 0.0 maps target to exactly 0.0) must
  // resolve to the first POSITIVE-weight outcome, not the zero-weight one.
  std::array<double, 4> const weights{0.0, 3.0, 0.0, 1.0};
  rnd::discrete_distribution<double> dist{weights};
  std::array<int, 4> hits{};
  for (int i{0}; i < kN; ++i) {
    auto const idx{dist.sample(g)};
    REQUIRE(idx < 4);
    ++hits[idx];
  }
  CHECK(hits[0] == 0);  // zero-weight leading outcome never chosen
  CHECK(hits[2] == 0);  // zero-weight interior outcome never chosen
  CHECK(hits[1] > 0);
  CHECK(hits[3] > 0);
  CHECK(static_cast<double>(hits[1]) / kN == doctest::Approx(0.75).epsilon(0.05));
}

TEST_CASE("nexenne::random::discrete_distribution reports sentinel for degenerate inputs") {
  rnd::pcg32 g{3, 1};
  rnd::discrete_distribution<double> const empty{std::array<double, 0>{}};
  CHECK(empty.sample(g) == empty.size());  // empty -> sentinel
  CHECK(empty.size() == 0);

  std::array<double, 3> const zeros{0.0, -2.0, 0.0};  // all non-positive
  rnd::discrete_distribution<double> const dead{zeros};
  CHECK(dead.total_weight() == 0.0);
  CHECK(dead.sample(g) == dead.size());  // no outcome possible -> sentinel (== 3)
}

}  // namespace
