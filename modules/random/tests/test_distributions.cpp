/**
 * @file
 * @brief Tests for nexenne::random distributions (statistical-mean checks).
 */

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <ranges>
#include <type_traits>
#include <utility>
#include <vector>

#include <nexenne/random/discrete.hpp>
#include <nexenne/random/exponential.hpp>
#include <nexenne/random/gamma.hpp>
#include <nexenne/random/normal.hpp>
#include <nexenne/random/pcg.hpp>
#include <nexenne/random/poisson.hpp>
#include <nexenne/utility/ignore.hpp>

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

TEST_CASE("nexenne::random::normal_distribution param accessors and stddev folding") {
  rnd::normal_distribution<double> const dist{2.0, 3.0};
  CHECK(dist.mean() == doctest::Approx(2.0));
  CHECK(dist.stddev() == doctest::Approx(3.0));

  rnd::normal_distribution<double> const folded{-1.5, -4.0};
  CHECK(folded.mean() == doctest::Approx(-1.5));
  CHECK(folded.stddev() == doctest::Approx(4.0));

  rnd::normal_distribution<double> const standard{};
  CHECK(standard.mean() == doctest::Approx(0.0));
  CHECK(standard.stddev() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::random::normal_distribution empirical 68/95 rule") {
  rnd::pcg32 g{101, 1};
  rnd::normal_distribution<double> dist{0.0, 1.0};
  int within_1{0};
  int within_2{0};
  for (int i{0}; i < kN; ++i) {
    auto const z{std::fabs(dist.sample(g))};
    if (z <= 1.0) {
      ++within_1;
    }
    if (z <= 2.0) {
      ++within_2;
    }
  }
  CHECK(static_cast<double>(within_1) / kN == doctest::Approx(0.6827).epsilon(0.02));
  CHECK(static_cast<double>(within_2) / kN == doctest::Approx(0.9545).epsilon(0.01));
}

TEST_CASE("nexenne::random::normal_distribution handles negative mean and extreme sigma") {
  {
    rnd::pcg32 g{202, 1};
    rnd::normal_distribution<double> dist{-50.0, 25.0};
    double sum{0.0};
    double sum_sq{0.0};
    for (int i{0}; i < kN; ++i) {
      auto const x{dist.sample(g)};
      sum += x;
      sum_sq += x * x;
    }
    auto const mean{sum / kN};
    auto const var{sum_sq / kN - mean * mean};
    CHECK(mean == doctest::Approx(-50.0).epsilon(0.03));
    CHECK(std::sqrt(var) == doctest::Approx(25.0).epsilon(0.03));
  }
  {
    rnd::pcg32 g{203, 1};
    rnd::normal_distribution<double> dist{7.0, 1.0e-6};
    double sum{0.0};
    for (int i{0}; i < kN; ++i) {
      auto const x{dist.sample(g)};
      sum += x;
      REQUIRE(std::fabs(x - 7.0) < 1.0e-4);
    }
    CHECK(sum / kN == doctest::Approx(7.0).epsilon(1.0e-6));
  }
}

TEST_CASE("nexenne::random::normal_distribution sigma=0 returns the mean exactly") {
  rnd::pcg32 g{204, 1};
  rnd::normal_distribution<double> dist{3.5, 0.0};
  for (int i{0}; i < 1000; ++i) {
    CHECK(dist.sample(g) == doctest::Approx(3.5));
  }
}

TEST_CASE("nexenne::random::normal_distribution reset discards the cached variate") {
  rnd::pcg32 g_warm{305, 1};
  rnd::normal_distribution<double> dist_a{0.0, 1.0};
  nexenne::utility::ignore(dist_a.sample(g_warm));
  dist_a.reset();

  rnd::normal_distribution<double> dist_b{0.0, 1.0};
  rnd::pcg32 g_a{g_warm};
  rnd::pcg32 g_b{g_warm};
  for (int i{0}; i < 100; ++i) {
    CHECK(dist_a.sample(g_a) == dist_b.sample(g_b));
  }
}

TEST_CASE("nexenne::random::normal_distribution is reproducible across equal-seed engines") {
  rnd::pcg32 g1{404, 7};
  rnd::pcg32 g2{404, 7};
  rnd::normal_distribution<double> d1{1.0, 2.0};
  rnd::normal_distribution<double> d2{1.0, 2.0};
  for (int i{0}; i < 500; ++i) {
    CHECK(d1.sample(g1) == d2.sample(g2));
  }
}

TEST_CASE("nexenne::random::normal free function matches its parameters") {
  rnd::pcg32 g{606, 1};
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{rnd::normal(g, 4.0, 2.0)};
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(4.0).epsilon(0.03));
  CHECK(std::sqrt(var) == doctest::Approx(2.0).epsilon(0.03));
}

TEST_CASE("nexenne::random::exponential_distribution is non-negative with mean 1/rate") {
  rnd::pcg32 g{13, 1};
  rnd::exponential_distribution<double> dist{2.0};
  double sum{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
  }
  CHECK(sum / kN == doctest::Approx(0.5).epsilon(0.03));
}

TEST_CASE(
  "nexenne::random::exponential_distribution mean and variance match 1/lambda, 1/lambda^2"
) {
  rnd::pcg32 g{131, 1};
  constexpr double lambda{0.5};
  rnd::exponential_distribution<double> dist{lambda};
  CHECK(dist.rate() == doctest::Approx(lambda));
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(2.0).epsilon(0.03));
  CHECK(var == doctest::Approx(4.0).epsilon(0.05));
}

TEST_CASE("nexenne::random::exponential_distribution honours large and small rates") {
  {
    rnd::pcg32 g{132, 1};
    rnd::exponential_distribution<double> dist{1000.0};
    double sum{0.0};
    for (int i{0}; i < kN; ++i) {
      auto const x{dist.sample(g)};
      REQUIRE(x >= 0.0);
      sum += x;
    }
    CHECK(sum / kN == doctest::Approx(0.001).epsilon(0.04));
  }
  {
    rnd::pcg32 g{133, 1};
    rnd::exponential_distribution<double> dist{0.01};
    double sum{0.0};
    for (int i{0}; i < kN; ++i) {
      auto const x{dist.sample(g)};
      REQUIRE(x >= 0.0);
      sum += x;
    }
    CHECK(sum / kN == doctest::Approx(100.0).epsilon(0.04));
  }
}

TEST_CASE("nexenne::random::exponential_distribution default rate is one") {
  rnd::exponential_distribution<double> const dist{};
  CHECK(dist.rate() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::random::exponential_distribution is reproducible across equal-seed engines") {
  rnd::pcg32 g1{134, 9};
  rnd::pcg32 g2{134, 9};
  rnd::exponential_distribution<double> const dist{3.0};
  for (int i{0}; i < 500; ++i) {
    CHECK(dist.sample(g1) == dist.sample(g2));
  }
}

TEST_CASE("nexenne::random::gamma_distribution has mean shape*scale (alpha>=1 and alpha<1)") {
  rnd::pcg32 g{17, 1};
  rnd::gamma_distribution<double> big{2.0, 1.5};
  double sum{0.0};
  for (int i{0}; i < kN; ++i) {
    sum += big.sample(g);
  }
  CHECK(sum / kN == doctest::Approx(3.0).epsilon(0.04));

  rnd::gamma_distribution<double> small{0.5, 2.0};
  double sum2{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{small.sample(g)};
    REQUIRE(x >= 0.0);
    sum2 += x;
  }
  CHECK(sum2 / kN == doctest::Approx(1.0).epsilon(0.05));
}

TEST_CASE("nexenne::random::gamma_distribution variance matches shape*scale^2 (alpha>=1)") {
  rnd::pcg32 g{171, 1};
  constexpr double k{3.0};
  constexpr double theta{2.0};
  rnd::gamma_distribution<double> dist{k, theta};
  CHECK(dist.shape() == doctest::Approx(k));
  CHECK(dist.scale() == doctest::Approx(theta));
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(6.0).epsilon(0.04));
  CHECK(var == doctest::Approx(12.0).epsilon(0.06));
}

TEST_CASE("nexenne::random::gamma_distribution variance matches shape*scale^2 (alpha<1 boost)") {
  rnd::pcg32 g{172, 1};
  constexpr double k{0.3};
  constexpr double theta{4.0};
  rnd::gamma_distribution<double> dist{k, theta};
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(1.2).epsilon(0.05));
  CHECK(var == doctest::Approx(4.8).epsilon(0.08));
}

TEST_CASE("nexenne::random::gamma_distribution shape exactly one reduces to exponential") {
  rnd::pcg32 g{173, 1};
  constexpr double theta{2.5};
  rnd::gamma_distribution<double> dist{1.0, theta};
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const x{dist.sample(g)};
    REQUIRE(x >= 0.0);
    sum += x;
    sum_sq += x * x;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(theta).epsilon(0.04));
  CHECK(var == doctest::Approx(theta * theta).epsilon(0.06));
}

TEST_CASE("nexenne::random::gamma_distribution default params and accessors") {
  rnd::gamma_distribution<double> const dist{};
  CHECK(dist.shape() == doctest::Approx(1.0));
  CHECK(dist.scale() == doctest::Approx(1.0));
}

TEST_CASE("nexenne::random::gamma_distribution is reproducible across equal-seed engines") {
  for (auto const shape : {2.5, 0.4}) {
    rnd::pcg32 g1{174, 5};
    rnd::pcg32 g2{174, 5};
    rnd::gamma_distribution<double> const dist{shape, 1.5};
    for (int i{0}; i < 500; ++i) {
      CHECK(dist.sample(g1) == dist.sample(g2));
    }
  }
}

TEST_CASE("nexenne::random::poisson_distribution has mean lambda (small and large)") {
  rnd::pcg32 g{19, 1};
  rnd::poisson_distribution<int> small{4.0};
  long long sum{0};
  for (int i{0}; i < kN; ++i) {
    auto const k{small.sample(g)};
    REQUIRE(k >= 0);
    sum += k;
  }
  CHECK(static_cast<double>(sum) / kN == doctest::Approx(4.0).epsilon(0.03));

  rnd::poisson_distribution<int> big{50.0};
  long long sum2{0};
  for (int i{0}; i < kN; ++i) {
    sum2 += big.sample(g);
  }
  CHECK(static_cast<double>(sum2) / kN == doctest::Approx(50.0).epsilon(0.03));
}

TEST_CASE("nexenne::random::poisson_distribution variance equals lambda on the Knuth path") {
  rnd::pcg32 g{191, 1};
  constexpr double lambda{4.0};
  rnd::poisson_distribution<int> dist{lambda};
  CHECK(dist.mean() == doctest::Approx(lambda));
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const k{dist.sample(g)};
    REQUIRE(k >= 0);
    sum += k;
    sum_sq += static_cast<double>(k) * k;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(lambda).epsilon(0.03));
  CHECK(var == doctest::Approx(lambda).epsilon(0.04));
}

TEST_CASE("nexenne::random::poisson_distribution variance ~ lambda on the large-lambda path") {
  rnd::pcg32 g{192, 1};
  constexpr double lambda{50.0};
  rnd::poisson_distribution<int> dist{lambda};
  CHECK(dist.mean() == doctest::Approx(lambda));
  double sum{0.0};
  double sum_sq{0.0};
  for (int i{0}; i < kN; ++i) {
    auto const k{dist.sample(g)};
    REQUIRE(k >= 0);
    sum += k;
    sum_sq += static_cast<double>(k) * k;
  }
  auto const mean{sum / kN};
  auto const var{sum_sq / kN - mean * mean};
  CHECK(mean == doctest::Approx(lambda).epsilon(0.02));
  CHECK(var == doctest::Approx(lambda).epsilon(0.06));
}

TEST_CASE("nexenne::random::poisson_distribution at lambda just below and at the path boundary") {
  {
    rnd::pcg32 g{193, 1};
    rnd::poisson_distribution<int> dist{29.0};
    long long sum{0};
    for (int i{0}; i < kN; ++i) {
      auto const k{dist.sample(g)};
      REQUIRE(k >= 0);
      sum += k;
    }
    CHECK(static_cast<double>(sum) / kN == doctest::Approx(29.0).epsilon(0.03));
  }
  {
    rnd::pcg32 g{194, 1};
    rnd::poisson_distribution<int> dist{30.0};
    long long sum{0};
    for (int i{0}; i < kN; ++i) {
      auto const k{dist.sample(g)};
      REQUIRE(k >= 0);
      sum += k;
    }
    CHECK(static_cast<double>(sum) / kN == doctest::Approx(30.0).epsilon(0.03));
  }
}

TEST_CASE("nexenne::random::poisson_distribution at lambda zero always yields zero") {
  rnd::pcg32 g{195, 1};
  rnd::poisson_distribution<int> dist{0.0};
  CHECK(dist.mean() == doctest::Approx(0.0));
  for (int i{0}; i < 1000; ++i) {
    CHECK(dist.sample(g) == 0);
  }
}

TEST_CASE("nexenne::random::poisson_distribution is reproducible across equal-seed engines") {
  for (auto const lambda : {4.0, 50.0}) {
    rnd::pcg32 g1{196, 3};
    rnd::pcg32 g2{196, 3};
    rnd::poisson_distribution<int> const dist{lambda};
    for (int i{0}; i < 500; ++i) {
      CHECK(dist.sample(g1) == dist.sample(g2));
    }
  }
}

TEST_CASE(
  "nexenne::random::poisson_distribution saturates instead of overflowing the result type"
) {
  rnd::pcg32 g{5, 1};
  rnd::poisson_distribution<std::uint16_t> small_type{1.0e6};
  for (int i{0}; i < 1000; ++i) {
    auto const k{small_type.sample(g)};
    CHECK(k <= std::numeric_limits<std::uint16_t>::max());
  }
  CHECK(small_type.sample(g) == std::numeric_limits<std::uint16_t>::max());
}

TEST_CASE("nexenne::random::discrete_distribution selects in proportion to weights") {
  rnd::pcg32 g{23, 1};
  std::array<double, 3> const weights{1.0, 3.0, 6.0};
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
  std::array<double, 4> const weights{0.0, 3.0, 0.0, 1.0};
  rnd::discrete_distribution<double> dist{weights};
  std::array<int, 4> hits{};
  for (int i{0}; i < kN; ++i) {
    auto const idx{dist.sample(g)};
    REQUIRE(idx < 4);
    ++hits[idx];
  }
  CHECK(hits[0] == 0);
  CHECK(hits[2] == 0);
  CHECK(hits[1] > 0);
  CHECK(hits[3] > 0);
  CHECK(static_cast<double>(hits[1]) / kN == doctest::Approx(0.75).epsilon(0.05));
}

TEST_CASE("nexenne::random::discrete_distribution reports sentinel for degenerate inputs") {
  rnd::pcg32 g{3, 1};
  rnd::discrete_distribution<double> const empty{std::array<double, 0>{}};
  CHECK(empty.sample(g) == empty.size());
  CHECK(empty.size() == 0);

  std::array<double, 3> const zeros{0.0, -2.0, 0.0};
  rnd::discrete_distribution<double> const dead{zeros};
  CHECK(dead.total_weight() == 0.0);
  CHECK(dead.sample(g) == dead.size());
}

TEST_CASE("nexenne::random::discrete_distribution exposes size, total, and probabilities") {
  std::array<double, 4> const weights{1.0, 2.0, 0.0, 7.0};
  rnd::discrete_distribution<double> const dist{weights};
  CHECK(dist.size() == 4);
  CHECK(dist.total_weight() == doctest::Approx(10.0));
  CHECK(dist.probability(0) == doctest::Approx(0.1));
  CHECK(dist.probability(1) == doctest::Approx(0.2));
  CHECK(dist.probability(2) == doctest::Approx(0.0));
  CHECK(dist.probability(3) == doctest::Approx(0.7));
  CHECK(dist.probability(4) == doctest::Approx(0.0));

  std::array<double, 3> const mixed{4.0, -5.0, 1.0};
  rnd::discrete_distribution<double> const clamped{mixed};
  CHECK(clamped.total_weight() == doctest::Approx(5.0));
  CHECK(clamped.probability(0) == doctest::Approx(0.8));
  CHECK(clamped.probability(1) == doctest::Approx(0.0));
  CHECK(clamped.probability(2) == doctest::Approx(0.2));
}

TEST_CASE("nexenne::random::discrete_distribution with a single weight always picks index 0") {
  rnd::pcg32 g{231, 1};
  std::array<double, 1> const weights{42.0};
  rnd::discrete_distribution<double> const dist{weights};
  CHECK(dist.probability(0) == doctest::Approx(1.0));
  for (int i{0}; i < 1000; ++i) {
    CHECK(dist.sample(g) == 0);
  }
}

TEST_CASE("nexenne::random::discrete_distribution with equal weights is approximately uniform") {
  rnd::pcg32 g{232, 1};
  constexpr std::size_t buckets{5};
  std::array<double, buckets> const weights{1.0, 1.0, 1.0, 1.0, 1.0};
  rnd::discrete_distribution<double> const dist{weights};
  std::array<int, buckets> hits{};
  for (int i{0}; i < kN; ++i) {
    auto const idx{dist.sample(g)};
    REQUIRE(idx < buckets);
    ++hits[idx];
  }
  for (auto const count : hits) {
    CHECK(static_cast<double>(count) / kN == doctest::Approx(1.0 / buckets).epsilon(0.05));
  }
}

TEST_CASE("nexenne::random::discrete_distribution empirical frequencies pass a chi-square check") {
  rnd::pcg32 g{233, 1};
  std::array<double, 4> const weights{2.0, 3.0, 5.0, 10.0};
  rnd::discrete_distribution<double> const dist{weights};
  std::array<int, 4> hits{};
  for (int i{0}; i < kN; ++i) {
    auto const idx{dist.sample(g)};
    REQUIRE(idx < 4);
    ++hits[idx];
  }
  double chi_sq{0.0};
  for (std::size_t i{0}; i < 4; ++i) {
    auto const expected{dist.probability(i) * kN};
    auto const diff{hits[i] - expected};
    chi_sq += diff * diff / expected;
  }
  // Pearson chi-square with 3 degrees of freedom: the 0.999 critical value is ~16.27.
  CHECK(chi_sq < 16.27);
}

TEST_CASE("nexenne::random::discrete_distribution is reproducible across equal-seed engines") {
  rnd::pcg32 g1{234, 8};
  rnd::pcg32 g2{234, 8};
  std::array<double, 4> const weights{1.0, 2.0, 3.0, 4.0};
  rnd::discrete_distribution<double> const dist{weights};
  for (int i{0}; i < 1000; ++i) {
    CHECK(dist.sample(g1) == dist.sample(g2));
  }
}

TEST_CASE("distributions accept their documented positive parameters") {
  rnd::pcg32 g{2, 1};

  rnd::exponential_distribution<double> const e{2.0};
  CHECK(e.rate() == doctest::Approx(2.0));
  CHECK(e.sample(g) >= 0.0);

  rnd::gamma_distribution<double> const ga{2.0, 3.0};
  CHECK(ga.shape() == doctest::Approx(2.0));
  CHECK(ga.scale() == doctest::Approx(3.0));
  CHECK(ga.sample(g) >= 0.0);

  rnd::poisson_distribution<> const p{4.0};
  CHECK(p.mean() == doctest::Approx(4.0));
  nexenne::utility::ignore(p.sample(g));
}

/**
 * @brief Engine stuck at its maximum output.
 *
 * \c uniform_real then returns its largest value below one, the draw that
 * exposes rounding at the top of the range.
 *
 * @pre None.
 * @post None.
 */
struct top_engine {
  using result_type = std::uint64_t;  ///< 64-bit output word.

  /**
   * @brief Returns the maximum 64-bit word on every call.
   *
   * @return \c std::numeric_limits<result_type>::max().
   *
   * @pre None.
   * @post None.
   */
  auto next() noexcept -> result_type {
    return std::numeric_limits<result_type>::max();
  }
};

TEST_CASE("nexenne::random::discrete_distribution<float> never picks a trailing zero weight") {
  rnd::discrete_distribution<float> const dist{1.0F, 0.0F};
  top_engine g;
  CHECK(dist.sample(g) == 0);
}

TEST_CASE("nexenne::random::discrete_distribution treats non-finite weights as zero") {
  auto const inf{std::numeric_limits<double>::infinity()};
  auto const nan{std::numeric_limits<double>::quiet_NaN()};
  rnd::discrete_distribution<double> const dist{inf, 1.0, nan, 3.0};
  CHECK(dist.total_weight() == doctest::Approx(4.0));
  CHECK(dist.probability(0) == doctest::Approx(0.0));
  CHECK(dist.probability(2) == doctest::Approx(0.0));
  CHECK(dist.probability(3) == doctest::Approx(0.75));
  rnd::pcg32 g{9, 9};
  for (int i{0}; i < 2000; ++i) {
    auto const pick{dist.sample(g)};
    REQUIRE((pick == 1 || pick == 3));
  }
}

// A filter view is rejected: it caches begin(), so it cannot be iterated through const.
template <typename T>
concept poisson_type = requires { typename rnd::poisson_distribution<T>; };
static_assert(!poisson_type<bool>);
static_assert(poisson_type<int>);
static_assert(!std::is_constructible_v<
              rnd::discrete_distribution<double>,
              decltype(std::declval<std::vector<double>&>() | std::views::filter([](double) {
                         return true;
                       }))>);
static_assert(std::is_constructible_v<rnd::discrete_distribution<double>, std::vector<double>>);

TEST_CASE("nexenne::random::normal_distribution<float> is standard normal") {
  rnd::pcg32 g{21, 4};
  rnd::normal_distribution<float> dist{0.0F, 1.0F};
  double sum{0.0};
  double sum_sq{0.0};
  constexpr int n{100000};
  for (int i{0}; i < n; ++i) {
    auto const x{static_cast<double>(dist.sample(g))};
    REQUIRE(std::isfinite(x));
    sum += x;
    sum_sq += x * x;
  }
  CHECK(sum / n == doctest::Approx(0.0).epsilon(0.02).scale(1.0));
  CHECK(sum_sq / n == doctest::Approx(1.0).epsilon(0.02));
}

}  // namespace
